#include "units.h"
#include <stddef.h>


void all_tests()
{
	ctest_test_asserts();
	compare_tests();
	pathstack_tests();
}


#ifdef UNITS_MAIN

#include "ctest/ctest.h"

int main(int argc, char **argv)
{
	all_tests();
	ctest_exit();
	
	// this will never be reached
	return 0;
}

#endif
