/* main.c
 * 28 Dec 2004
 * Copyright (C) 2004 Scott Bronson
 * 
 * The main routine for tmtest.
 *
 * This file is distrubuted under the MIT License
 * See http://en.wikipedia.org/wiki/MIT_License for more.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <getopt.h>
#include <assert.h>

#include "re2c/read-fd.h"

#include "test.h"
#include "qscandir.h"
#include "vars.h"
#include "tfscan.h"
#include "pathconv.h"
#include "pathstack.h"
#include "units.h"


#define DIFFPROG "/usr/bin/diff"
#define SHPROG   "/bin/bash"


enum {
    outmode_test,
    outmode_dump,
    outmode_diff,
	outmode_failures_only
};

int outmode = outmode_test;
int allfiles = 0;			// run a testfile even if it begins with a dash
int dumpscript = 0;			// print the script instead of running it
int quiet = 0;
const char *orig_cwd;		// tmtest runs with the cwd pointed to /tmp
char *config_file;	// absolute path to the user-specified config file
					// null if user didn't specify a config file.

#define TESTDIR "/tmp/tmtest-XXXXXX"
char g_testdir[sizeof(TESTDIR)];

#define OUTNAME "stdout"
#define ERRNAME "stderr"
#define STATUSNAME "status"
char g_outname[sizeof(TESTDIR)+sizeof(OUTNAME)];
char g_errname[sizeof(TESTDIR)+sizeof(ERRNAME)];
char g_statusname[sizeof(TESTDIR)+sizeof(STATUSNAME)];

// file in tmpdir that holds stdout
#define DIFFNAME "diff"


int g_outfd;
int g_errfd;
int g_statusfd;


struct timeval test_start_time;
struct timeval test_stop_time;


// exit values:
enum {
    no_error = 0,
    argument_error=100,
    runtime_error,
    interrupted_error,
    internal_error,
	initialization_error,
};


#define xstringify(x) #x
#define stringify(x) xstringify(x)


#define is_dash(s) ((s)[0] == '-' && (s)[1] == '\0')


/** Returns zero if s1 ends with s2, nonzero if not.
 */

static int strcmpend(const char *s1, const char *s2)
{
    size_t n1 = strlen(s1);
    size_t n2 = strlen(s2);

    if(n2 <= n1) {
        return strncmp(s1+n1-n2, s2, n2);
    } else {
        return 1;
    }
}


static int i_have_permission(const struct stat *st, int op)
{
	if(st->st_mode & S_IRWXU & op) {
		if(geteuid() == st->st_uid) {
			return 1;
		}
	}

	if(st->st_mode & S_IRWXG & op) {
		if(getegid() == st->st_gid) {
			return 1;
		}
	}

	if(st->st_mode & S_IRWXO & op) {
		return 1;
	}

	return 0;
}


static int verify_readable(const char *file, struct stat *st, int regfile)
{
	struct stat sts;
	
	// arg is optional so we can pass the stat struct back
	// if the caller wants to do more with it.
	if(st == NULL) {
		st = &sts;
	}

	if(stat(file, st) < 0) {
		fprintf(stderr, "Could not locate %s: %s\n", file, strerror(errno));
		return 0;
	}
	if(regfile && !S_ISREG(st->st_mode)) {
		fprintf(stderr, "Could not open %s: not a file!\n", file);
		return 0;
	}
	if(!i_have_permission(st, 0444)) {
		fprintf(stderr, "Could not open %s: permission denied!\n", file);
		return 0;
	}

	return 1;
}


/** Prints the given template to the given file, performing substitutions.
 */

static void print_template(struct test *test, const char *tmpl,  FILE *fp)
{
    char varbuf[32];
    const char *cp, *ocp, *ce;
    int len;

    for(ocp=cp=tmpl; (cp=strchr(cp,'%')); cp++) {
        if(cp[1] == '(') {
            // perform a substitution
            fwrite(ocp, cp - ocp, 1, fp);
            cp += 2;
            ce = strchr(cp,')');
            if(!ce) {
                fprintf(stderr, "Unterminated template variable: '%.20s'\n", cp);
                exit(runtime_error);
            }
            len = ce - cp;
            if(len <= 0) {
                fprintf(stderr, "Garbage template variable: '%.20s'\n", cp);
                exit(runtime_error);
            }
            // truncate variable name if it doesn't fit into varbuf.
            if(len > sizeof(varbuf)-1) {
                len = sizeof(varbuf)-1;
            }
            memcpy(varbuf, cp, len);
            varbuf[len] = '\0';
            if(printvar(test,fp,varbuf) != 0) {
                // printvar has already printed the error message
                exit(runtime_error);
            }
            ocp = cp = ce+1;
        }
    }

    fputs(ocp, fp);
}


/** Checks the actual results against the expected results.
 *
 * Returns 0 if all results match, or a code telling what
 * part didn't match.
 */


static void reset_fd(int fd, const char *fname)
{
    if(lseek(fd, 0, SEEK_SET) < 0) {
        fprintf(stderr, "Couldn't seek to start of %s: %s\n",
                fname, strerror(errno));
        exit(runtime_error);
    }

    if(ftruncate(fd, 0) < 0) {
        fprintf(stderr, "Couldn't reset file %s: %s\n",
                fname, strerror(errno));
        exit(runtime_error);
    }
}


static int wait_for_child(int child, const char *name)
{
    int pid;
    int status;

    // wait patiently for child to finish.
    pid = waitpid(child, &status, 0);
    if(pid < 0) {
        fprintf(stderr, "Error waiting for %s to finish: %s\n",
                strerror(errno), name);
        exit(runtime_error);
    }

    if(WIFSIGNALED(status)) {
        if(WTERMSIG(status) == SIGINT) {
            // If test was interrupted with a sigint then raise it on ourselves.
            // Otherwise it can be hard to interrupt a series of tests
            // (you kill one test but the next one fires right up).
			kill(getpid(), SIGINT);
        }
    } else if(!WIFEXITED(status)) {
        // not signalled, but os claims child didn't exit normally.
        fprintf(stderr, "WTF??  Unknown status returned by %s: %d\n",
                name, status);
        exit(runtime_error);
    }

    return status;
}


static int open_file(char *fn, const char *name, int flags)
{
	strcpy(fn, g_testdir);
	strcat(fn, "/");
	strcat(fn, name);

    int fd = open(fn, flags|O_RDWR|O_CREAT/*|O_EXCL*/, S_IRUSR|S_IWUSR);
    if(fd < 0) {
        fprintf(stderr, "couldn't open %s: %s\n", fn, strerror(errno));
        exit(runtime_error);
    }

    return fd;
}


static void write_stdin_to_tmpfile(struct test *test)
{
	char *buf;
	int fd;

	buf = malloc(sizeof(TESTDIR) + sizeof(DIFFNAME));
	if(!buf) {
		perror("malloc");
		exit(runtime_error);
	}

	test->diffname = buf;
	fd = open_file(buf, DIFFNAME, 0);
	assert(strlen(buf) == sizeof(TESTDIR)+sizeof(DIFFNAME)-1);
	write_file(test, fd, 0, NULL);
	close(fd);
}


/** Forks off a diff process and sets it up to receive the dumped test.
 */

static int start_diff(struct test *test)
{
    int pipes[2];
    int child;
	const char *filename = NULL;
	char buf[PATH_MAX];
	struct pathstack stack;

	assert(test->testfilename);
	// if the test is coming from stdin, we need to copy it to a
	// real file before we can diff against it.
    if(is_dash(test->testfilename)) {
		// first, write all of our stdin to a tmpfile.
		write_stdin_to_tmpfile(test);
		// then, read the test from this file instead of stdin.
		filename = test->diffname;
		assert(filename);
		assert(filename[0]);
    }
		
    if(pipe(pipes) < 0) {
        perror("creating diff pipe");
        exit(runtime_error);
    }
    
    child = fork();
    if(child < 0) {
        perror("forking diff");
        exit(runtime_error);
    }
    if(child == 0) {
        if(dup2(pipes[0], 0) < 0) {
            perror("dup2ing test output to child's stdin");
            exit(runtime_error);
        }
        close(pipes[0]);
        close(pipes[1]);
        
        if (!filename) {
			// need to figure out the filename to pass to diff
        	if (test->testfilename[0] == '/') {
        		// if the path is absolute, we can just use it straight away.
				filename = test->testfilename;
			} else {
				pathstack_init(&stack, buf, sizeof(buf), test->testfiledir);
				pathstack_push(&stack, test->testfilename, NULL);
				filename = pathstack_absolute(&stack);
			}
		}

		if(0 != chdir(test->testfiledir)) {
			fprintf(stderr, "Could not chdir 1 to %s: %s\n",
					test->testfiledir, strerror(errno));
			exit(runtime_error);
		}

        execl(DIFFPROG, DIFFPROG, "-u", filename, "-", (char*)NULL);
        perror("executing " DIFFPROG " for test");
        exit(runtime_error);
    }

    close(pipes[0]);
    test->rewritefd = pipes[1];

    return child;
}


/** Waits for the forked diff process to finish.
 */

static void finish_diff(struct test *test, int diffpid)
{
    int status;
    int exitcode;

    close(test->rewritefd);

    status = wait_for_child(diffpid, "diff");
    if(WIFSIGNALED(status)) {
        fprintf(stderr, "diff terminated by signal %d!\n", WTERMSIG(status));
        exit(runtime_error);
    }

    exitcode = WEXITSTATUS(status);

    // I forget what return code 1 meands but it's harmless
    // (in gnu diff anyway; dunno about other diffs)
    if(exitcode != 0 && exitcode != 1) {
        fprintf(stderr, "diff returned %d!\n", exitcode);
        exit(runtime_error);
    }
}


/* Combines testfiledir and testfilename into a single absolute path for the testfile.
 * The caller must supply a buffer to fill with the result. */

static void assemble_absolute_testpath(struct test *test, char *buf, int bufsiz)
{
	buf[0] = '\0';
	strncat(buf, test->testfiledir, bufsiz-1);
	strncat(buf, "/", bufsiz-1);
	strncat(buf, test->testfilename, bufsiz-1);
}


/* Prints the relative path from the original cwd to the current testfile */

static void print_test_path(struct test *test)
{
	char result[PATH_MAX];
	char testfile[PATH_MAX];
	
	assemble_absolute_testpath(test, testfile, sizeof(testfile));

	if(abs2rel(testfile, orig_cwd, result, sizeof(result))) {
		printf("%s\n", result);
	} else {
		printf("print_test_path: abs2rel error: %s relto %s\n", testfile, orig_cwd);
	}
}

static int open_test_file(struct test *test)
{
	char buf[PATH_MAX];
	int fd;
	
	// If the filename is a dash then we just use stdin.
	if(is_dash(test->testfilename)) {
        return STDIN_FILENO;
    }
    
	if(test->testfilename[0] == '/') {
	    // If the filename is absolute, we use it directly.
	    strncpy(buf, test->testfilename, sizeof(buf));
	    buf[sizeof(buf)-1] = 0;
	} else {
		// Otherwise we need to make an absolute path
		assemble_absolute_testpath(test, buf, sizeof(buf));
	}
	
	fd = open(buf, O_RDONLY);
    if(fd < 0) {
        fprintf(stderr, "Could not open %s: %s\n", buf, strerror(errno));
        exit(runtime_error);
    }
    
    return fd;
}


/** Runs the named testfile.
 *
 * If warn_suffix is true and the ffilename doesn't end in ".test"
 * then we'll print a warning to stderr.  This is used when
 * processing the cmdline args so the user will know why a file
 * that was explicitly named didn't run.
 *
 * When config files are executing, they use the standard stdout
 * and stderr.  That way, the user sees any output while the test
 * is running (should help with debugging).  However, when the
 * test itself is running, its output is redirected into outfd/errfd.
 *
 * It may appear that outmode_dump mixes stdio and Unix I/O, but it
 * doesn't really.  We only print to stdio when testing, and we only
 * dump the file when dumping.  They cannot both happen simultaneously.
 *
 * @returns 1 if we should keep testing, 0 if we should stop now.
 */

static int run_test(const char *path, const char *name, const char *dispname, int warn_suffix)
{
    struct test test;
    char buf[BUFSIZ];   // scan buffer for the testfile
    int pipes[2];
    int child;
	int keepontruckin = 0;
    int diffpid;
    int fd = -1;
    int i;
    FILE *tochild;
	jmp_buf abort_jump;

    // defined in the exec.c file generated by exec.tmpl.
    extern const char exec_template[];

    if(!is_dash(name) && !allfiles && (strcmpend(name, ".test") != 0)) {
        if(warn_suffix) {
            fprintf(stderr, "%s was skipped because it doesn't end in '.test'.\n", name);
        }
        return 1;
    }

	// so that we can safely single quote filenames in the shell.
	if(strchr(name, '\'') || strchr(name, '"')) {
		fprintf(stderr, "%s was skipped because its file name contains a quote character.\n", name);
		return 1;
	}

    test_init(&test);
	if(setjmp(abort_jump)) {
		// test was aborted.
		fprintf(stderr, "Test aborted: %s\n", test.status_reason);
        exit(runtime_error);
	}

    test.testfilename = name;
    test.testfiledir = path;
    test.outfd = g_outfd;
    test.errfd = g_errfd;
    test.statusfd = g_statusfd;

    // initialize the test mode
    switch(outmode) {
        case outmode_test:
		case outmode_failures_only:
            // nothing to do
            break;
        case outmode_dump:
            test.rewritefd = STDOUT_FILENO;
            break;
        case outmode_diff:
            diffpid = start_diff(&test);
            break;
        default:
            assert(!"Unhandled outmode 1 in run_test()");
    }

    // reset the stdout and stderr capture files.
    reset_fd(test.outfd, "stdout");
    reset_fd(test.errfd, "stderr");
    reset_fd(test.statusfd, "status");

    // set up the pipe to feed input to the child.
    // ignore sigpipes since we don't want a signal raised if child
    // quits early (which almost always happens since it exits before
    // it reads its expected stdout/stderr).
    if(pipe(pipes) < 0) {
        perror("creating test pipe");
        exit(runtime_error);
    }

    // fork child process
    child = fork();
    if(child < 0) {
        perror("forking test");
        exit(runtime_error);
    }
    if(child == 0) {
        if(dup2(pipes[0], 0) < 0) {
            perror("dup2ing input to test's stdin");
            exit(runtime_error);
        }
        close(pipes[0]);
        close(pipes[1]);
        execl(SHPROG, SHPROG, "-s", (char*)NULL);
        perror("executing " SHPROG " for test");
        exit(runtime_error);
    }

    // create the testfile scanner.  it will either scan from
    // the testfile itself or from stdin if filename is "-".
    scanstate_init(&test.testfile, buf, sizeof(buf));
	if(test.diffname) {
		if(lseek(test.diff_fd, 0, SEEK_SET) < 0) {
			fprintf(stderr, "Couldn't seek to start of %s: %s\n",
					test.diffname, strerror(errno));
			exit(runtime_error);
		}
        readfd_attach(&test.testfile, test.diff_fd);
	} else {
        readfd_attach(&test.testfile, open_test_file(&test));
    }
    tfscan_attach(&test.testfile);

    if(dumpscript) {
        print_template(&test, exec_template, stdout);
        // don't want to print a summary of the tests run so make
        // sure tmtest realizes it's dumping a test.
        outmode = outmode_dump;
    } else {
        // set up the pipes for the parent
        close(pipes[0]);
        tochild = fdopen(pipes[1], "w");
        if(!tochild) {
            perror("calling fdopen on pipe");
            exit(runtime_error);
        }

        // write the test script to the kid
        print_template(&test, exec_template, tochild);
        fclose(tochild);

        // wait for the test to finish
        i = wait_for_child(child, "test");
        test.exitsignal = (WIFSIGNALED(i) ? WTERMSIG(i) : 0);
        test.exitcored = (WIFSIGNALED(i) ? WCOREDUMP(i) : 0);
        test.exitno = (WIFEXITED(i) ? WEXITSTATUS(i) : 256);

        // read the status file to determine what happened
        // and store the information in the test struct.
        scan_status_file(&test);

        // process and output the test results
        switch(outmode) {
            case outmode_test:
                test_results(&test, dispname);
                break;
			case outmode_failures_only:
				if(check_for_failure(&test, dispname)) {
					print_test_path(&test);
				}
				break;
            case outmode_dump:
                dump_results(&test);
                break;
            case outmode_diff:
                dump_results(&test);
                finish_diff(&test, diffpid);
                break;
            default:
                assert(!"Unhandled outmode 2 in run_test()");
        }

        // if we had to open the testfile to read it, we now close it.
        // because the scanner is statically allocated, there's no
        // need to destroy it.
        if(fd >= 0) {
            close(fd);
        }

        keepontruckin = !was_aborted(test.status);
    }

    test_free(&test);

	return keepontruckin;
}


/** Processes a directory specified using an absolute or deep
 * path.  We need to save and restore curpath to do this.
 *
 * It's illegal to call this routine with a path that doesn't
 * include at least one / character.
 *
 * @returns 1 if we should continue testing, 0 if we should abort.
 */

static int process_absolute_file(const char *abspath, const char *origpath, int warn_suffix)
{
	char buf[PATH_MAX];
	struct pathstack stack;
	char *file, *dir;

	pathstack_init(&stack, buf, sizeof(buf), abspath);
	pathstack_normalize(&stack);
	
	dir = pathstack_absolute(&stack);
	file = strrchr(dir, '/');
	if(!file) {
		fprintf(stderr, "Path wasn't absolute in process_absolute file!?  %s\n", abspath);
		return 0;
	}
	
	*file++ = '\0';		// separate the path and the filename
	// If the file was in the root directory, ensure we don't blow away
	// the leading slash.
	if(dir[0] == '\0') {
		dir = "/";
	}
	
	return run_test(dir, file, origpath, warn_suffix);
}


// forward declaration for recursion
int process_dir(struct pathstack *ps, int print_absolute);


static int process_absolute_dir(const char *abspath, const char *origpath, int print_absolute)
{
	char buf[PATH_MAX];
	struct pathstack stack;

	if(outmode == outmode_test) {
		if(print_absolute) {
			printf("\nProcessing %s\n", abspath);
		} else {
			printf("\nProcessing ./%s\n", origpath);
		}
	}
	
	pathstack_init(&stack, buf, sizeof(buf), abspath);
	pathstack_normalize(&stack);
	return process_dir(&stack, print_absolute);
}


static void print_relative_dir(struct pathstack *ps, int print_absolute)
{
	char buf[PATH_MAX];
	
	// Don't print anything unless we're actually testing.
    if(outmode != outmode_test) {
    	return;
    }

	if(print_absolute) {
		printf("\nProcessing %s\n", pathstack_absolute(ps));
	} else {
		if(!abs2rel(pathstack_absolute(ps), orig_cwd, buf, PATH_MAX)) {
			printf("Path couldn't be converted \"\%s\"\n", pathstack_absolute(ps));
			exit(runtime_error);
		}
		printf("\nProcessing ./%s\n", buf);
	}
}


/** Process all entries in a directory.
 *
 * @param ps The pathstack to use.  It comes pre-set-up with whatever
 * basedir we should be running from (where the ents are found).
 * This will be used during processing but is guaranteed to be
 * returned in exactly the same state as it was passed in.
 * @param ents The list of entries to process.
 * @param print_absolute tells if you want the user-visible path to
 * be printed absolute or relative.  -1 means that you don't know,
 * and process_ents should decide on its own for each ent (if the
 * ent is absolute then all paths will be absolute).
 */

static int process_ents(struct pathstack *ps, char **ents, int print_absolute)
{
	struct stat st;
	struct pathstate save;
    mode_t *modes;
    int i, n, ret;
	int keepontruckin;

    for(n=0; ents[n]; n++)
        ;

    modes = malloc(n * sizeof(mode_t));
    if(!modes) {
        fprintf(stderr, "Could not allocate %d mode_t objects.\n", n);
        exit(runtime_error);
    }
    
    // first collect the stat info for each entry and perform a quick sanity check
    for(i=0; i<n; i++) {
        if(!is_dash(ents[i])) {
			const char *cp = ents[i];
			if(ents[i][0] != '/') {
				ret = pathstack_push(ps, ents[i], &save);
				if(ret != 0) {
					fprintf(stderr, "Paths are too long:\n   %s\n   %s\n", pathstack_absolute(ps), ents[i]);
					exit(runtime_error);
				}
				cp = pathstack_absolute(ps);
			}
			// Need to be careful to test that file does exist.
			// Bash opens it, not us, so the error message might
			// be confusing.
			if(!verify_readable(cp,&st,0)) {
                exit(runtime_error);
            }
			if(ents[i][0] != '/') {
				pathstack_pop(ps, &save);
			}
            modes[i] = st.st_mode;
        }
    }
    
    // process all files in dir
    for(i=0; i<n; i++) {
        if(is_dash(ents[i]) || S_ISREG(modes[i])) {
			if(ents[i][0] == '/') {
				// we know the path has already been fully normalized.
				assert(print_absolute == -1);	// it should be impossible to get here if we've already recursed
				keepontruckin = process_absolute_file(ents[i], ents[i], 1);
			} else {
				if(strchr(ents[i], '.') || strchr(ents[i], '/')) {
					// if there are potential non-normals in the path, we need to normalize it.
					ret = pathstack_push(ps, ents[i], &save);
					keepontruckin = process_absolute_file(pathstack_absolute(ps), ents[i], print_absolute == -1);
					pathstack_pop(ps, &save);
				} else {
					keepontruckin = run_test(pathstack_absolute(ps), ents[i], ents[i], print_absolute == -1);
				}
			}
			if(!keepontruckin) {
				goto abort;
			}
            modes[i] = 0;
        }
    }

    // process all subdirs
    for(i=0; i<n; i++) {
        if(is_dash(ents[i]) || modes[i] == 0) continue;
        if(S_ISDIR(modes[i])) {
			if(ents[i][0] == '/') {
				assert(print_absolute == -1);	// it should be impossible to get here if we've already recursed
				keepontruckin = process_absolute_dir(ents[i], ents[i], 1);
			} else {
				if(print_absolute == -1) print_absolute = 0;
				if(strchr(ents[i], '.') || strchr(ents[i], '/')) {
					// if there are potential non-normals in the path, we need to normalize it.
					ret = pathstack_push(ps, ents[i], &save);
					keepontruckin = process_absolute_dir(pathstack_absolute(ps), ents[i], print_absolute);
					pathstack_pop(ps, &save);
				} else {
					// Otherwise, we just push the path and chug.
					ret = pathstack_push(ps, ents[i], &save);
					print_relative_dir(ps, print_absolute);
					keepontruckin = process_dir(ps, print_absolute);
					pathstack_pop(ps, &save);
				}
			}
			if(!keepontruckin) {
				goto abort;
			}
        }
    }

abort:
    free(modes);
	return keepontruckin;
}


/** This routine filters out any dirents that begin with '.'.
 *  We don't want to process any hidden files or special directories.
 */

static int select_nodots(const struct dirent *d)
{
    return d->d_name[0] != '.';
}


/** Runs all tests in the current directory and all its subdirectories.
 */

int process_dir(struct pathstack *ps, int print_absolute)
{
    char **ents;
    int i, keepontruckin;

    ents = qscandir(pathstack_absolute(ps), select_nodots, qdirentcoll);
    if(!ents) {
        // qscandir has already printed the error message
        exit(runtime_error);
    }

    keepontruckin = process_ents(ps, ents, print_absolute);

    for(i=0; ents[i]; i++) {
        free(ents[i]);
    }
    free(ents);

	return keepontruckin;
}


static void checkerr(int err, const char *op, const char *name)
{
	if(err < 0) {
		fprintf(stderr, "There was an error %s %s: %s\n",
				op, name, strerror(errno));
		// not much else we can do other than complain...
	}
}


static void stop_tests()
{
	gettimeofday(&test_stop_time, NULL);

	checkerr(close(g_outfd), "closing", g_outname);
	checkerr(close(g_errfd), "closing", g_errname);
	checkerr(close(g_statusfd), "closing", g_statusname);

	checkerr(unlink(g_outname), "deleting", g_outname);
	checkerr(unlink(g_errname), "deleting", g_errname);
	checkerr(unlink(g_statusname), "deleting", g_statusname);

	checkerr(rmdir(g_testdir), "removing directory", g_testdir);
}


static void sig_int(int blah)
{
	stop_tests();
	exit(interrupted_error);
}


/** Prepare system for running tests.
 *
 * We do all I/O for all tests through only three file descriptors.
 * We seek to the beginning of each file before running each test.
 * This should save some inode thrashing.
 */

static void start_tests()
{
	char *cp;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sig_int);

	strcpy(g_testdir, TESTDIR);
	if(!mkdtemp(g_testdir)) {
		fprintf(stderr, "Could not call mkdtemp() on %s: %s\n", g_testdir, strerror(errno));
		exit(initialization_error);
	}

	// errors are handled by open_file.
    g_outfd = open_file(g_outname, OUTNAME, 0);
	assert(strlen(g_outname) == sizeof(g_outname)-1);
    g_errfd = open_file(g_errname, ERRNAME, 0);
	assert(strlen(g_errname) == sizeof(g_errname)-1);
    g_statusfd = open_file(g_statusname, STATUSNAME, O_APPEND);
	assert(strlen(g_statusname) == sizeof(g_statusname)-1);

	// tmtest always runs with the CWD pointed to the temporary directory
	cp = getenv("TMPDIR");
	if(!cp) cp = "/tmp";
	if(chdir(cp) != 0) {
		fprintf(stderr, "Could not chdir 2 to %s: %s\n", cp, strerror(errno));
		exit(initialization_error);
	}

	gettimeofday(&test_start_time, NULL);
}


static void set_config_file(const char *cfg)
{
	char buf[PATH_MAX];

	if(cfg[0] == '\0') {
		fprintf(stderr, "You must specify a directory for --config.\n");
		exit(argument_error);
	}

	if(cfg[0] == '/') {
		strncpy(buf, cfg, PATH_MAX);
		buf[PATH_MAX-1] = '\0';
	} else {
		strncpy(buf, orig_cwd, PATH_MAX);
		strncat(buf, "/", PATH_MAX);
		strncat(buf, cfg, PATH_MAX);
		buf[PATH_MAX-1] = '\0';
	}

	normalize_absolute_path(buf);

	// need to ensure as well as we can that the file is readable because
	// we don't open it ourselves.  Bash does.  And that can lead to some
	// really cryptic error messages.
	if(!verify_readable(buf,NULL,1)) {
		exit(runtime_error);
	}

	config_file = strdup(buf);
	if(!config_file) {
		perror("strdup");
		exit(runtime_error);
	}
}


static void usage()
{
	printf(
			"Usage: tmtest [OPTION]... [DLDIR]\n"
			"  -o: output the test file with the new output.\n"
			"  -d: output a diff between the expected and actual outputs.\n"
			"  -V --version: print the version of this program.\n"
			"  -h --help: prints this help text\n"
			"Run tmtest with no arguments to run all tests in the current directory.\n"
		  );
}


static void process_args(int argc, char **argv)
{
    char buf[256], *cp;
    int optidx, i, c;

	optidx = 0;
	static struct option longopts[] = {
		// name, has_arg (1=reqd,2=opt), flag, val
		{"ignore-extension", 0, &allfiles, 1},
		{"config", 1, 0, 'c'},
		{"diff", 0, 0, 'd'},
		{"dump-script", 0, &dumpscript, 1},
		{"failures-only", 0, 0, 'f'},
		{"help", 0, 0, 'h'},
		{"output", 0, 0, 'o'},
		{"quiet", 0, 0, 'q'},
		{"run-unit-tests", 0, 0, 'U'},
		{"show-unit-fails", 0, 0, 257},
		{"version", 0, 0, 'V'},
		{0, 0, 0, 0},
	};

	// dynamically create the option string from the long
	// options.  Why oh why doesn't glibc do this for us???
	cp = buf;
	for(i=0; longopts[i].name; i++) {
		if(!longopts[i].flag) {
			*cp++ = longopts[i].val;
			if(longopts[i].has_arg > 0) *cp++ = ':';
			if(longopts[i].has_arg > 1) *cp++ = ':';
		}
	}
	*cp++ = '\0';

	while(1) {
		c = getopt_long(argc, argv, buf, longopts, &optidx);
		if(c == -1) break;

		switch(c) {
			case 'c':
				set_config_file(optarg);
				break;

            case 'd':
                outmode = outmode_diff;
                break;

			case 'f':
				outmode = outmode_failures_only;
				break;

			case 'h':
				usage();
				exit(0);

			case 'o':
                outmode = outmode_dump;
				break;

			case 'q':
				quiet++;
				break;

			case 'U':
				run_unit_tests(all_tests);
				exit(0);

			case 257:
				run_unit_tests_showing_failures(all_tests);
				exit(0);

			case 'V':
				printf("tmtest version %s\n", stringify(VERSION));
				exit(0);

			case '?':
				// getopt_long already printed the error message
				exit(argument_error);

			case 0:
				// an option was automatically set
				break;

			default:
				fprintf(stderr, "getopt_long returned something weird: %d\n", c);
				exit(internal_error);
		}
	}
}


/* normalize_path
 * 
 * I wish I could use canonicalize_path(3), but that routine resolves
 * symbolic links and provides no way to turn that behavior off.
 * How stupid!  This isn't as much of a hack as it looks because
 * "./../.." still needs to resolve to a real directory.
 *
 * @param original: the path to be normalized
 * @param outpath: the normalized path.  this may or may not be the same
 *     as original.
 * 
 * TODO: how about hitting this routine with some unit tests?
 */

static void normalize_path(struct pathstack *ps, char *original, char **outpath)
{
    char buf[PATH_MAX];
    char normalized[PATH_MAX];

    if(original[0] == '/') {
		if(strlen(original) > PATH_MAX-1) {
			fprintf(stderr, "Path is too long: %s\n", original);
			exit(runtime_error);
		}
		strcpy(normalized, original);
		normalize_absolute_path(normalized);
    } else {
        strncpy(buf, pathstack_absolute(ps), PATH_MAX);
		strncat(buf, "/", PATH_MAX);
		strncat(buf, original, PATH_MAX);
		buf[PATH_MAX-1] = '\0';
		normalize_absolute_path(buf);

		// convert it back to a relative path so it prints the
		// way the user intends.  We need to beware later on
		// to trim .. from the leading path.
		if(!abs2rel(buf, pathstack_absolute(ps), normalized, PATH_MAX)) {
            fprintf(stderr, "Could not reabsize %s: %s\n",
				original, strerror(errno));
			exit(runtime_error);
		}
    }

	if(strcmp(original,normalized) == 0) {
		*outpath = original;
	} else {
		*outpath = strdup(normalized);
	}
}


static void normalize_free(const char *original, char *outpath)
{
    if(outpath != original) {
        free(outpath);
    }
}


/**
 * Normalize all the passed-in paths, then call process_ents()
 * with the normalized paths.  It's easiest to just dup argv
 * and modify that.
 */

static void process_argv(struct pathstack *ps, char **argv)
{
	char **ents;
	int i, n, oldlen;

    for(n=0; argv[n]; n++) { }

	ents = malloc((n+1)*sizeof(*ents));
	ents[n] = NULL;
	
	for(i=0; i<n; i++) { normalize_path(ps, argv[i], &ents[i]); }
	oldlen = ps->curlen;
	process_ents(ps, ents, -1);
	assert(oldlen == ps->curlen);	// process_ents needs to not modify the pathstack.
	for(i=0; i<n; i++) { normalize_free(argv[i], ents[i]); }

	free(ents);
}


/**
 * The Gnu version of getcwd can do this for us but that's not portable.
 */

static const char* dup_cwd()
{
	char buf[PATH_MAX];

	if(!getcwd(buf, PATH_MAX)) {
		perror("Couldn't get current working directory");
		exit(runtime_error);
	}

	return strdup(buf);
}


int main(int argc, char **argv)
{
	char buf[PATH_MAX];
	struct pathstack pathstack;
	
	orig_cwd = dup_cwd();
	process_args(argc, argv);

	pathstack_init(&pathstack, buf, sizeof(buf), orig_cwd);

    start_tests();
    if(optind < argc) {
		process_argv(&pathstack, argv+optind);
    } else {
        if(outmode == outmode_test) {
            printf("\nProcessing .\n");
        }
        process_dir(&pathstack, 0);
    }
    stop_tests();

    if(outmode == outmode_test) {
        print_test_summary(&test_start_time, &test_stop_time);
    }

	free((char*)orig_cwd);
	return test_get_exit_value();
}

