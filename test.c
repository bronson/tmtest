/* test.c
 * 30 Dec 2004
 * Copyright (C) 2004 Scott Bronson
 * This entire file is covered by the GNU GPL V2.
 * 
 * Contains the routines to check/diff/etc test output.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "test.h"
#include "r2read-fd.h"
#include "scan.h"

static int test_runs = 0;
static int test_successes = 0;
static int test_failures = 0;


/** Tells if the given file descriptor has a nonzero length.
 * NOTE: it changes the file offset to the end of the file.
 */

int fd_has_data(int fd)
{
    off_t pos = lseek(fd, 0, SEEK_END);
    if(pos < 0) {
        perror("lseek in fd_has_data");
        exit(10);   // todo: consolidate with error codes in main
    }
    return pos ? 1 : 0;
}


/** Returns true if the test was started, or false if we bailed before.
 *
 * If a configuration file causes the test shell to exit, we may never
 * even enter the testfile.  If this is the case, then we don't want
 * to be diffing against nonexistent results.
 *
 * It examines the status file for a line, "^running: ".
 * If found, then it returns true.  If not found, then it
 * generates a message describing what the last config file
 * to run was.  If no config file bailed, it trys to say
 * where in the test process the error occurred.
 */

int config_bailed(struct test *test, char *msg, int msglen)
{
    // first rewind the status file
    if(lseek(test->statusfd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "read_file lseek for status file: %s\n", strerror(errno));
        exit(10);   // todo: consolidate with error codes in main
    }

    // now, if we see the token "CBRUNNING" in the status stream,
    // it means that we attempted to start the test.


    return 0;
}


/* The callback called every time the parser encounters a section.
 *
 * @param section: a number matching test->outfd, errfd, or statusfd
 * depending on whether this section is stdout, stderr, or result.
 * @param pos: the location relative to the start of the section
 * of this data.
 * @param datap: the data for this portion of the section.
 * @param len: the number of bytes of data found in cp.
 * if len is 0 then it means that the previous chunk of data
 * was the final chunk in that section.
 */

typedef void (*sectionproc)(struct test *test, int section,
        int pos, char *datap, int len);


/** Parses the test file, looking for stdout, stderr, and exitval.
 *
 * The section proc will be called each time a portion of a section
 * is encountered.
 *
 */

void parse_test(struct test *test, sectionproc proc)
{
}


/** Checks the expected sections against the actual
 * and stores the results in test->match_stdout, etc.
 */

void test_parse_section(struct test *test, int section,
        int pos, char *datap, int len)
{
}


/** Checks the actual results against the expected results.
 */

void test_results(struct test *test)
{
    char msg[100];
    int diffs;

    if(config_bailed(test, msg, sizeof(msg))) {
        printf("ERR  %s: %s\n", test->filename, msg);
        test_failures++;
        return;
    }

    test->exitno_match = match_unknown;
    test->stdout_match = match_unknown;
    test->stderr_match = match_unknown;

    parse_test(test, test_parse_section);

    // convert any unknowns into a solid yes/no
    if(test->exitno_match == match_unknown) {
        test->exitno_match = (test->exitno == 0 ? match_yes : match_no);
    }
    if(test->stdout_match == match_unknown) {
        test->stdout_match = (fd_has_data(test->outfd) ? match_no : match_yes);
    }
    if(test->stderr_match == match_unknown) {
        test->stderr_match = (fd_has_data(test->errfd) ? match_no : match_yes);
    }

    diffs = 0;
    diffs |= (test->stdout_match ? 0 : 0x01);
    diffs |= (test->stderr_match ? 0 : 0x02);
    diffs |= (test->exitno_match ? 0 : 0x04);

    if(diffs == 0) {
        test_successes++;
        printf("ok   %s\n", test->filename);
    } else {
        test_failures++;
        // TODO: improve the failure message
        printf("FAIL %s: diffs=%d\n", test->filename, diffs);
    }

    return;
}


void dump_results(struct test *test)
{
    //     we have a routine that will generate a new test file based on the old.
    //       output: it just dumps that new test file to stdout.
}


void diff_results(struct test *test)
{
    //       diff: it pipes that new file to a forked diff process
    //          need to cd to orig dir before running diff so it has
    //          the correct path to the file.  otherwise patch won't run.
}


void inc_test_runs(struct test *test)
{
    test_runs++;
}


void print_test_summary()
{
    printf("\n");
    printf("%d test%s run, ", test_runs, (test_runs != 1 ? "s" : ""));
    printf("%d success%s, ", test_successes, (test_successes != 1 ? "es" : ""));
    printf("%d failure%s, ", test_failures, (test_failures != 1 ? "s" : ""));
    printf(".\n");
}

