/* test.h
 * Scott Bronson
 * 29 Dec 2004
 *
 * All the data needed to run a test and print the results.
 * This file is covered by the MIT License.
 */

#include "compare.h"
#include <setjmp.h>


/**
 * a tristate that tells whether something
 *    - matches
 *    - doesn't match
 *    - hasn't been checked yet.
 */

enum matchval {
    match_inprogress = -2,
    match_unknown = -1,
    match_no = 0,
    match_yes = 1,
};


typedef enum {
    test_pending=0,      ///< still processing config files
    config_was_aborted,  ///< some config file called ABORT
    config_was_disabled, ///< some config file called DISABLED

    test_was_started=16, ///< test was started but we haven't received an exit status yet.
    test_was_completed,  ///< test completed normally.  tests may abort prematurely but still consider it a successful run, so use test_was_started.  this status is largely useless.
    test_was_aborted,    ///< somebody called abort in the middle of the test
    test_was_disabled,   ///< the test was disabled by somebody.
    test_has_failed,     ///< the test has already been marked a failure
} test_status;


#define was_started(st)  ((st) >= test_was_started)
#define was_aborted(st)  ((st) == config_was_aborted || (st) == test_was_aborted)
#define was_disabled(st) ((st) == config_was_disabled || (st) == test_was_disabled)


// flags for how test output should be printed
extern int quiet;
extern int verbose;


// if set then read this config file before scanning through directories
extern char *config_file;

// all strings are malloc'd and need to be freed when the test is finished.

struct test {
    const char *testfile;       ///< relative or absolute path to the testfile
    const char *testpath;       ///< always an absolute path to the testfile
    scanstate testscanner;      ///< scans the testfile.  may be stdin so seeking is not allowed.

    int rewritefd;              ///< where to dump the rewritten test.  -1 if we're just running the tests, or the fd of the file that should receive the test contents.

    int outfd;                  ///< the file that receives the test's stdout.
    int errfd;                  ///< the file that receives the test's stderr.
    int statusfd;               ///< receives the runtime test status messages.
    int exitno;                 ///< the testfile exited with this value
    int exitsignal;             ///< the value returned for the test by waitpid(2)
    int exitcored;              ///< if exitsignal is true, true if child core dumped.


    char *diffname;             ///< if we're diffing against stdin, this contains the name of the required tempfile.
    int diff_fd;                ///< if diffname is set, then this is the fd of the tempfile we're using to store stdin.

    test_status status;         ///< Tells what happened with the test.
    char *status_reason;        ///< If the test was aborted or disabled, and the user gave a reason why, that reason is stored here.  Allocated dynamically -- free it when done.

    int num_config_files;       ///< the number of config files we started processing.  If the status is higher than test_was_started, then this gives the total number of config files processed.
    char *last_file_processed;  ///< if it could be discovered, this contains the name of the last file to be started.  must be freed.

    enum matchval stdout_match; ///< tells whether the expected and actual stdout matches.
    enum matchval stderr_match; ///< tells whether the expected and actual stderr matches.

    jmp_buf abort_jump;
};


void scan_status_file(struct test *test);
void test_command_copy(struct test *test, FILE *fp);

void test_results(struct test *test);
void dump_results(struct test *test);
void print_test_summary(struct timeval *start, struct timeval *stop);
int check_for_failure(struct test *test, const char *testpath);
int test_get_exit_value();

void test_init(struct test *test);
void test_free(struct test *test);
void test_abort(struct test *test, const char *fmt, ...);


// random utility function for start_diff.  Return value is true if the
// file ends in a newline, false if not.
size_t write_file(struct test *test, int outfd, int infd, int *ending_nl);

const char *convert_testfile_name(const char *fn);
