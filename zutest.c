/* zutest.c
 * Scott Bronson
 * 6 Mar 2006
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include "zutest.h"


/**
 * A single procedure is called a test.  If any of the asserts fail
 * within a test, the test itself is stopped but all other tests will
 * be run.
 *
 * Most files will contain a number of tests.  These tests are organized
 * into a suite.  A test suite contains one or more tests.
 *
 * Test suites are organized into a test battery.  You don't need to
 * know this except when specifying all the suites that will be tested.
 */


static jmp_buf test_bail;
static int tests_run;
static int successes;
static int failures;


void zutest_fail(const char *file, int line, const char *func, 
		const char *msg, ...)
{
	va_list ap;

	fprintf(stderr, "FAIL %s at %s line %d:\n\t", func, file, line);
	va_start(ap, msg);
	vfprintf(stderr, msg, ap);
	va_end(ap);
	fputc('\n', stderr);

	longjmp(test_bail, 1);
}


void run_zutest_suite(const zutest_suite suite)
{
	const zutest_proc *test;

	for(test=suite; *test; test++) {
		tests_run += 1;
		if(!setjmp(test_bail)) {
			(*test)();
			successes += 1;
		} else {
			failures += 1;
		}
	}
}


void run_zutest_battery(const zutest_battery battery)
{
	zutest_suite *suite;

	for(suite=battery; *suite; suite++) {
		run_zutest_suite(*suite);
	}
}


void print_zutest_results()
{
	if(failures == 0) {
		printf("%d tests run, %d successes.\n", successes, successes);
		return;
	}

	printf("%d failures of %d tests run!\n", failures, tests_run);
}


void run_unit_tests(const zutest_battery battery)
{
	run_zutest_battery(battery);
	print_zutest_results();
	exit(failures < 99 ? failures : 99);
}


/**
 * Examines the command-line arguments.  If "--run-unit-tests" is
 * the first argument, then it runs the unit tests (further arguments
 * may affect how the tests are processed).  This routine exits with
 * a nonzero result code if any test fails; otherwise it exits with 0.
 * It never returns.
 *
 * If --run-unit-tests is not on the command line, this routine returns
 * without doing anything.
 */

void unit_test_check(int argc, char **argv, const zutest_battery battery)
{
	if(argc > 1 && strcmp(argv[1],"--run-unit-tests") == 0) {
		run_unit_tests(battery);
	}
}

