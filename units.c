#include "units.h"
#include <stddef.h>


void all_tests()
{
	mutest_test_assert_flavor();
	compare_tests();
	pathstack_tests();
}
