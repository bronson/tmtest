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


/** Prints the command section of the test suitable for how the test is being run.
 *
 * If the user is just running the test, nothing is printed.  If the
 * user is diffing or dumping the test, however, the modified command
 * section needs to be printed to the appropriate command.
 */

void rewrite_command_section(struct test *test, int tok, const char *ptr, int len)
{
    // only dump if we're asked to.
    if(test->rewritefd < 0) {
        return;
    }

    // for now we don't modify it at all.
    write(test->rewritefd, ptr, len);
}


/** Copies the command section of the test to the given fileptr and
 * also supplies it to the dump_command_section() routine.
 *
 * If you don't want to dump to a fileptr (i.e. if you're running
 * the test from a file) just pass NULL for fp.
 *
 * This routine is a whole lot like scan_sections except that it stops
 * at the end of the command section.  It leaves the result sections
 * on the stream to be parsed later.
 */

void test_command_copy(struct test *test, FILE *fp)
{
    int oldline;

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
            // now we attempt to push the token back on the stream...
            scan_pushback(&test->testfile);
            test->testfile.line = oldline;
            // The pushback reset the stream, and I restored the line number,
            // but the scanner is still in a different state.
            // We need it to be in a COMMAND state, so that when it feeds
            // the new SECTION token it marks it NEW.  Reattaching resets
            // the state to a command state, so we can just do that.
            tfscan_attach(&test->testfile);
            break;
        }

        // print the modified data to the output stream.
        rewrite_command_section(test, tokno, token_start(&test->testfile), token_length(&test->testfile));

        if(fp) {
            // print the unmodified data to the command script.
            fwrite(token_start(&test->testfile), token_length(&test->testfile), 1, fp);
        }
    } while(!scan_finished(&test->testfile));

    rewrite_command_section(test, 0, NULL, 0);
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
    diffs |= (test->stdout_match != match_yes ? 0x01 : 0);
    diffs |= (test->stderr_match != match_yes ? 0x02 : 0);
    diffs |= (test->exitno_match != match_yes ? 0x04 : 0);

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


static void write_exit_no(int fd, int exitno)
{
    char buf[512];
    int cnt;

    cnt = snprintf(buf, sizeof(buf), "RESULT : %d\n", exitno);
    write(fd, buf, cnt);
}


static void write_file(int outfd, const char *name, int infd)
{
    char buf[BUFSIZ];
    int cnt;

    // first rewind the input file
    if(lseek(infd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "write_file lseek on %d: %s\n", infd, strerror(errno));
        exit(10);   // todo: consolidate with error codes in main
    }

    // write the section header
    write(outfd, name, strlen(name));

    // then write the file.
    do {
        do {
            cnt = read(infd, buf, sizeof(buf));
        } while(cnt < 0 && errno == EINTR);
        if(cnt > 0) {
            write(outfd, buf, cnt);
        }
    } while(cnt);
}


void parse_section_output(struct test *test, int sec,
        const char *datap, int len, void *refcon)
{
    assert(sec >= 0);

    switch(sec) {
        case 0:
            // don't need to worry about eof
            break;

        case exSTDOUT|exNEW:
            test->stdout_match = match_yes;
            write_file(test->rewritefd, "STDOUT : \n", test->outfd);
            break;
        case exSTDOUT:
            // ignore all data in the expected stdout.
            break;

        case exSTDERR|exNEW:
            test->stderr_match = match_yes;
            write_file(test->rewritefd, "STDERR : \n", test->errfd);
        case exSTDERR:
            // ignore all data in the expected stderr
            break;

        case exRESULT|exNEW:
            test->exitno_match = match_yes;
            write_exit_no(test->rewritefd, test->exitno);
            break;

        case exRESULT:
            // allow random garbage in result section to pass
        case exMODIFY|exNEW:
        case exMODIFY:
            // leave modify sections unchanged.
        default:
            write(test->rewritefd, datap, len);
    }
}


/** Prints the actual result sections in the same order as they
 * appear in the testfile.
 */

void dump_results(struct test *test)
{
    // The command section has already been dumped.  We just
    // need to dump the result sections.  The trick is, though,
    // that we need to dump them in the same order as they occur
    // in the testfile otherwise the diff will be all screwed up.

    test->exitno_match = match_unknown;
    test->stdout_match = match_unknown;
    test->stderr_match = match_unknown;

    scan_sections(test, &test->testfile, parse_section_output, NULL);

    if(test->exitno_match == match_unknown && test->exitno != 0) {
        write_exit_no(test->rewritefd, test->exitno);
    }
    if(test->stderr_match == match_unknown && fd_has_data(test->errfd)) {
        write_file(test->rewritefd, "STDERR : \n", test->errfd);
    }
    if(test->stdout_match == match_unknown && fd_has_data(test->outfd)) {
        write_file(test->rewritefd, "STDOUT : \n", test->outfd);
    }
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
    test_runs++;
    memset(test, 0, sizeof(struct test));
    test->rewritefd = -1;
}


void test_free(struct test *test)
{
}


