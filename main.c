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
#include "curdir.h"
#include "qscandir.h"
#include "vars.h"
#include "tfscan.h"
#include "pathconv.h"
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
int allfiles = 0;
int dumpscript = 0;
int quiet = 0;
const char *orig_cwd;		// tmtest runs with the cwd pointed to /tmp
char *config_file;	// absolute path to the user-specified config file
					// null if user didn't specify a config file.

int arg_was_absolute;	// this is an absolutely awful hack.  it's true
		// if the current path is absolute, false if it's relative.

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


static int write_stdin_to_tmpfile(struct test *test)
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

	return fd;
}


/** Forks off a diff process and sets it up to receive the dumped test.
 */

static int start_diff(struct test *test)
{
    int pipes[2];
    int child;
	const char *filename = NULL;

    assert(test->testfilename);
	// if the test is coming from stdin, we need to copy it to a
	// real file before we can diff against it.
    if(is_dash(test->testfilename)) {
		// first, write all of our stdin to a tmpfile.
		write_stdin_to_tmpfile(test);
		// then, read the test from this file instead of stdin.
		filename = test->diffname;
		assert(filename);
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

		if(!filename) {
			// figure out the filename that diff will use
			if(test->testfilename[0] == '/') {
				filename = test->testfilename;
			} else {
				// since we don't have an absolute path, we need to
				// cd to the original wd and run the diff with
				// a relative path.  it takes a bit of computation...
				curpush(test->testfilename);
				filename = strdup(currelative());
				if(!filename) {
					perror("strdup in start_diff");
					exit(runtime_error);
				}
				curpop(1);
				if(0 != chdir(curabsolute())) {
					fprintf(stderr, "Could not chdir 1 to %s: %s\n",
							curabsolute(), strerror(errno));
					exit(runtime_error);
				}
			}
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


/* Prints the relative path from the original cwd to the current testfile */

static void print_test_path(struct test *test)
{
	char result[PATH_MAX];

	int keep = curpush(test->testfilename);
	if(keep <= 0) {
		printf("print_test_path: path is too long!\n");
		return;
	}

	if(abs2rel(curabsolute(), orig_cwd, result, sizeof(result))) {
		printf("%s\n", result);
	} else {
		printf("print_test_path: abs2rel error: %s relto %s\n",
			curabsolute(), orig_cwd);
	}

	curpop(keep);
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

static int run_test(const char *name, const char *dispname, int warn_suffix)
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
	} else if(is_dash(name)) {
        readfd_attach(&test.testfile, STDIN_FILENO);
    } else {
		if(name[0] == '/') {
			fd = open(name, O_RDONLY);
		} else {
			int keep = curpush(name);
			if(keep <= 0) {
				fprintf(stderr, "Path is too long.");
				exit(runtime_error);
			}
			fd = open(curabsolute(), O_RDONLY);
			curpop(keep);
		}
        if(fd < 0) {
            fprintf(stderr, "Could not open %s: %s\n",
                    curabsolute(), strerror(errno));
            exit(runtime_error);
        }
        readfd_attach(&test.testfile, fd);
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


/** This routine filters out any dirents that begin with '.'.
 *  We don't want to process any hidden files or special directories.
 */

static int select_nodots(const struct dirent *d)
{
    return d->d_name[0] != '.';
}


/**
 * Sucks the dirname from an absolute file path and calls curinit with it.
 */

static void init_absolute_filepath(const char *path)
{
	const char *cp;
	int loc;
	char buf[PATH_MAX];
	
	cp = strrchr(path, '/');
	if(cp == NULL) {
		fprintf(stderr, "Illegal absolute path '%s'\n", path);
		exit(runtime_error);
	}

	strncpy(buf, path, sizeof(buf));

	loc = cp - path;
	if(sizeof(buf)-1 < loc) {
		loc = sizeof(buf)-1;
	}

	buf[loc] = '\0';
	curinit(buf);
}


/** Processes a directory specified using an absolute or deep
 * path.  We need to save and restore curpath to do this.
 *
 * It's illegal to call this routine with a path that doesn't
 * include at least one / character.
 *
 * @returns 1 if we should continue testing, 0 if we should abort.
 */

static int process_absolute_file(const char *path, int warn_suffix)
{
	struct cursave save;
	int keepontruckin;

	cursave(&save);
	init_absolute_filepath(path);

	keepontruckin = run_test(strrchr(path,'/')+1, path, warn_suffix);

	currestore(&save);
	return keepontruckin;
}


static void init_path(const char *base, const char *path)
{
	char buf[PATH_MAX];

	strncpy(buf, curabsolute(), PATH_MAX);
	strncat(buf, "/", PATH_MAX);
	strncat(buf, path, PATH_MAX);
	buf[PATH_MAX-1] = '\0';

	normalize_absolute_path(buf);

	curinit(buf);
}



/**
 * This is actually a hassle.  The user may have specified
 * "../.." which means we need to normalize an absolute path
 * and use that.
 */

static int process_deep_file(const char *path, int warn_suffix)
{
	struct cursave save;
	int keepontruckin;

	cursave(&save);
	init_path(orig_cwd, path);
	curpop(1);	// get rid of the filename

	keepontruckin = run_test(strrchr(path,'/')+1, path, warn_suffix);

	currestore(&save);
	return keepontruckin;
}


// forward declaration for recursion
int process_dir();

static int process_absolute_dir(const char *path)
{
	struct cursave save;
	int keepontruckin;

	cursave(&save);
	curinit(path);

	// we are certain the fullpath has already been normalized.
	// no need to do it again.

	if(outmode == outmode_test) {
		printf("\nProcessing %s\n", path);
	}
	keepontruckin = process_dir();

	currestore(&save);

	return keepontruckin;
}


static void print_relative_dir()
{
	char buf[PATH_MAX];

	if(arg_was_absolute) {
		printf("\nProcessing %s\n", curabsolute());
	} else {
		if(!abs2rel(curabsolute(), orig_cwd, buf, PATH_MAX)) {
			printf("Path couldn't be converted \"\%s\"\n", curabsolute());
			exit(runtime_error);
		}
		printf("\nProcessing ./%s\n", buf);
	}
}


/**
 * This routine used to be a simple "push the dir onto curpath,
 * run, and pop" affair.  Now, with ".." being fairly nontrivial,
 * we just need to save and restore.  Arg.  But at least this
 * works.
 *
 * The relative path can't be normalied because it might be
 * simply "..".  Therefore, it's one above whatever the cwd
 * is.  Gotta figure that out at runtime.
 */

static int process_relative_dir(const char *path)
{
	struct cursave save;
	int keepontruckin;

	cursave(&save);
	init_path(curabsolute(), path);

	if(outmode == outmode_test) {
		print_relative_dir();
	}
	keepontruckin = process_dir();

	currestore(&save);

	return keepontruckin;
}


/** Process all entries in a directory.
 *
 * @param is_topmost True if we are not recursing.  This allows us to
 * tell whether we should display pathnames absolute or relative
 * (if the user specified them relative on the command line, we
 * show them relative, and vice versa).
 */

static int process_ents(char **ents, int is_topmost)
{
	struct stat st;
    mode_t *modes;
    int i, n;
	int keepontruckin;

    for(n=0; ents[n]; n++)
        ;

    modes = malloc(n * sizeof(mode_t));
    if(!modes) {
        fprintf(stderr, "Could not allocate %d mode_t objects.\n", n);
        exit(runtime_error);
    }
    
    // first collect the stat info for each entry
    for(i=0; i<n; i++) {
        if(!is_dash(ents[i])) {
			const char *cp = ents[i];
			int keep = 0;
			if(ents[i][0] != '/') {
				keep = curpush(ents[i]);
				if(keep <= 0) {
					fprintf(stderr, "Path is too long.");
					exit(runtime_error);
				}
				cp = curabsolute();
			}
			// Need to be careful to test that file does exist.
			// Bash opens it, not us, so the error message might
			// be confusing.
			if(!verify_readable(cp,&st,0)) {
                exit(runtime_error);
            }
			if(ents[i][0] != '/') curpop(keep);
            modes[i] = st.st_mode;
        }
    }

    // process all files in dir
    for(i=0; i<n; i++) {
        if(is_dash(ents[i]) || S_ISREG(modes[i])) {
			if(ents[i][0] == '/') {
				// we know the path has already been fully normalized.
				keepontruckin = process_absolute_file(ents[i], is_topmost);
			} else if(strchr(ents[i], '/')) {
				keepontruckin = process_deep_file(ents[i], is_topmost);
			} else {
				keepontruckin = run_test(ents[i], ents[i], is_topmost);
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
			if(is_topmost) {
				// this is an unfortunate hack.  we display the path the same
				// way the user specified (absolute or relative) so we need
				// to remember which one it is.
				arg_was_absolute = (ents[i][0] == '/');
			}
			if(ents[i][0] == '/') {
				keepontruckin = process_absolute_dir(ents[i]);
			} else {
				keepontruckin = process_relative_dir(ents[i]);
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


/** Runs all tests in the current directory and all its subdirectories.
 */

int process_dir()
{
    char **ents;
    int i;
	int keepontruckin;

    ents = qscandir(curabsolute(), select_nodots, qdirentcoll);
    if(!ents) {
        // qscandir has already printed the error message
        exit(runtime_error);
    }

    keepontruckin = process_ents(ents, 0);

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

	if(curinit(NULL) != 0) {
		fprintf(stderr, "Could not get the cwd: %s\n", strerror(errno));
		exit(initialization_error);
	}

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
				run_unit_tests(all_unit_tests);
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


/*
 * I wish I could use canonicalize_path(3), but that routine resolves
 * symbolic links and provides no way to turn that behavior off.
 * How stupid!  This isn't as much of a hack as it looks because
 * "./../.." still needs to resolve to a real directory.
 *
 * @param original: the path to be normalized
 * @param outpath: the normalized path.  this may or may not be the same
 *     as original.
 */

static void normalize_path(char *original, char **outpath)
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
        strncpy(buf, curabsolute(), PATH_MAX);
		strncat(buf, "/", PATH_MAX);
		strncat(buf, original, PATH_MAX);
		buf[PATH_MAX-1] = '\0';
		normalize_absolute_path(buf);

		// convert it back to a relative path so it prints the
		// way the user intends.  We need to beware later on
		// to trim .. from the leading path.
		if(!abs2rel(buf, curabsolute(), normalized, PATH_MAX)) {
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

static void process_argv(char **argv)
{
	char **ents;
	int i, n;

    for(n=0; argv[n]; n++) { }

	ents = malloc((n+1)*sizeof(*ents));
	ents[n] = NULL;

	for(i=0; i<n; i++) { normalize_path(argv[i], &ents[i]); }
	process_ents(ents, 1);
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
	orig_cwd = dup_cwd();
	process_args(argc, argv);

    start_tests();
    if(optind < argc) {
		process_argv(argv+optind);
    } else {
        if(outmode == outmode_test) {
            printf("\nProcessing .\n");
        }
        process_dir();
    }
    stop_tests();

    if(outmode == outmode_test) {
        print_test_summary(&test_start_time, &test_stop_time);
    }

	free((char*)orig_cwd);
	return test_get_exit_value();
}

