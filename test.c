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
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

// to get PATH_MAX:
#include <dirent.h>

#include "re2c/read-fd.h"

#include "test.h"
#include "stscan.h"
#include "tfscan.h"
#include "compare.h"


// This is the maximum line length for the eachline substitution.
// Lines longer than this will be parsed as 2 separate lines.
#define MAX_LINE_LENGTH BUFSIZ

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

int fd_has_data(int fd)
{
    off_t pos = lseek(fd, 0, SEEK_END);
    if(pos < 0) {
        perror("lseek in fd_has_data");
        exit(10);   // todo: consolidate with error codes in main
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
        fprintf(stderr, "read_file lseek for status file: %s\n", strerror(errno));
        exit(10);   // todo: consolidate with error codes in main
    }

    // then create our scanner
    scanstate_init(&ss, buf, sizeof(buf));
    readfd_attach(&ss, test->statusfd);
    stscan_attach(&ss);

    // now, if we see the token "CBRUNNING" in the token stream,
    // it means that we attempted to start the test.  If not,
    // then the test bailed early.
    do {
        tok = scan_token(&ss);

		// look for errors...
        if(tok < 0) {
            fprintf(stderr, "Error %d pulling status tokens: %s\n", 
                    tok, strerror(errno));
            exit(10);
        } else if(tok == stGARBAGE) {
			fprintf(stderr, "Garbage on line %d in the status file: '%.*s'\n",
					ss.line, token_length(&ss)-1, token_start(&ss));
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
								ss.line, token_length(&ss)-1, token_start(&ss));
					}
				} else {
					fprintf(stderr, "CONFIG but status (%d) wasn't pending on line %d of the status file: '%.*s'\n",
							test->status, ss.line, token_length(&ss)-1, token_start(&ss));
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
								ss.line, token_length(&ss)-1, token_start(&ss));
					}
				} else {
					fprintf(stderr, "RUNNING but status (%d) wasn't pending on line %d of the status file: '%.*s'\n",
							test->status, ss.line, token_length(&ss)-1, token_start(&ss));
				}
				break;

			case stDONE:
				if(test->status == test_was_started) {
					test->status = test_was_completed;
				} else {
					fprintf(stderr, "DONE but status (%d) wasn't RUNNING on line %d of the status file: '%.*s'\n",
							test->status, ss.line, token_length(&ss)-1, token_start(&ss));
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
						tok, ss.line, token_length(&ss)-1, token_start(&ss));
		}
    } while(!scan_finished(&ss));

	if(lastfile_good) {
		test->last_file_processed = strdup(lastfile);
	}
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

void compare_section_start(scanstate *cmpscan, int fd, pcrs_job *joblist,
		matchval *mv, const char *filename, const char *sectionname)
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
	compare_attach(cmpscan, mv, joblist);

    // we may want to check the token to see if there are any
    // special requests (like detabbing).
}


/** Returns true if the given buffer contains non-whitespace characters,
 * false if the buffer consists entirely of whitespace. */

static int contains_nws(const char *cp, int len)
{
	const char *ce = cp + len;

	while(cp < ce) {
		if(!isspace(*cp)) {
			return 1;
		}
	}

	return 0;
}


/** Scans the given buffer for the exit value.
 *
 * Ignores everything except for the first digit and any digits that
 * follow it.
 *
 * If digits are found, then it updates the test structure with
 * whether the exit values match or not.
 * If no digits are found, then this routine does nothing.
 */

void parse_exit_clause(struct test *test, const char *cp, int len)
{
	const char *ce = cp + len;
	unsigned int num = 0;

	// skip to the first digit in the buffer
	while(!isdigit(*cp) && cp < ce) cp++;
	if(cp >= ce) return;

	// scan the number
	while(isdigit(*cp)) {
		num = 10*num + (*cp - '0');
		cp++;
	}

	test->expected_exitno = num;
	test->exitno_match = (test->exitno == num ? match_yes : match_no);
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


void parse_modify_clause(struct test *test, const char *cp, const char *ce)
{
	pcrs_job *job, **p;
	char *string;
	int err;

	// skip any leading whitespace
	while(isspace(*cp) && cp < ce) cp++;

	// ensure there's still data worth parsing
	if(*cp == '\n' || cp >= ce) {
		return;
	}

	// don't parse it if it's a comment
	if(*cp == '#') {
		return;
	}

	// it's retarded that I can't pass a buf/len combo to pcrs_compile_command.
	string = malloc(ce-cp+1);
	if(!string) {
		perror("malloc in parse_modify_clause");
		exit(10);
	}
	memcpy(string, cp, ce-cp);
	string[ce-cp] = '\0';
	job = pcrs_compile_command(string, &err);
	if(job == NULL) {
        fprintf(stderr, "%s line %d compile error: %s (%d).\n",
                get_testfile_name(test), test->testfile.line,
                pcrs_strerror(err), err);
	}
	memset(string, '1', ce-cp+1);
	free(string);
	if(job == NULL) {
		return;
	}

	// link this job onto the end of the list.
	p = &test->eachline;
	while(*p) p = &(**p).next;
	*p = job;
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
        // This is the first time this scanner has been used.
        set_cur_state(cmpscan, exCOMMAND);
    }

    if(!is_section_token(newsec) && sec != 0) {
        // We only work with result section tokens.  Ignore all
        // non-section-tokens except the eof token.
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

        // then fire up the new section
        set_cur_state(cmpscan, newsec);
        switch(get_cur_state(cmpscan)) {
            case exSTDOUT:
				if(test->stdout_match == match_unknown) {
					compare_section_start(cmpscan, test->outfd, test->eachline,
							&test->stdout_match, get_testfile_name(test), "STDOUT");
				} else {
					fprintf(stderr, "%s line %d Error: duplicate STDOUT section.  Ignored.\n",
							get_testfile_name(test), test->testfile.line);
					// as long as scanref == null, no comparison will happen.
					assert(!cmpscan->scanref);
				}
                break;
            case exSTDERR:
				if(test->stderr_match == match_unknown) {
					compare_section_start(cmpscan, test->errfd, test->eachline,
							&test->stderr_match, get_testfile_name(test), "STDERR");
				} else {
					fprintf(stderr, "%s line %d Error: duplicate STDERR section.  Ignored.\n",
							get_testfile_name(test), test->testfile.line);
					// as long as scanref == null, no comparison will happen.
					assert(!cmpscan->scanref);
				}
                break;
            case exRESULT:
				parse_exit_clause(test, datap, len);
                break;
            case exMODIFY:
				parse_modify_clause(test, skip_section_name(datap,len), datap+len);
                break;
            case exCOMMAND:
                assert(!"Well, this is impossible.  How did you start a new command section??");
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
				if(contains_nws(datap, len)) {
					fprintf(stderr, "%s line %d Error: RESULT clause contains garbage.\n",
							get_testfile_name(test), test->testfile.line);
					//exit(10);
				}
                break;
            case exMODIFY:
				parse_modify_clause(test, datap, datap+len);
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


static void print_reason(struct test *test, const char *name)
{
	printf("%s %-25s ", name, get_testfile_name(test));
	if(!was_started(test->status)) {
		printf("by %s", test->last_file_processed);
	}
	if(test->status_reason) {
		printf("%s", test->status_reason);
	}
	printf("\n");
}


/** Checks the actual results against the expected results.
 */

void test_results(struct test *test)
{
    scanstate scanner;
    char scanbuf[MAX_LINE_LENGTH];
	int stdo, stde, exno;	// true if there are differences.
	
	if(was_aborted(test->status)) {
		print_reason(test, "ABRT");
		test_failures++;
		return;
	}

	if(was_disabled(test->status)) {
		print_reason(test, "dis ");
		return;
	}

    if(!was_started(test->status)) {
        fprintf(stderr, "Error: %s was not started.  TODO: add more info\n", get_testfile_name(test));
        test_failures++;
        return;
    }

    test->exitno_match = match_unknown;
    test->stdout_match = match_unknown;
    test->stderr_match = match_unknown;

    scanstate_init(&scanner, scanbuf, sizeof(scanbuf));
    scan_sections(test, &test->testfile, parse_section_compare, &scanner);

    assert(test->stdout_match != match_inprogress);
    assert(test->stderr_match != match_inprogress);

    // convert any unknowns into a solid yes/no
    if(test->exitno_match == match_unknown) {
		test->expected_exitno = 0;
        test->exitno_match = (test->exitno == 0 ? match_yes : match_no);
    }
    if(test->stdout_match == match_unknown) {
        test->stdout_match = (fd_has_data(test->outfd) ? match_no : match_yes);
    }
    if(test->stderr_match == match_unknown) {
        test->stderr_match = (fd_has_data(test->errfd) ? match_no : match_yes);
    }

    stdo = (test->stdout_match != match_yes);
    stde = (test->stderr_match != match_yes);
    exno = (test->exitno_match != match_yes);

    if(!stdo && !stde && !exno) {
        test_successes++;
        printf("ok   %s \n", get_testfile_name(test));
    } else {
        test_failures++;
        printf("FAIL %-25s ", get_testfile_name(test));
		printf("%c%c%c  ",
				(stdo ? 'O' : '.'),
				(stde ? 'E' : '.'),
				(exno ? 'X' : '.'));
		if(stdo || stde) {
			if(stdo) printf("stdout ");
			if(stdo && stde) printf("and ");
			if(stde) printf("stderr ");
			printf("differed");
		}
		if((stdo || stde) && exno) printf(", ");
		if(exno) printf("result was %d not %d", test->exitno, test->expected_exitno);
		printf("\n");
    }

    return;
}


static void write_exit_no(int fd, int exitno)
{
    char buf[512];
    int cnt;

    cnt = snprintf(buf, sizeof(buf), "RESULT: %d\n", exitno);
    write(fd, buf, cnt);
}


void write_raw_file(int outfd, int infd)
{
    char buf[BUFSIZ];
    int rcnt, wcnt;

    // first rewind the input file
    if(lseek(infd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "write_file lseek on %d: %s\n", infd, strerror(errno));
        exit(10);   // todo: consolidate with error codes in main
    }

    // then write the file.
    do {
        do {
            rcnt = read(infd, buf, sizeof(buf));
        } while(rcnt < 0 && errno == EINTR);
        if(rcnt > 0) {
            do {
                wcnt = write(outfd, buf, rcnt);
            } while(wcnt < 0 && errno == EINTR);
            if(wcnt < 0) {
                // write error.  do something!
                perror("writing in write_file");
                break;
            }
        } else if (rcnt < 0) {
            // read error.  do something!
            perror("reading in write_file");
            break;
        }
    } while(rcnt);
}


static void write_modified_file(int outfd, int infd, pcrs_job *job)
{
    // this routine is fairly similar to compare_continue_lines.
    // it would be nice to unify them.  that would take some fairly
    // major surgery though.

    scanstate scanner, *ss = &scanner;
    char scanbuf[MAX_LINE_LENGTH];
    const char *p;
    char *new;
    int newsize;
    int rcnt, wcnt;

    // first rewind the input file
    if(lseek(infd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "write_file lseek on %d: %s\n", infd, strerror(errno));
        exit(10);   // todo: consolidate with error codes in main
    }

    // create the scanner we'll use to buffer the lines
    scanstate_init(ss, scanbuf, sizeof(scanbuf));
    readfd_attach(ss, infd);

    do {
        p = memchr(ss->cursor, '\n', ss->limit - ss->cursor);
        if(!p) {
            rcnt = (*ss->read)(ss);
            if(rcnt < 0) {
                // read error.  do something!
                perror("reading in write_modified_file");
                break;
            }
            p = memchr(ss->cursor, '\n', ss->limit - ss->cursor);
            if(!p) {
                p = ss->limit - 1;
            }
        }

        p += 1;
        new = substitute_string(job, ss->cursor, p, &newsize);
        if(!new) {
            // substitution error!  message already printed.
            break;
        }

        do {
            wcnt = write(outfd, new, newsize);
        } while(wcnt < 0 && errno == EINTR);
        free(new);
        if(wcnt < 0) {
            // write error.  do something!
            perror("writing in write_modified_file");
            break;
        }
        ss->cursor = p;
    } while(rcnt);
}


static void write_file(int outfd, int infd, pcrs_job *job)
{
	if(!job) {
		// use the simple, fast routine
		write_raw_file(outfd, infd);
	} else {
		// use the line buffered routine
		write_modified_file(outfd, infd, job);
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
            // don't need to worry about eof
            break;

        case exSTDOUT|exNEW:
            test->stdout_match = match_yes;
			write_strconst(test->rewritefd, "STDOUT:\n");
            write_file(test->rewritefd, test->outfd, test->eachline);
            break;
        case exSTDOUT:
            // ignore all data in the expected stdout.
            break;

        case exSTDERR|exNEW:
            test->stderr_match = match_yes;
			write_strconst(test->rewritefd, "STDERR:\n");
            write_file(test->rewritefd, test->errfd, test->eachline);
        case exSTDERR:
            // ignore all data in the expected stderr
            break;

        case exRESULT|exNEW:
            test->exitno_match = match_yes;
            write_exit_no(test->rewritefd, test->exitno);
            break;

        case exRESULT:
            // allow random garbage in result section to pass
            write(test->rewritefd, datap, len);
            break;

        case exMODIFY|exNEW:
            parse_modify_clause(test, skip_section_name(datap,len), datap+len);
            write(test->rewritefd, datap, len);
            break;

        case exMODIFY:
            // parse modify sections and still print them.
            parse_modify_clause(test, datap, datap+len);
            write(test->rewritefd, datap, len);
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
	if(was_aborted(test->status)) {
		dump_reason(test, "was aborted");
		return;
	}

	if(was_disabled(test->status)) {
		dump_reason(test, "is disabled");
		return;

	}

    if(!was_started(test->status)) {
        fprintf(stderr, "Error: %s was not started.  TODO: add more info\n", get_testfile_name(test));
        test_failures++;
        return;
    }

    // The command section has already been dumped.  We just
    // need to dump the result sections.  The trick is, though,
    // that we need to dump them in the same order as they occur
    // in the testfile otherwise the diff will be all screwed up.

    test->exitno_match = match_unknown;
    test->stdout_match = match_unknown;
    test->stderr_match = match_unknown;

    // ensure that we haven't yet parsed any modify sections.
    assert(!test->eachline);

    scan_sections(test, &test->testfile, parse_section_output, NULL);

    // if any sections haven't been output, but they differ from
    // the default, then they need to be output here at the end.

    if(test->exitno_match == match_unknown && test->exitno != 0) {
        write_exit_no(test->rewritefd, test->exitno);
    }
    if(test->stderr_match == match_unknown && fd_has_data(test->errfd)) {
		write_strconst(test->rewritefd, "STDERR:\n");
        write_file(test->rewritefd, test->errfd, test->eachline);
    }
    if(test->stdout_match == match_unknown && fd_has_data(test->outfd)) {
		write_strconst(test->rewritefd, "STDOUT:\n");
        write_file(test->rewritefd, test->outfd, test->eachline);
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
	int err;

	// the buffer for the testfile scanner is allocated on the stack.
	if(test->eachline) {
		pcrs_free_joblist(test->eachline);
	}

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


