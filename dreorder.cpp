
// SPOTs are very important places!

#include <iostream>
#include <cstring>
#include <mutex>
#include <thread>
#include <atomic>

#include <windows.h>

using namespace std;

// Let's catch the secondary incorrect read of the object.
const bool stop_on_secondary_fail = true;


// SPOT: size of huge_object's data. It is an empirical value :)
const int DataSize = 1024*1024*1;
// After filling 'data' member with `ones` we need to get a sum equal `data's` length.
const size_t NeededSum = DataSize;

const uint32_t SignBad = 0xBAD0BAD0;
const uint32_t Sign0 = 0xA0005000;
const uint32_t Sign1 = 0xA1115111;
const uint32_t Sign2 = 0xA2225222;
const uint32_t Sign3 = 0xA3335333;

// This class is huge, so we use DCLP approach.
class huge_class
{
public:
	// SPOT: this member (sign2) has a great impact on FAIL probability.
	uint32_t sign2;
	uint32_t sign0;
	uint8_t  data[DataSize];
	uint32_t sign1;

	huge_class()
	{
		sign0 = Sign0;
		// Filling with `ones`, see above.
		memset(data, 1, DataSize);
		sign1 = Sign1;
	}

	inline size_t get_sum()
	{
		size_t sum = 0;
		for (size_t i = 0; i < DataSize; ++i)
			sum += data[i];
		return sum;
	}
};

template <typename T>
class dclp
{
private:
	// Quite frankly, this pointer must be std::atomic to ensure atomic read/write
	// operations on it. Otherwise we can get partial read/write on some architectures.
	// I want to keep the get_instance() code clean, sorry about that :)
	T *pInstance;

	// Standard DCLP's mutex.
	std::mutex guard;
	// Number of singleton's destruction. For testing purpose only.
	size_t destructed;

public:
	dclp() : pInstance(nullptr), destructed(0) {}
	// SPOT: virtual keyword has a large impact on FAIL probability too :)
	virtual
	~dclp() { put_instance(); }

	T *get_instance()
	{
		T *tmp = pInstance;
		// test mb (data dependency barrier is essential, but it is
		// optional on genuine intel CPUs, afaik).
		atomic_thread_fence(std::memory_order_acquire);
		if (tmp == nullptr) {
			guard.lock();
			tmp = pInstance;
			if (tmp == nullptr) {
				tmp = new T;
				// test mb
				// atomic_thread_fence() synchronizes-with an operations on atomic
				// variables only, not with other atomic_thread_fence(). pInstance
				// is not atomic, so we'll get a race. Moreover, atomic_thread_fence()
				// doesn't imply compiler barrier here, so we can get a race even on
				// strong-ordered CPUs.
				atomic_thread_fence(std::memory_order_release);
				pInstance = tmp;
			}
			guard.unlock();
		}
		return tmp;
	}

	// Testing-only purpose function. "Destroys" the lazy singleton.
	void put_instance()
	{
		guard.lock();
		T *tmp = pInstance;
		if (tmp) {
			pInstance = nullptr;
			// Dances with OS memory allocator.
			tmp->sign0 = SignBad;
			tmp->sign1 = SignBad + 1;
			memset(tmp->data, 0x55, DataSize);

			delete tmp;
			destructed++;
		}
		// Need to ensure to see the new object's state in thread_func().
		atomic_thread_fence(std::memory_order_seq_cst);

		// This implies full memory barrier (or not?).
		// Anyway, this function is single-threaded by design.
		guard.unlock();
	}

	inline size_t get_destruction_count() { return destructed; }
};

static void workload(int id)
{
	static std::atomic<size_t> count(0);
	size_t i, j, k;

	this_thread::yield();

	if (1) {
		i = (count += 10 * (id + 1));
		for (j = i; j != 0; --j)
			for (k = j; k != 0; --k)
				;
		if (i % 90000 == 0)
			cout << "id = " << id << ", count = " << i << endl;
	}
}

// Our singleton.
static dclp<huge_class> a;
// Exit flag. Implicit sec_cst operations are not important.
static std::atomic<int> exiting(0);
// aux mutex for printing.
static std::mutex gmutex;

static unsigned __stdcall thread_func(void *param)
{
	// volatile keyword may reduce FAIL probability.
	volatile uint32_t sign0, sign1;
	size_t sum;
	bool sane;
	int id = (int)param;

	// Pair barrier with put_instance()'s one.
	atomic_thread_fence(std::memory_order_seq_cst);

	// Simulate some work. Exponentially by design, but who cares.
	workload(id);

	// Get the object's instance!
	auto p = a.get_instance();

	auto check_object = [&]() {
		sign0 = p->sign0;
		sign1 = p->sign1;
		sum = p->get_sum();
		sane = (sign0 == Sign0) && (sum == NeededSum)/* && (sign1 == Sign1)*/;
	};
	auto print_stats = [&]() {
		cout << "\nobj address = " << p
			 << "\nthread id   = " << id << hex
			 << "\nsign0       = 0x" << sign0 << ", needed 0x" << Sign0
			 << "\nsign1       = 0x" << sign1 << ", needed 0x" << Sign1
			 << "\ndata sum    = 0x" << sum << ", needed 0x" << NeededSum
			 << "\niteration   = " << dec << a.get_destruction_count()
			 << endl;
	};

	check_object();
	while (!sane) {
		//if (!id) break;
		std::lock_guard<std::mutex> lock(gmutex);

		if (!stop_on_secondary_fail) {
			cout << "FAIL!";
			print_stats();
			cout << "\nSecondary read... ";
		}

		// This mb cannot help us due to lack of a pair.
		atomic_thread_fence(std::memory_order_seq_cst);
		check_object();

		if (!stop_on_secondary_fail) {
			cout << (sane ? "OK" : "FAIL AGAIN!");
			print_stats();
			exiting = -1;
			break;
		}
		// We need secondary fail at least.
		if (sane) break;

		cout << "GOT DOUBLE FAILURE!";
		// Waiting for correct data.
		while (!sane) {
			print_stats();
			cout << "\nNext read... ";
			check_object();
		}
		print_stats();
		exiting = -1;
	}

	_endthreadex(0);
    return 0;
}

static void termhandler(int reason)
{
	cout << "\nGot signal, exiting..." << endl;
	exiting = reason;
}

int main()
{
	cout << "\nDCLP checker v1.24 by Konstantin Chaplaev (c) 2013.";
	cout << "\nBuilt on " << __DATE__ << " " __TIME__ << endl << endl;

	// Set Ctrl+C handlers.
	signal(SIGINT, termhandler);
	signal(SIGTERM, termhandler);

	// Initialize random seed.
	srand(time(NULL));

	const unsigned ncores = thread::hardware_concurrency();
	const unsigned tcount = ncores + 0; // 0+ for some pressure :)
	cout << ncores << " concurrent threads are supported, " << tcount << " are used.\n";
	cout << "Please, be patient or press Ctrl+C to exit.\n" << endl;

	while (!exiting) {
		HANDLE th [tcount];
		// Randomizing threads per cores will improve fail probability is rare cases.
		int rnd = rand();

		for (unsigned i = 0; i < tcount; ++i) {
			th[i] = (HANDLE)_beginthreadex(NULL, 0, thread_func, (void *)i, 0, NULL);
			if (th[i] == (HANDLE)-1L) {
				cout << "Error with _beginthread!" << endl;
				exit(-2);
			}
			DWORD err = SetThreadAffinityMask(th[i], (1 << ((i + rnd) % ncores)));
			if (!err) {
				DWORD glerr = GetLastError();
				cout << "Error with SetThreadAffinityMask (err = 0x" << hex << err << ")!" << endl;
				cout << "GetLastError() is 0x" << glerr << endl;
				exit(-3);
			}
        }

		for (unsigned i = 0; i < tcount; ++i) {
			DWORD err = WaitForSingleObject(th[i], INFINITE);
			if (err != WAIT_OBJECT_0) {
				DWORD glerr = GetLastError();
				cout << "Error with WaitForSingleObject (err = 0x" << hex << err << ")!" << endl;
				cout << "GetLastError() is 0x" << glerr << endl;
				exit(-4);
			}
			CloseHandle(th[i]);
        }

		a.put_instance();
	}

	return exiting;
}
