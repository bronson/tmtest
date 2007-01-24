/* test.c
 * 30 Dec 2004
 * Copyright (C) 2004 Scott Bronson
 * 
 * Contains the routines to check/diff/etc test output.
 *
 * This file is covered by the MIT license.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <assert.h>
#include <stdarg.h>

#include "re2c/read-fd.h"

#include "test.h"
#include "stscan.h"
#include "tfscan.h"
#include "rusage.h"


// utility function so you can say i.e. write_strconst(fd, "/");
#define write_strconst(fd, str) write((fd), (str), sizeof(str)-1)

static int test_runs = 0;
static int test_successes = 0;
static int test_failures = 0;


const char *convert_testfile_name(const char *fn)
{
    if(fn[0] == '-' && fn[1] == '\0') {
        return "(STDIN)";
    }

	return fn;
}


const char* get_testfile_name(struct test *test)
{
    return convert_testfile_name(test->testfilename);
}


/** Tells if the given file descriptor has a nonzero length.
 * NOTE: it changes the file offset to the end of the file.
 *
 * Returns nonzero if file has data, zero if it doesn't.
 * Actually, it just returns the file's length.
 */

int fd_has_data(struct test *test, int fd)
{
    off_t pos = lseek(fd, 0, SEEK_END);
    if(pos < 0) {
		test_abort(test, "fd_has_data lseek error: %s", strerror(errno));
    }

    return pos;
}


/** Tries to find the argument in the status line given.
 *
 * @return  nonzero if the argument could be found, zero if not.
 * If the argument was found, then incp and ince are updated to
 * point to its beginning and end.
 */

static int locate_status_arg(const char **incp, const char **ince)
{
	const char *cp = *incp;
	const char *ce = *ince;

	// trim the newline from the end
	if(ce[-1] == '\n') ce--;

	// skip to colon separating name from arg
	while(*cp != ':' && *cp != '\n' && cp < ce) cp++;
	if(*cp == ':') {
		cp++;					// skip the colon
		if(*cp == ' ') cp++;	// skip the optional space after it
		if(cp < ce) {
			*incp = cp;
			*ince = ce;
			return 1;
		}
	}

	return 0;
}


static char* dup_status_arg(const char *cp, const char *ce)
{
	char *ret = NULL;

	if(locate_status_arg(&cp, &ce)) {
		ret = malloc(ce - cp + 1);
		if(ret) {
			memcpy(ret, cp, ce-cp);
			// replace the NL on the end with the null terminator.
			ret[ce-cp] = '\0';
		}
	}

	return ret;
}


static int copy_status_arg(const char *cp, const char *ce, char *buf, int size)
{
	if(locate_status_arg(&cp, &ce)) {
		int len = ce - cp;
		if(size-1 < len) len = size-1;
		memcpy(buf, cp, len);
		buf[len] = '\0';
		return 1;
	}

	return 0;
}


/** Looks through the status file and stores the items of interest
 * in the test structure.
 */

void scan_status_file(struct test *test)
{
	char lastfile[PATH_MAX];
	int lastfile_good = 0;
    char buf[BUFSIZ];
    scanstate ss;
    int tok;
	int state = 0;

    // first rewind the status file
    if(lseek(test->statusfd, 0, SEEK_SET) < 0) {
        test_abort(test, "read_file lseek on status file: %s\n",
			strerror(errno));
    }

    // then create our scanner
    scanstate_init(&ss, buf, sizeof(buf));
    readfd_attach(&ss, test->statusfd);
    stscan_attach(&ss);

    // now, if we see the token "CBRUNNING" in the token stream,
    // it means that we attempted to start the test.  If not,
    // then the test bailed early.
    do {
        tok = scan_next_token(&ss);

		// look for errors...
        if(tok < 0) {
            test_abort(test, "Error %d pulling status tokens: %s\n",
				tok, strerror(errno));
        } else if(tok == stGARBAGE) {
			fprintf(stderr, "Garbage on line %d in the status file: '%.*s'\n",
					ss.line, (int)token_length(&ss)-1, token_start(&ss));
		} else {
			state = tok;
		}

		switch(tok) {
			case stSTART:
				// nothing to do
				break;

			case stCONFIG:
				if(test->status == test_pending) {
					test->num_config_files += 1;
					if(copy_status_arg(token_start(&ss), token_end(&ss), lastfile, sizeof(lastfile))) {
						lastfile_good = 1;
					} else {
						fprintf(stderr, "CONFIG needs arg on line %d of the status file: '%.*s'\n",
								ss.line, (int)token_length(&ss)-1, token_start(&ss));
					}
				} else {
					fprintf(stderr, "CONFIG but status (%d) wasn't pending on line %d of the status file: '%.*s'\n",
							test->status, ss.line, (int)token_length(&ss)-1, token_start(&ss));
				}
				break;

			case stPREPARE:
				// nothing to do
				break;

			case stRUNNING:
				if(test->status == test_pending) {
					test->status = test_was_started;
					if(copy_status_arg(token_start(&ss), token_end(&ss), lastfile, sizeof(lastfile))) {
						lastfile_good = 1;
					} else {
						fprintf(stderr, "RUNNING needs arg on line %d of the status file: '%.*s'\n",
								ss.line, (int)token_length(&ss)-1, token_start(&ss));
					}
				} else {
					fprintf(stderr, "RUNNING but status (%d) wasn't pending on line %d of the status file: '%.*s'\n",
							test->status, ss.line, (int)token_length(&ss)-1, token_start(&ss));
				}
				break;

			case stDONE:
				if(test->status == test_was_started) {
					test->status = test_was_completed;
				} else {
					fprintf(stderr, "DONE but status (%d) wasn't RUNNING on line %d of the status file: '%.*s'\n",
							test->status, ss.line, (int)token_length(&ss)-1, token_start(&ss));
				}
				break;
			
			case stABORTED:
				test->status = (test->status >= test_was_started ? test_was_aborted : config_was_aborted);
				test->status_reason = dup_status_arg(token_start(&ss), token_end(&ss));
				break;

			case stDISABLED:
				test->status = (test->status >= test_was_started ? test_was_disabled : config_was_disabled);
				test->status_reason = dup_status_arg(token_start(&ss), token_end(&ss));
				break;

			default:
				fprintf(stderr, "Unknown token (%d) on line %d of the status file: '%.*s'\n",
						tok, ss.line, (int)token_length(&ss)-1, token_start(&ss));
		}
    } while(!scan_is_finished(&ss));

	if(lastfile_good) {
		test->last_file_processed = strdup(lastfile);
	}
}


/**
 * Prints the command section of the test suitable for how the test
 * is being run.
 *
 * If the user is just running the test, nothing is printed.  If the
 * user is diffing or dumping the test, however, the modified command
 * section needs to be printed to the appropriate command.
 *
 * @param test The test being run.
 * @param tok The type of data this is (from tfscan.h).  If 0 then 
 *            this is the EOF and this routine won't be called anymore.
 * @param ptr The data to write.  If tok==0 then ptr is undefined.
 * @param len The amount of data to write.  If tok==0 then len==0.
 *
 * Hm, a year later it ooks like rewriting is a feature that will
 * never need to be implemented...?
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
        int tokno = scan_next_token(&test->testfile);
        if(tokno < 0) {
            test_abort(test, "Error %d pulling status tokens: %s\n", 
                    tokno, strerror(errno));
        } else if(tokno == 0) {
			// if the test file is totally empty.
			break;
		}


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
            // Now we're done dumping the command and the scanner
            // is poised to return the correct section start to the
            // next client.
            break;
        }

        // print the modified data to the output stream.
        rewrite_command_section(test, tokno, token_start(&test->testfile), token_length(&test->testfile));

        if(fp) {
            // print the unmodified data to the command script.
            fwrite(token_start(&test->testfile), token_length(&test->testfile), 1, fp);
        }
    } while(!scan_is_finished(&test->testfile));

    rewrite_command_section(test, 0, NULL, 0);
}


/** Prepares a test section for comparison against actual results.
 *
 * The comparison is handled by compare.c/h.  We just need to set
 * it up.
 */

void compare_section_start(struct test *test,
	scanstate *cmpscan, int fd,
	const char *sectionname)
{
    // rewind the file
    if(lseek(fd, 0, SEEK_SET) < 0) {
        test_abort(test, "compare_section_start lseek compare: %s\n",
			strerror(errno));
    }

    scanstate_reset(cmpscan);
    readfd_attach(cmpscan, fd);
	compare_attach(cmpscan);

    // we may want to check the token to see if there are any
    // special requests (like detabbing).
}


/** Increments cp past the section name.
 *
 * Will not increment cp by more than len bytes.
 * This routine must match the token parsing found in the tfscan_start routine.
 */

const char *skip_section_name(const char *cp, int len)
{
	const char *ce = cp + len;

	// skip to colon separating section name from data
	while(*cp != ':' && *cp != '\n' && cp < ce) cp++;
	if(*cp == ':') cp++;
	return cp;
}



/**
 * Calls the given callback routine for each argument found.
 *
 * For now, we just split on whitespace.  In the future, if needed,
 * this routine could be modified to handle \, ", ', etc just like bash.
 */

int parse_section_args(const char *tok, int toklen, const char *file, int line,
        int (*argproc)(int index, const char *buf, const char *end, 
            const char *file, int line, void *refcon),
        void *refcon)
{
    const char *end = tok + toklen;
    const char *cb, *ce;
    int index = 0;
    int val = 0;

    ce = tok;
    while(ce < end) {
        cb = ce;
        while(!isspace(*ce) && ce < end) {
            ce++;
        }

        val = (*argproc)(index, cb, ce, file, line, refcon);
        if(val) {
            break;
        }
        index += 1;

        while(isspace(*ce) && ce < end) {
            ce++;
        }
    }

    return val;
}


int constreq(const char *cp, const char *ce, const char *str)
{
    int len = strlen(str);

    if(ce - cp != len) {
        return 0;
    }

    if(memcmp(cp, str, len) != 0) {
        return 0;
    }

    return 1;
}


int start_output_section_argproc(int i, const char *cp, const char *ce,
        const char *file, int line, void *refcon)
{
    if(i == 0) {
        // index == 0 is the name of this section
        return 0;
    }

    // trim colons from arguments...  they can appear anywhere you want.
    while(*cp == ':' && cp < ce) {
        cp++;
    }
    while(ce[-1] == ':' && ce > cp) {
        ce--;
    }

    if(constreq(cp,ce,"-n") || constreq(cp,ce,"--no-trailing-newline")) {
        *(int*)refcon = 1;
    } else if(cp < ce) {
        fprintf(stderr, "%s line %d: unknown arguments \"%.*s\"\n",
                file, line, (int)(ce-cp), cp);
    }

    return 0;
}


// store the state we need directly in the cmpscan structure.
#define cmpscan_state             (*(int*)&(cmpscan)->userref)
#define cmpscan_suppress_newline  (*(int*)&(cmpscan)->userproc)
	///< 0 for normal processing, 1 if a newline should be suppressed
	///  from the expected output (so it can match actual).


/**
 * Called when we're at the start of a STDOUT or STDERR section.
 * Sets the cmpscanner up to compare the section.
 * See end_output_section().
 */

int start_output_section(struct test *test, const char *tok,
        int toklen, scanstate *cmpscan, int fd, enum matchval val,
        const char *secname)
{
    int suppress_trailing_newline = 0;

    parse_section_args(tok, toklen,
            get_testfile_name(test), test->testfile.line,
            start_output_section_argproc, 
            (void*)&suppress_trailing_newline);

    if(val != match_unknown) {
        // we've already obtained a value for this section!
        fprintf(stderr, "%s line %d Error: duplicate %s "
                "section.  Ignored.\n", get_testfile_name(test),
                test->testfile.line, secname);
        return 0;
    }

	scanstate_reset(cmpscan);
    compare_section_start(test, cmpscan, fd, secname);

	// store the newline flag in the cmpscan structure
	cmpscan_suppress_newline = suppress_trailing_newline;

    return 1;
}


/** 
 * If the actual test results were found to not end in a newline,
 * but the expected results were marked in the testfile as expecting
 * a newline, this function prints the warning.
 */

void warn_section_newline(struct test *test, const char *name)
{
	fprintf(stderr, "WARNING: %s didn't end with a newline!\n"
			"   Add a -n to %s line %d if this is the expected behavior.\n",
			name, get_testfile_name(test), test->testfile.line);
}


/**
 * Finishes comparing a section.
 * see start_output_section().
 *
 * When should we warn?
 * - If the actual stdout didn't end with a \n but the exptected stdout
 *   said it would.  Actual is ss, expected is ptr.  So that means
 *   compare_ptr_has_extra_nl is true AND suppress_trailing_newline
 *   is false.
 *
 * Holy cats.  The -n option has made this routine really complex!
 */

enum matchval end_output_section(struct test *test, scanstate *cmpscan,
        const char *name)
{
	compare_result cmp = compare_check_newlines(cmpscan);
	int suppress_trailing_newline = cmpscan_suppress_newline;

	if(cmp == cmp_ptr_has_extra_nl) {
		// actual is missing a single newline as compared to expected.

		if(!suppress_trailing_newline) {
			// user hasn't marked section needing suppression so warn.
			warn_section_newline(test, name);
			return match_no;
		}

		// it's met all the requirements.  We have a match.
		return match_yes;
	}

	if(cmp == cmp_ptr_has_more_nls && suppress_trailing_newline) {
		fprintf(stderr,
			"WARNING: %s is marked -n but it ends with multiple newlines!\n"
			"    Please remove all but one newline from %s around line %d.\n",
			name, get_testfile_name(test), test->testfile.line);
		return match_no;
	}

	if(cmp == cmp_full_match) {
		if(suppress_trailing_newline) {
			if(cmpscan->line == 0) {
				// don't want to print the warning if it's an empty section
				// because, while it's weird, it's technically correct.
				return match_yes;
			}

			// section was marked -n but a newline was present.  No match.
			return match_no;
		}

		// full match in a normal section (not marked -n)
		return match_yes;
	}

	return match_no;
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
    // cmpscan is the scanner used to perform the diff.
    scanstate *cmpscan = refcon;
	int val;

    // the section that we're processing (without the NEW flag attached)
    int newsec = EX_TOKEN(sec);

    // make sure we're not fed an illegal token.
    assert(is_section_token(newsec) || sec == 0);

    // make sure we're not starting from an illegal state.
    assert(is_section_token(cmpscan_state) || cmpscan_state == 0);

    if(EX_ISNEW(sec) || sec == 0) {
        // ensure previoius section is finished
        switch(cmpscan_state) {
            case exSTDOUT:
			  test->stdout_match = end_output_section(test, cmpscan, "STDOUT");
              break;
            case exSTDERR:
			  test->stderr_match = end_output_section(test, cmpscan, "STDERR");
              break;
            default:
                ;
        }

        // then fire up the new section
        cmpscan_state = newsec;
        switch(newsec) {
            case 0:
                // don't start a new section if eof.
                break;
            case exSTDOUT:
                if(!start_output_section(test, datap, len, cmpscan,
                        test->outfd, test->stdout_match, "STDOUT")) {
					// ignore the rest of this section
                    cmpscan_state = 0;
                }
                break;
            case exSTDERR:
                if(!start_output_section(test, datap, len, cmpscan,
                        test->errfd, test->stderr_match, "STDERR")) {
					// ignore the rest of this section
                    cmpscan_state = 0;
                }
                break;
        }
    } else {
        // we're continuing an already started section.
        assert(cmpscan_state == newsec || cmpscan_state == 0);

        switch(cmpscan_state) {
            case 0:
                // do nothing
                break;
            case exSTDOUT:
            case exSTDERR:
				val = compare_continue(cmpscan, datap, len);
				if(val < 0) {
					test_abort(test, "compare_continue error: %d\n", val);
				}
                break;
            case exCOMMAND:
                break;
        }
    }
}


/** Scans the output sections of the test and calls the supplied parser
 * for each token.
 *
 * Tokens are defined by the tfscan_start() routine.  Currently they're
 * full lines.  If the line starts with a recognized section heading,
 *
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
    if(scan_is_finished(scanner)) {
        return;
    }
    
    do {
        int tokno = scan_next_token(scanner);
        if(tokno < 0) {
            test_abort(test, "scan_sections error %d pulling status tokens: %s\n", 
                    tokno, strerror(errno));
        } else if(tokno == 0) {
			break;
		}

        (*parseproc)(test, tokno, token_start(scanner),
                token_length(scanner), refcon);

    } while(!scan_is_finished(scanner));

    // give the parser an eof token so it can finalize things.
    (*parseproc)(test, 0, NULL, 0, refcon);
}


static void print_reason(struct test *test, const char *name, const char *prep)
{
	printf("%s %-25s ", name, get_testfile_name(test));
	if(!was_started(test->status)) {
		printf("%s %s", prep, test->last_file_processed);
		if(test->status_reason) {
			printf(": ");
		}
	}
	if(test->status_reason) {
		printf("%s", test->status_reason);
	}
	printf("\n");
}


static void test_analyze_results(struct test *test, int *stdo, int *stde)
{
    scanstate scanner;
    char scanbuf[BUFSIZ];
	
	*stdo = *stde = -1;

	if(was_aborted(test->status)) {
		test_failures++;
		return;
	}

	if(was_disabled(test->status)) {
		return;
	}

    if(!was_started(test->status)) {
        test_failures++;
        return;
    }

    test->stdout_match = match_unknown;
    test->stderr_match = match_unknown;

    scanstate_init(&scanner, scanbuf, sizeof(scanbuf));
    scan_sections(test, &test->testfile, parse_section_compare, &scanner);

    assert(test->stdout_match != match_inprogress);
    assert(test->stderr_match != match_inprogress);

    if(test->stdout_match == match_unknown) {
        test->stdout_match = (fd_has_data(test, test->outfd) ? match_no : match_yes);
    }
    if(test->stderr_match == match_unknown) {
        test->stderr_match = (fd_has_data(test, test->errfd) ? match_no : match_yes);
    }

    *stdo = (test->stdout_match != match_yes);
    *stde = (test->stderr_match != match_yes);

    if(!*stdo && !*stde && !test->exitsignal) {
        test_successes++;
    } else {
        test_failures++;
	}
}


/** Checks the actual results against the expected results.
 * dispname is the name we should display for the test.
 */

void test_results(struct test *test, const char *dispname)
{
	int stdo, stde;	// true if there are differences.

	test_analyze_results(test, &stdo, &stde);
	
	if(was_aborted(test->status)) {
		print_reason(test, "ABRT", "by");
		return;
	}

	if(was_disabled(test->status)) {
		print_reason(test, "dis ", "by");
		return;
	}

    if(!was_started(test->status)) {
		print_reason(test, "ERR ", "error in");
        return;
    }

    if(!stdo && !stde && !test->exitsignal) {
        printf("ok   %s \n", convert_testfile_name(dispname));
    } else {
        printf("FAIL %-25s ", convert_testfile_name(dispname));
        if(test->exitsignal) {
            printf("terminated by signal %d%s", test->exitsignal,
                    (test->exitcored ? " with core" : ""));
        } else {
            printf("%c%c  ",
                    (stdo ? 'O' : '.'),
                    (stde ? 'E' : '.'));
            if(stdo || stde) {
                if(stdo) printf("stdout ");
                if(stdo && stde) printf("and ");
                if(stde) printf("stderr ");
                printf("differed");
            }
        }
		printf("\n");
    }

    return;
}


/** Like test_results() except that it returns 1 if the test failed
 *  and 0 if it was disabled or succeeded.
 */

int check_for_failure(struct test *test, const char *testpath)
{
	int stdo, stde;	// true if there are differences.
	int oldfailures = test_failures;

	test_analyze_results(test, &stdo, &stde);
	if(oldfailures+1 == test_failures) {
		return 1;
	}

	return 0;
}


/**
 * Reads all the data from infd and writes it onto outfd.
 *
 * @param endnl (optional) true if the data written ended with a newline,
 *   false if not.  Pass NULL if you don't care.
 * @returns the number of bytes written.
 */

size_t write_file(struct test *test, int outfd, int infd, int *endnl)
{
    char buf[BUFSIZ];
    size_t rcnt, wcnt;
	size_t total = 0;

    // first rewind the input file
    if(lseek(infd, 0, SEEK_SET) < 0) {
		test_abort(test, "write_file lseek on %d: %s\n", infd, strerror(errno));
    }

    // then write the file.
    do {
        do {
            rcnt = read(infd, buf, sizeof(buf));
        } while(rcnt < 0 && errno == EINTR);
        if(rcnt > 0) {
			if(endnl) *endnl = (buf[rcnt-1] == '\n');
            do {
                wcnt = write(outfd, buf, rcnt);
            } while(wcnt < 0 && errno == EINTR);
            if(wcnt < 0) {
                test_abort(test, "write_file got %s while writing!",
					strerror(errno));
                break;
            }
			total += rcnt;
        } else if (rcnt < 0) {
			test_abort(test, "write_file got %s while reading!",
				strerror(errno));
            break;
        }
    } while(rcnt);

	return total;
}


static void write_section(struct test *test, const char *datap, int len,
		int fd, const char *name)
{
    int marked_no_nl = 0;
	size_t cnt;
	int has_nl;

	parse_section_args(datap, len,
			get_testfile_name(test), test->testfile.line,
			start_output_section_argproc, &marked_no_nl);

	write(test->rewritefd, datap, len);
	cnt = write_file(test, test->rewritefd, fd, &has_nl);

	if(marked_no_nl) {
		// if a section is marked with --no-trailing-newline, we need
		// to print a newline here so that the testfile isn't messed up.
		// Otherwise, you'd end up with "STDOUT -n:STDERR:" on one line.
		write_strconst(test->rewritefd, "\n");
	} else if(!has_nl) {
		// If the section isn't marked with --no-trailing-newline, but
		// the output DOESN'T have one, we need to print a warning.
		// First, if the user will be viewing the test output, we need
		// to add the final CR so our error message will appear at the
		// start of the line.
		if(test->rewritefd == STDOUT_FILENO) {
			printf("\n");
		}

		if(cnt > 0) {
			warn_section_newline(test, name);
		}
	}
}


/** Writes the actual results in place of the expected results.
 */

void parse_section_output(struct test *test, int sec,
        const char *datap, int len, void *refcon)
{
    assert(sec >= 0);

    switch(sec) {
		case 0:
			// eof!  nothing to do.
			break;

        case exSTDOUT|exNEW:
			write_section(test, datap, len, test->outfd, "STDOUT");
			test->stdout_match = match_yes;
            break;
        case exSTDOUT:
            // ignore all data in the expected stdout.
            break;

        case exSTDERR|exNEW:
			write_section(test, datap, len, test->errfd, "STDERR");
            test->stderr_match = match_yes;
            break;
        case exSTDERR:
            // ignore all data in the expected stderr
            break;

        default:
            write(test->rewritefd, datap, len);
    }
}


static void dump_reason(struct test *test, const char *name)
{
	fprintf(stderr, "ERROR Test %s", name);
	if(!was_started(test->status)) {
		fprintf(stderr, " by %s", convert_testfile_name(test->last_file_processed));
		if(test->status_reason) {
			printf(": ");
		}
	}
	if(test->status_reason) {
		fprintf(stderr, ": %s", test->status_reason);
	}
	fprintf(stderr, "\n");
}


/** Prints the actual result sections in the same order as they
 * appear in the testfile.
 */

void dump_results(struct test *test)
{
    int tempref = 0;

	if(was_aborted(test->status)) {
		dump_reason(test, "was aborted");
		return;
	}

	if(was_disabled(test->status)) {
		dump_reason(test, "is disabled");
		return;

	}

    if(!was_started(test->status)) {
        fprintf(stderr, "Error: %s was not started due to errors in %s.\n",
				get_testfile_name(test), test->last_file_processed);
        test_failures++;
        return;
    }

    // The command section has already been dumped.  We just
    // need to dump the result sections.  The trick is, though,
    // that we need to dump them in the same order as they occur
    // in the testfile otherwise the diff will be all screwed up.

    test->stdout_match = match_unknown;
    test->stderr_match = match_unknown;

    scan_sections(test, &test->testfile, parse_section_output, &tempref);

    // if any sections haven't been output, but they differ from
    // the default, then they need to be output here at the end.
    if(test->stderr_match == match_unknown && fd_has_data(test, test->errfd)) {
		write_strconst(test->rewritefd, "STDERR:\n");
        write_file(test, test->rewritefd, test->errfd, NULL);
    }
    if(test->stdout_match == match_unknown && fd_has_data(test, test->outfd)) {
		write_strconst(test->rewritefd, "STDOUT:\n");
        write_file(test, test->rewritefd, test->outfd, NULL);
    }
}


void print_test_summary(struct timeval *start, struct timeval *stop)
{
    printf("\n");
    printf("%d test%s run, ", test_runs, (test_runs != 1 ? "s" : ""));
    printf("%d success%s, ", test_successes,
			(test_successes != 1 ? "es" : ""));
    printf("%d failure%s", test_failures, (test_failures != 1 ? "s" : ""));

	if(!quiet) {
		printf(", in ");
		print_rusage(start, stop);
	}
	
	printf(".\n");
}


void test_init(struct test *test)
{
    test_runs++;
    memset(test, 0, sizeof(struct test));
    test->rewritefd = -1;
}


void test_abort(struct test *test, const char *fmt, ...)
{
	char buf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	test->status = test_was_aborted;
	test->status_reason = strdup(buf);
	longjmp(test->abort_jump, 1);
}


void test_free(struct test *test)
{
	int err;

	if(test->diffname) {
		err = close(test->diff_fd);
		if(err < 0) {
			fprintf(stderr, "Could not close %s: %s\n", test->diffname, strerror(errno));
		}
		err = unlink(test->diffname);
		if(err < 0) {
			fprintf(stderr, "Could not remove %s: %s\n", test->diffname, strerror(errno));
		}
		free(test->diffname);
	}

	if(test->status_reason) {
		free(test->status_reason);
	}

	if(test->last_file_processed) {
		free(test->last_file_processed);
	}
}


int test_get_exit_value()
{
	return test_failures < 99 ? test_failures : 99;
}

