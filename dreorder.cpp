
#include <iostream>
#include <cstring>
#include <mutex>
#include <thread>
#include <atomic>

#include <windows.h>

using namespace std;

// let's catch the secondary incorrect read of the object
const bool stop_on_secondary_fail = true;

const int DataSize = 1024*1024*1;
const size_t NeededSum = DataSize;

const uint32_t SignBad = 0xBAD0BAD0;
const uint32_t Sign0 = 0xA0005000;
const uint32_t Sign1 = 0xA1115111;
const uint32_t Sign2 = 0xA2225222;
const uint32_t Sign3 = 0xA3335333;

class huge_class
{
public:
	// SPOT: this member (sign2) has a great impact on GOTCHA probability
	uint32_t sign2;
	uint32_t sign0;
	uint8_t  data[DataSize];
	uint32_t sign1;

	huge_class()
	{
		sign0 = Sign0;
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
	T *pInstance;
	std::mutex guard;
	size_t destructed;

public:
	dclp() : pInstance(nullptr), destructed(0) {}
	// SPOT: virtual keyword has a large impact on GOTCHA probability too :)
	virtual
	~dclp() { put_instance(); }

	T *get_instance()
	{
		T *tmp = pInstance;
		// test mb (this mb is optional on geniue intel cpus)
		//atomic_thread_fence(std::memory_order_seq_cst);
		if (tmp == nullptr) {
			guard.lock();
			tmp = pInstance;
			if (tmp == nullptr) {
				tmp = new T;
				// test mb
				//atomic_thread_fence(std::memory_order_seq_cst);
				pInstance = tmp;
			}
			guard.unlock();
		}
		return tmp;
	}

	// testing-only purpose function
	void put_instance()
	{
		guard.lock();
		T *tmp = pInstance;
		if (tmp) {
			pInstance = nullptr;
			//
			tmp->sign0 = SignBad;
			tmp->sign1 = SignBad + 1;
			memset(tmp->data, 0x55, DataSize);
			delete tmp;
			destructed++;
		}
		atomic_thread_fence(std::memory_order_seq_cst);
		// this implies full memory barrier (or not?)
		// anyway, this function is single-thread by design
		guard.unlock();
	}

	inline size_t get_destruction_count() { return destructed; }
};

static dclp<huge_class> a;
static std::mutex gmutex;
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

static std::atomic<int> exiting(0);
static unsigned __stdcall thread_func(void *param)
{
	// volatile keyword may reduce GOTCHA probability
	volatile uint32_t sign0, sign1;
	size_t sum;
	bool sane;
	int id = (int)param;

	// simulate some work
	workload(id);

	// Getting instance
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
	if (1) {
		if (!sane) {
			std::lock_guard<std::mutex> lock(gmutex);
			cout << "GOTCHA!";
			print_stats();

			cout << "\nSecondary read... ";
			// this mb cannot help due to lack of a pair mb
			atomic_thread_fence(std::memory_order_seq_cst);
			check_object();

			cout << (sane ? "OK" : "GOTCHA AGAIN!");
			if (!(sane && stop_on_secondary_fail)) {
				print_stats();
				exiting = -1;
			} else
				cout << "\n\n";
		}
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
	cout << "\nDCLP checker v1.22 by Konstantin Chaplaev (c) 2013.";
	cout << "\nBuilt on " << __DATE__ << " " __TIME__ << endl << endl;

	// Set Ctrl+C handlers
	signal(SIGINT, termhandler);
	signal(SIGTERM, termhandler);

	// initialize random seed
	srand(time(NULL));

	const unsigned ncores = thread::hardware_concurrency();
	const unsigned tcount = ncores + 0; // for some pressure :)
	cout << ncores << " concurrent threads are supported, " << tcount << " are used.\n";
	cout << "Please, be patient or press Ctrl+C to exit.\n" << endl;

	while (!exiting) {
		HANDLE th [tcount];
		// randomizing threads per cores will improve fail probability is rare cases
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
