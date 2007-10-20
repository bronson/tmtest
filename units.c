#include "units.h"
#include <stddef.h>


void all_tests()
{
	mutest_test_assert_flavor();
	compare_tests();
	pathstack_tests();
}


#ifdef UNITS_MAIN

#include "mutest/mutest.h"

int main(int argc, char **argv)
{
	all_tests();
	mutest_exit();
	
	// this will never be reached
	return 0;
}

#endif
