
#include "stdafx.h"
#include <relacy/relacy_std.hpp>


const uint32_t Sign0 = 1122334455;


// This class is huge, so we use DCLP approach.
class huge_class
{
public:
	std::atomic<uint32_t> sign0;

	huge_class() { sign0.store(Sign0, std::memory_order_relaxed); }
	uint32_t get(){ return sign0.load(std::memory_order_relaxed); }
};

template <typename T>
class dlcp
{
private:
	T *pInstance;
	std::mutex guard;

public:

	dlcp() : pInstance(nullptr) {}

	T *get_instance()
	{
		T *tmp = pInstance;
		// test mb
		atomic_thread_fence(std::memory_order_acquire);
		if (tmp == nullptr) {
			guard.lock($);
			tmp = pInstance;
			if (tmp == nullptr) {
				tmp = new T();
				// test mb
				atomic_thread_fence(std::memory_order_release);
				pInstance = tmp;
			}
			guard.unlock($);
		}
		return tmp;
	}

	void put_instance()
	{
		guard.lock($);
		T *tmp = pInstance;
		if (tmp) {
			pInstance = nullptr;
			atomic_thread_fence(std::memory_order_seq_cst);
			delete tmp;
		}
		guard.unlock($);
	}
};

struct dlcp_test : rl::test_suite<dlcp_test, 2> {
	dlcp<huge_class> t;
	uint32_t val1, val2;
	huge_class *p1, *p2;

	void before() { t.put_instance(); }

	void thread(unsigned thread_index)
	{
		if ((thread_index % 2) == 0) {
			p1 = t.get_instance();
			val1 = p1->get();
			RL_ASSERT(val1 == Sign0);

		} else if ((thread_index % 2) != 0) {
			p2 = t.get_instance();
			val2 = p2->get();
			RL_ASSERT(val2 == Sign0);
		}
	}

	void after()
	{
		RL_ASSERT(p1 == p2);
		t.put_instance();
	}
};
