/* test.h
 * Scott Bronson
 * 29 Dec 2004
 *
 * All the data needed to run a test and print the results.
 */


// all strings are malloc'd and need to be freed when the test is finished.

typedef enum {
    match_unknown = -1,
    match_no = 0,
    match_yes = 1,
} matchval;


struct test {
    const char *filename;

    int outfd;
    int errfd;
    int statusfd;

    int exitno;

    matchval exitno_match;
    matchval stdout_match;
    matchval stderr_match;
};

void test_results(struct test *test);
void dump_results(struct test *test);
void diff_results(struct test *test);

void inc_test_runs(struct test *test);
void print_test_summary();

