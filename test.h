/* test.h
 * Scott Bronson
 * 29 Dec 2004
 *
 * All the data needed to run a test and print the results.
 */

#include "pcrs.h"
#include "matchval.h"
#include "re2c/scan.h"


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

	int test_was_started;	///< 0 if the test didn't run or we don't know.  1 if we have verified that configuration happened without error and the test was successfully started.  This does not imply that the test completed without errors of course.
	int test_was_disabled;	///< 1 if the test was disabled.  The reason (if there was one) can be found in disable_reason.
	char *disable_reason;	///< if the test was disabled, and the user gave a reason why, that reason is stored here.  must be freed.
	int num_config_files;	///< the number of config files we started to process.  if test_was_started is true, then this is the number of config files we read.
	char *last_file_processed; ///< if it could be discovered, this contains the name of the last file to be started.  must be freed.

	int expected_exitno;	///< the test's expected exit value.  this is only valid when stderr_match != match_unknown.

    matchval exitno_match;	///< tells whether the expected and actual exit values match.
    matchval stdout_match;	///< tells whether the expected and actual stdout matches.
    matchval stderr_match;	///< tells whether the expected and actual stderr matches.
};


void scan_status_file(struct test *test);
void test_command_copy(struct test *test, FILE *fp);

void test_results(struct test *test);
void dump_results(struct test *test);
void print_test_summary();

void test_init(struct test *test);
void test_free(struct test *test);


// random utility function for start_diff:
void write_raw_file(int outfd, int infd);

