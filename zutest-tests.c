/* zutest.c
 * Scott Bronson
 * 6 Mar 2006
 *
 * Runs a bunch of unit tests.  Ensures each macro expands without error
 * and passes a simple test.  Does not test each macro for all failure
 * modes because that would be a hell of a lot of code.
 *
 * A certain number of tests are expected to fail.  If the name of a
 * failing test does not end in "_fail" then it is NOT expected to fail.
 */

#include "zutest.h"
#include <string.h>


void test_fail()
{
	Fail("Gone!");
	Fail("Won't be printed");
}


void test_assert_int()
{
	int a=4, b=3, c=4;

	// These should all pass
	AssertEq(a,c);
	AssertNe(a,b);
	AssertGt(a,b);
	AssertGe(a,b);
	AssertGe(a,c);
	AssertLt(b,a);
	AssertLe(b,a);
	AssertLe(c,a);
	AssertIntEq(a,c);
	AssertIntNe(a,b);
	AssertIntGt(a,b);
	AssertIntGe(a,b);
	AssertIntGe(a,c);
	AssertIntLt(b,a);
	AssertIntLe(b,a);
	AssertIntLe(c,a);
	AssertEqHex(a,c);
	AssertNeHex(a,b);
	AssertGtHex(a,b);
	AssertGeHex(a,b);
	AssertGeHex(a,c);
	AssertLtHex(b,a);
	AssertLeHex(b,a);
	AssertLeHex(c,a);
}

void test_assert_int_eq_fail()
{
	int a=4, b=3;
	AssertEq(a,b);
}
void test_assert_int_ne_fail()
{
	int a=4, b=3;
	AssertNe(a,b);
}
void test_assert_int_gt_fail()
{
	int a=4, b=3;
	AssertGt(a,b);
}
void test_assert_int_ge_fail()
{
	int a=4, b=3;
	AssertGe(a,b);
}
void test_assert_int_lt_fail()
{
	int a=4, b=3;
	AssertLt(a,b);
}
void test_assert_int_le_fail()
{
	int a=4, b=3;
	AssertLe(a,b);
}

void test_assert_ptr()
{
	int a, b;
	int *ap = &a;
	int *bp = &b;
	int *cp = &a;
	int *n = NULL;

	// These should all pass
	AssertPtr(ap);
	AssertNull(n);
	AssertPtrEq(ap,cp);
	AssertPtrNe(ap,bp);
	AssertPtrGt(ap,bp);
	AssertPtrGe(ap,bp);
	AssertPtrGe(ap,cp);
	AssertPtrLt(bp,ap);
	AssertPtrLe(bp,ap);
	AssertPtrLe(cp,ap);
}

void test_assert_ptr_fail()
{
	AssertPtr(NULL);
}

void test_assert_ptr_null_fail()
{
	void (*p)() = &test_assert_ptr_null_fail;
	AssertPtrNull(p);
}

void test_assert_int_hex_eq_fail()
{
	int a=431, b=577;
	AssertEqHex(a,b);
}

void test_assert_float()
{
	float a=0.0004, b=0.0003, c=0.0004;

	// These should all pass
	AssertFloatEq(a,c);
	AssertFloatNe(a,b);
	AssertFloatGt(a,b);
	AssertFloatGe(a,b);
	AssertFloatGe(a,c);
	AssertFloatLt(b,a);
	AssertFloatLe(b,a);
	AssertFloatLe(c,a);
	AssertDblEq(a,c);
	AssertDblNe(a,b);
	AssertDblGt(a,b);
	AssertDblGe(a,b);
	AssertDblGe(a,c);
	AssertDblLt(b,a);
	AssertDblLe(b,a);
	AssertDblLe(c,a);
	AssertDoubleEq(a,c);
	AssertDoubleNe(a,b);
	AssertDoubleGt(a,b);
	AssertDoubleGe(a,b);
	AssertDoubleGe(a,c);
	AssertDoubleLt(b,a);
	AssertDoubleLe(b,a);
	AssertDoubleLe(c,a);
}


void test_assert_float_eq_fail()
{
	double a=1.0/3.0, b=0.4;
	AssertFloatEq(a,b);
}

void test_assert_strings()
{
	const char *a = "Bogozity";
	const char *b = "Arclamp";
	const char *c = "Bogozity";

	// These should all pass
	AssertStrEq(a,c);
	AssertStrNe(a,b);
	AssertStrGt(a,b);
	AssertStrGe(a,b);
	AssertStrGe(a,c);
	AssertStrLt(b,a);
	AssertStrLe(b,a);
	AssertStrLe(c,a);
}

void test_assert_string_eq_fail()
{
	const char *a = "A";
	const char *b = "a";

	AssertStrEq(a,b);
}


void test_assert_string_ne_fail()
{
	const char *a = "A";
	const char *b = "A";

	AssertStrNe(a,b);
}

void test_assert_string_gt_fail()
{
	const char *a = "A";
	const char *b = "A";

	AssertStrGt(a,b);
}

zutest_proc zutest_tests[] = {
	test_assert_int,
	test_assert_ptr,
	test_assert_float,
	test_assert_strings,
	NULL
};

zutest_proc zutest_empty_suite[] = {
	NULL
};

zutest_proc zutest_failures[] = {
	test_fail,
	test_assert_int_eq_fail,
	test_assert_ptr_fail,
	test_assert_ptr_null_fail,
	test_assert_int_hex_eq_fail,
	test_assert_float_eq_fail,
	test_assert_string_eq_fail,
	test_assert_string_ne_fail,
	test_assert_string_gt_fail,
	NULL
};

zutest_suite all_zutests[] = {
	zutest_tests,
	zutest_empty_suite,
	zutest_failures,
	NULL
};


int main(int argc, char **argv)
{
	run_unit_tests(all_zutests);
	return 0;
}

