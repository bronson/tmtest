/* test.h
 * Scott Bronson
 * 29 Dec 2004
 *
 * All the data needed to run a test and print the results.
 */

#include "matchval.h"
#include "r2scan.h"


// all strings are malloc'd and need to be freed when the test is finished.

struct test {
    const char *testfilename;   // will be "-" if reading from stdin.
    scanstate testfile;         // may be stdin so seeking is not allowed.

    int rewritefd;         // where to dump the rewritten test.  -1 if we're just running the tests, or the fd of the file that should receive the test contents.

    int outfd;
    int errfd;
    int statusfd;
    int exitno;

    matchval exitno_match;
    matchval stdout_match;
    matchval stderr_match;

};


void test_command_copy(struct test *test, FILE *fp);

void test_results(struct test *test);
void dump_results(struct test *test);

void print_test_summary();

void test_init(struct test *test);
void test_free(struct test *test);

