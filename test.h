/* test.h
 * Scott Bronson
 * 29 Dec 2004
 *
 * All the data needed to run a test and print the results.
 */

#include "pcrs.h"
#include "matchval.h"
#include "r2scan.h"


// all strings are malloc'd and need to be freed when the test is finished.

struct test {
    const char *testfilename;   ///< will be "-" if reading from stdin.
    scanstate testfile;         ///< scans the testfile.  may be stdin so seeking is not allowed.

    int rewritefd;          ///< where to dump the rewritten test.  -1 if we're just running the tests, or the fd of the file that should receive the test contents.

    int outfd;				///< the file that receives the test's stdout.
    int errfd;				///< the file that receives the test's stderr.
    int statusfd;			///< receives the runtime test status messages.
    int exitno;				///< the test's actual exit value.

	pcrs_job *eachline;		///< a linked list of pcrs jobs to be applied to each line.

	char *diffname;			///< if we're diffing against stdin, this contains the name of the required tempfile.
	int diff_fd;			///< if diffname is set, then this is the fd of the tempfile we're using to store stdin.

	int expected_exitno;	///< the test's expected exit value.  this is only valid when stderr_match != match_unknown.

    matchval exitno_match;	///< tells whether the expected and actual exit values match.
    matchval stdout_match;	///< tells whether the expected and actual stdout matches.
    matchval stderr_match;	///< tells whether the expected and actual stderr matches.
};


void test_command_copy(struct test *test, FILE *fp);

void test_results(struct test *test);
void dump_results(struct test *test);

void print_test_summary();

void test_init(struct test *test);
void test_free(struct test *test);


// random utility function for start_diff:
void write_raw_file(int outfd, int infd);

