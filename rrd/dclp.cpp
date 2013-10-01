
#include "stdafx.h"
#include <relacy/relacy_std.hpp>

#include "dclp.h"


int main()
{
	rl::test_params p;

	p.iteration_count = 1000000;
	p.search_type = rl::sched_full;
	p.context_bound = 5;
	rl::simulate<dlcp_test>(p);

	return 0;
}
