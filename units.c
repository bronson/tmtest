#include "units.h"
#include <stddef.h>


void run_all_unit_tests()
{
	zutest_tests();
	compare_tests();
	pathstack_tests();
}


#ifdef UNITS_MAIN
int main(int argc, char **argv)
{
	if(argc > 1) {
		// "zutest -f" prints all the failures in the zutest unit tests.
		// This allows you to check the output of each macro.
		run_unit_tests_showing_failures(run_all_unit_tests);
	} else {
		run_unit_tests(run_all_unit_tests);
	}
	// this will never be reached
	return 0;
}
#endif
