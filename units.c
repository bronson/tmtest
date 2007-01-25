#include "units.h"
#include "compare.h"
#include <stddef.h>


zutest_suite all_unit_tests[] = {
	zutest_tests,	// run a self-check on the unit test library
	compare_tests,
	NULL
};

