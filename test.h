/* test.h
 * Scott Bronson
 * 29 Dec 2004
 *
 * All the data needed to run a test and print the results.
 */


struct test {
    // all of these need to be freed when the test is finished.
    const char *filename;
    const char *outfile;
    const char *errfile;
    const char *oobfile;
    const char *oobsep;
};

