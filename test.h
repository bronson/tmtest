/* test.h
 * Scott Bronson
 * 29 Dec 2004
 *
 * All the data needed to run a test and print the results.
 */


// all strings are malloc'd and need to be freed when the test is finished.

struct test {
    const char *filename;

    int outfd;
    int errfd;
    int statusfd;
};

