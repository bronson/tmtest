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
#include "r2scan-dyn.h"
#include "status.h"
#include "tfscan.h"
#include "compare.h"


static int test_runs = 0;
static int test_successes = 0;
static int test_failures = 0;



const char* get_test_filename(struct test *test)
{
    if(test->testfilename[0] == '-' && test->testfilename[1] == '\0') {
        return "(STDIN)";
    }

    return test->testfilename;
}


/** Tells if the given file descriptor has a nonzero length.
 * NOTE: it changes the file offset to the end of the file.
 *
 * Returns nonzero if file has data, zero if it doesn't.
 * Actually, it just returns the file's length.
 */

int fd_has_data(int fd)
{
    off_t pos = lseek(fd, 0, SEEK_END);
    if(pos < 0) {
        perror("lseek in fd_has_data");
        exit(10);   // todo: consolidate with error codes in main
    }

    return pos;
}


/** Returns true if the test was started or false if it bailed during
 * configuration.
 *
 * If false is returned, it also returns a litte message as to why.
 *
 * If an error in a configuration file causes the test shell to exit,
 * we want to stop here.  There's no point in processing the results.
 *
 * HOW IT WORKS:
 * It examines the status file for a line, "^running: ".
 * If found, then it returns true.  If not found, then it
 * generates a message describing what the last config file
 * to run was, or where else in the test process we bailed.
 */

int test_ran(struct test *test, char *msgbuf, int msgbufsiz)
{
    char buf[BUFSIZ];
    scanstate ss;

    int tok;

    // first rewind the status file
    if(lseek(test->statusfd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "read_file lseek for status file: %s\n", strerror(errno));
        exit(10);   // todo: consolidate with error codes in main
    }

    // then create our scanner
    scanstate_init(&ss, buf, sizeof(buf));
    readfd_attach(&ss, test->statusfd);
    cb_scanner_attach(&ss);

    // now, if we see the token "CBRUNNING" in the token stream,
    // it means that we attempted to start the test.  If not,
    // then the test bailed early.
    do {
        tok = scan_token(&ss);
        if(tok < 0) {
            fprintf(stderr, "Error %d pulling status tokens: %s\n", 
                    tok, strerror(errno));
            exit(10);
        }
        if(tok == CBRUNNING) {
            // it's ok to bail early since there's no memory to
            // deallocate and no files that need closing.
            return 1;
        }
    } while(!scan_finished(&ss));

    // should add a short message to the user saying what happened.
    strncpy(msgbuf, "todo: add a message here.", msgbufsiz);

    return 0;
}


/** Copies the command section of the given text to the given fileptr.
 *
 * It also saves all data in a memory block.  We will probably need
 * this data again when we want to output the test file.  Luckily
 * the command section is usually tiny.  It's the output sections
 * that get lengthly.
 *
 * This routine is a whole lot like scan_sections except that it stops
 * at the end of the command section.  It leaves the result sections
 * on the stream to be parsed later.
 */

void test_command_copy(struct test *test, FILE *fp)
{
    int oldline;

    test->cmdsection = memfile_alloc(512);

    do {
        oldline = test->testfile.line;
        int tokno = scan_token(&test->testfile);
        if(tokno < 0) {
            fprintf(stderr, "Error %d pulling status tokens: %s\n", 
                    tokno, strerror(errno));
            exit(10);
        }

        // I don't think the scanner will return a 0 token anymore.
        // Let's check that out.  Need to know for the parser.
        assert(tokno);

        if(tokno != exCOMMAND) {
            // this is the start of another section.  we're done.
            memfile_freeze(test->cmdsection);

            // now we attempt to push the token back on the stream...
            scan_pushback(&test->testfile);
            test->testfile.line = oldline;
            // The pushback reset the stream, and I restored the line number,
            // but the scanner is still in a different state.
            // We need it to be in a COMMAND state, so that when it feeds
            // the new SECTION token it marks it NEW.  Reattaching resets
            // the state to a command state, so we can just do that.
            tfscan_attach(&test->testfile);
            return;
        }

        fwrite(token_start(&test->testfile), token_length(&test->testfile), 1, fp);
        memfile_write(test->cmdsection, token_start(&test->testfile), token_length(&test->testfile));
    } while(!scan_finished(&test->testfile));

    // the scan finished without hitting another section.
    // no problem -- that just means this test doesn't HAVE
    // any other sections.
}


/** Prepares a test section for comparison against actual results.
 *
 * The comparison is handled by compare.c/h.  We just need to set
 * it up.
 */

void compare_section_start(scanstate *cmpscan, int fd, matchval *mv,
        const char *filename, const char *sectionname)
{
    assert(!compare_in_progress(cmpscan));

    // associates the scanstate with the fd and prepares to compare.
    if(*mv != match_unknown) {
        fprintf(stderr, "'%s' has multiple %s sections!\n",
                filename, sectionname);
        exit(10);
    }

    // rewind the file
    if(lseek(fd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "read_file lseek for status file: %s\n", strerror(errno));
        exit(10);   // todo: consolidate with error codes in main
    }

    scanstate_reset(cmpscan);
    readfd_attach(cmpscan, fd);
    compare_attach(cmpscan, mv);

    // we may want to check the token to see if there are any
    // special requests (like detabbing).
}


/** This routine parses the tokens returned by scan_sections() and
 * compares them against the actual test results.  It stores the
 * results in test->match_stdout, match_stderr, and match_result.
 *
 * The refcon needs to be an allocated scanner.  It need not be
 * attached to anything -- this routine will take care of attaching
 * and detaching it as needed.
 */

void parse_section_compare(struct test *test, int sec,
        const char *datap, int len, void *refcon)
{
    #define get_cur_state(ss)    ((int)(ss)->userref)
    #define set_cur_state(ss,x)  ((ss)->userref=(void*)(x))

    scanstate *cmpscan = refcon;
    int newsec = EX_TOKEN(sec);

    if(get_cur_state(cmpscan) == 0) {
        // This is the first time this scanner has been used, so our
        // current state has not been set yet.  Initialize it to the
        // command state.
        set_cur_state(cmpscan, exCOMMAND);
    }

    if(!is_section_token(newsec) && sec != 0) {
        // We only work with result section tokens.  Ignore all
        // other tokens (except the eof token of course).
        return;
    }

    if(EX_ISNEW(sec) || sec == 0) {
        // ensure previoius section is finished
        switch(get_cur_state(cmpscan)) {
            case exSTDOUT:
            case exSTDERR:
                compare_end(cmpscan);
                break;
            default:
                ;
        }

        // fire up new section
        set_cur_state(cmpscan, newsec);
        switch(get_cur_state(cmpscan)) {
            case exSTDOUT:
                compare_section_start(cmpscan, test->outfd,
                        &test->stdout_match, get_test_filename(test), "STDOUT");
                break;
            case exSTDERR:
                compare_section_start(cmpscan, test->errfd,
                        &test->stderr_match, get_test_filename(test), "STDERR");
                break;
            case exRESULT:
                // TODO parse exit code
                break;
            case exMODIFY:
                // add data to modify clause.
                break;
            case exCOMMAND:
                assert(!"should never start a new garbage!");
                break;
        }
    } else {
        // we're continuing an already started section.
        assert(get_cur_state(cmpscan) == newsec);

        switch(get_cur_state(cmpscan)) {
            case exSTDOUT:
            case exSTDERR:
                compare_continue(cmpscan, datap, len);
                break;
            case exRESULT:
                // ignore any lines after the start of a RESULT section.
                // todo: might want to print a warning if any non-whitespace
                // characters are found...
                break;
            case exMODIFY:
                // add line to modify clause.
                break;
            case exCOMMAND:
                break;
        }
    }
}


/** Scans the output sections of the test and calls the supplied parser
 * for each token.
 *
 * @param scanner: used to provide the section tokens.
 */
void scan_sections(struct test *test, scanstate *scanner,
        void (*parseproc)(struct test *test, int sec, const char *datap,
                int len, void *refcon), void *refcon)
{
    // if the testfile is already at its eof, it means that
    // it didn't have any sections.  therefore, we'll assume
    // defaults for all values.  we're done.
    if(scan_finished(scanner)) {
        return;
    }
    
    do {
        int tokno = scan_token(scanner);
        if(tokno < 0) {
            fprintf(stderr, "Error %d pulling status tokens: %s\n", 
                    tokno, strerror(errno));
            exit(10);
        }

        // I don't think the scanner will return a 0 token anymore.
        // Let's check that out.  Need to know for the parser.
        assert(tokno);

        (*parseproc)(test, tokno, token_start(scanner),
                token_length(scanner), refcon);

    } while(!scan_finished(scanner));

    // give the parser an eof token so it can finalize things.
    (*parseproc)(test, 0, NULL, 0, refcon);
}


/** Checks the actual results against the expected results.
 */

void test_results(struct test *test)
{
    scanstate *ref;
    char buf[120];
    int diffs;

    if(!test_ran(test, buf, sizeof(buf))) {
        printf("ERR  %s: %s\n", get_test_filename(test), buf);
        test_failures++;
        return;
    }

    test->exitno_match = match_unknown;
    test->stdout_match = match_unknown;
    test->stderr_match = match_unknown;

    ref = dynscan_create(BUFSIZ);
    if(!ref) {
        perror("Could not allocate section compare scanner");
        exit(10);
    }
    scan_sections(test, &test->testfile, parse_section_compare, ref);
    dynscan_free(ref);

    assert(test->stdout_match != match_inprogress);
    assert(test->stderr_match != match_inprogress);

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
        printf("ok   %s\n", get_test_filename(test));
    } else {
        test_failures++;
        // TODO: improve the failure message
        printf("FAIL %s: diffs=%d\n", get_test_filename(test), diffs);
    }

    return;
}


/*
void modify_cmdsection(struct test *test, int fd)
{
    if(test->cmdsection) {
        // this test's command section has already been printed
        // (probably because the user fed the test script over stdin).
        
    }

    // now scan the rest of the testfile.
    cb_scanner_attach(&ss);

    // now, if we see the token "CBRUNNING" in the token stream,
    // it means that we attempted to start the test.  If not,
    // then the test bailed early.
    do {
        tok = scan_token(&ss);
        if(tok < 0) {
            fprintf(stderr, "Error %d pulling status tokens: %s\n", 
                    tok, strerror(errno));
            exit(10);
        }
        if(tok == CBRUNNING) {
            // it's ok to bail early since there's no memory to
            // deallocate and no files that need closing.
            return 1;
        }
    } while(!scan_finished(&ss));

    // should add a short message to the user saying what happened.
    strncpy(msgbuf, "todo: add a message here.", msgbufsiz);
}
*/


void dump_results(struct test *test)
{
    /*
    print_command_section();

    test->exitno_match = match_unknown;
    test->stdout_match = match_unknown;
    test->stderr_match = match_unknown;

    scan_sections(test, parse_section_output);
    */

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
    printf("%d failure%s.\n", test_failures, (test_failures != 1 ? "s" : ""));
}


void test_init(struct test *test)
{
    memset(test, 0, sizeof(struct test));
}


void test_free(struct test *test)
{
    if(test->cmdsection) {
        memfile_free(test->cmdsection);
    }
}


