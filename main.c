/* main.c
 * 28 Dec 2004
 * Copyright (C) 2004 Scott Bronson
 * This entire file is covered by the GNU GPL V2.
 * 
 * The main routine for tmtest.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <dirent.h>
#include <assert.h>

#include "test.h"
#include "qscandir.h"
#include "vars.h"
#include "r2read-fd.h"
#include "tfscan.h"

#define DIFFPROG "/usr/bin/diff"
#define SH   "/bin/bash"

// debugging flags:
#define DUMP_EXEC 0


enum {
    outmode_test,
    outmode_dump,
    outmode_diff
};

int outmode;
int verbose = 0;
double timeout = 10.0;		// timeout in seconds

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



// exit values:
enum {
    no_error = 0,
    argument_error,
    runtime_error,
    interrupted_error,
    internal_error,
	initialization_error,
};


#define xstringify(x) #x
#define stringify(x) xstringify(x)


#define is_dash(s) ((s)[0] == '-' && (s)[1] == '\0')


void add_define(char *def)
{
    printf("Adding define %s\n", def);
}


/** Returns zero if s1 ends with s2, nonzero if not.
 */

int strcmpend(const char *s1, const char *s2)
{
    size_t n1 = strlen(s1);
    size_t n2 = strlen(s2);

    if(n2 <= n1) {
        return strncmp(s1+n1-n2, s2, n2);
    } else {
        return 1;
    }
}


/** Prints the given template to the given file, performing substitutions.
 */

void print_template(struct test *test, const char *tmpl,  FILE *fp)
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


void reset_fd(int fd, const char *fname)
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


int wait_for_child(int child, const char *name)
{
    int pid;
    int signal;
    int status;

    // wait patiently for child to finish.
    pid = waitpid(child, &status, 0);
    if(pid < 0) {
        fprintf(stderr, "Error waiting for %s to finish: %s\n",
                strerror(errno), name);
        exit(runtime_error);
    }

    if(WIFSIGNALED(status)) {
        signal = WTERMSIG(status);
        if(signal == SIGINT) {
            // If test was interrupted with a sigint then raise it on ourselves.
            // Otherwise it can be hard to interrupt a test battery.
			kill(getpid(), SIGINT);
        }
        // it's probably a SIGABRT if child hit an assertion.
        // we'll just return 256.
        return 256;
    }

    if(!WIFEXITED(status)) {
        fprintf(stderr, "Unknown status returned by %s: %d\n", name, status);
        exit(runtime_error);
    }

    return WEXITSTATUS(status);
}


int open_file(char *fn, const char *name, int flags)
{
	strcpy(fn, g_testdir);
	strcat(fn, "/");
	strcat(fn, name);

    int fd = open(fn, flags|O_RDWR|O_CREAT/*|O_EXCL*/, S_IRUSR|S_IWUSR);

    if(fd < 0) {
        fprintf(stderr, "couldn't open %s: %s\n", fn, strerror(errno));
        exit(runtime_error);	// TODO
    }

    return fd;
}


int write_stdin_to_tmpfile(struct test *test)
{
	char *buf;
	int fd;

	buf = malloc(sizeof(TESTDIR) + sizeof(DIFFNAME));
	if(!buf) {
		perror("malloc");
		exit(10);	// TODO
	}

	test->diffname = buf;
	fd = open_file(buf, DIFFNAME, 0);
	assert(strlen(buf) == sizeof(TESTDIR)+sizeof(DIFFNAME)-1);
	write_raw_file(fd, 0);

	return fd;
}


/** Forks off a diff process and sets it up to receive the dumped test.
 */

int start_diff(struct test *test)
{
    int pipes[2];
    int child;
	const char *filename = test->testfilename;

    assert(test->testfilename);
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

    // fork child process
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

void finish_diff(struct test *test, int diffpid)
{
    close(test->rewritefd);

    int status = wait_for_child(diffpid, "diff");

    if(status != 0 && status != 1) {
        fprintf(stderr, "diff returned %d!\n", status);
        exit(runtime_error);
    }
}


/** Runs the named testfile.
 *
 * If warn_suffix is true and the ffilename doesn't end in ".test"
 * then we'll print a warning to stderr.  This is used when
 * processing the cmdline args so the user will know why a file
 * explicitly named didn't run.
 *
 * When config files are executing, they use the standard stdout
 * and stderr.  That way, the user sees any output while the test
 * is running (should help with debugging).  However, when the
 * test itself is running, its output is redirected into outfd/errfd.
 *
 * It may appear that outmode_dump mixes stdio and Unix I/O, but it
 * doesn't really.  We only print to stdio when testing, and we only
 * dump the file when dumping.  They cannot both happen simultaneously.
 */

void run_test(const char *name, int warn_suffix)
{
    struct test test;
    char buf[BUFSIZ];   // scan buffer for the testfile
    int pipes[2];
    int child;
    int diffpid;
    int fd = -1;
    FILE *tochild;

    // defined in the exec.c file generated by exec.tmpl.
    extern const char exec_template[];

    if(!is_dash(name) && strcmpend(name, ".test") != 0) {
        if(warn_suffix) {
            fprintf(stderr, "%s was skipped because it doesn't end in '.test'.\n", name);
        }
        return;
    }

    test_init(&test);
    test.testfilename = name;
    test.outfd = g_outfd;
    test.errfd = g_errfd;
    test.statusfd = g_statusfd;

    // initialize the test mode
    switch(outmode) {
        case outmode_test:
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
        execl(SH, SH, "-s", (char*)NULL);
        perror("executing " SH " for test");
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
        fd = open(name, O_RDONLY);
        if(fd < 0) {
            fprintf(stderr, "Could not open %s: %s\n",
                    name, strerror(errno));
            exit(runtime_error); // TODO
        }
        readfd_attach(&test.testfile, fd);
    }
    tfscan_attach(&test.testfile);

    // a little debugging code
    if(DUMP_EXEC) {
        print_template(&test, exec_template, stdout);
        exit(0);  // screw the kid
    }

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
    test.exitno = wait_for_child(child, "test");

    // process the test results
    switch(outmode) {
        case outmode_test:
            test_results(&test);
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
    test_free(&test);
}


/** This routine filters out any dirents that begin with '.'.
 *  We don't want to process any hidden files or special directories.
 */

int select_nodots(const struct dirent *d)
{
    return d->d_name[0] != '.';
}


// forward declaration for recursion
void process_dir();


/** Process all entries in a directory.
 *
 * See run_test() for an explanation of warn_suffix.
 */

void process_ents(char **ents, int warn_suffix)
{
    static char pathbuf[PATH_MAX];
    struct stat st;
    mode_t *modes;
    int i, n;
    char *slash;

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
            if(stat(ents[i], &st) < 0) {
                fprintf(stderr, "%s: %s\n", ents[i], strerror(errno));
                exit(runtime_error);
            }
            modes[i] = st.st_mode;
        }
    }

    // process all files in dir
    for(i=0; i<n; i++) {
        if(is_dash(ents[i]) || S_ISREG(modes[i])) {
            run_test(ents[i], warn_suffix);
            modes[i] = 0;
        }
    }

    // process all subdirs
    for(i=0; i<n; i++) {
        if(is_dash(ents[i]) || modes[i] == 0) continue;
        if(S_ISDIR(modes[i])) {
            chdir(ents[i]);
            if(pathbuf[0]) strncat(pathbuf, "/", sizeof(pathbuf));
            strncat(pathbuf, ents[i], sizeof(pathbuf));
            if(outmode == outmode_test) {
                printf("\nProcessing %s\n", pathbuf);
            }
            process_dir();
            slash = strrchr(pathbuf, '/');
            if(slash) {
                *slash = '\0';
            } else {
                pathbuf[0] = '\0';
            }
            chdir("..");
        }
    }

    free(modes);
}


/** Runs all tests in the current directory and all its subdirectories.
 */

void process_dir()
{
    char **ents;
    int i;

    ents = qscandir(".", select_nodots, qdirentcoll);
    if(!ents) {
        // qscandir has already printed the error message
        exit(runtime_error);
    }

    process_ents(ents, 0);

    for(i=0; ents[i]; i++) {
        free(ents[i]);
    }
    free(ents);
}


static void checkerr(int err, const char *op, const char *name)
{
	if(err < 0) {
		fprintf(stderr, "There was an error %s %s: %s\n",
				op, name, strerror(errno));
		// not much else we can do other than complain...
	}
}


void stop_tests()
{
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

void start_tests()
{
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
}


void usage()
{
	printf(
			"Usage: tmtest [OPTION]... [DLDIR]\n"
			"  -o: output the test file with the new output.\n"
			"  -d: output a diff between the expected and actual outputs.\n"
            "  -D NAME=VAL: define a variable on the command line.\n"
            "  -t --timeout: time in seconds before test is terminated.\n"
			"  -v --verbose: increase verbosity.\n"
			"  -V --version: print the version of this program.\n"
			"  -h --help: prints this help text\n"
			"Run tmtest with no arguments to run all tests in the current directory.\n"
		  );
}


void process_args(int argc, char **argv)
{
    char buf[256], *cp;
    int optidx, i, c;

	while(1) {
		optidx = 0;
		static struct option longopts[] = {
			// name, has_arg (1=reqd,2=opt), flag, val
			{"diff", 0, 0, 'd'},
			{"define", 1, 0, 'D'},
			{"help", 0, 0, 'h'},
			{"output", 0, 0, 'o'},
			{"timeout", 1, 0, 't'},
			{"verbose", 0, 0, 'v'},
			{"version", 0, 0, 'V'},
			{0, 0, 0, 0},
		};

        // dynamically create the option string from the long
        // options.  Why oh why doesn't glibc do this for us???
        cp = buf;
        for(i=0; longopts[i].name; i++) {
            *cp++ = longopts[i].val;
            if(longopts[i].has_arg > 0) *cp++ = ':';
            if(longopts[i].has_arg > 1) *cp++ = ':';
        }
        *cp++ = '\0';

		c = getopt_long(argc, argv, buf, longopts, &optidx);
		if(c == -1) break;

		switch(c) {
            case 'd':
                outmode = outmode_diff;
                break;

			case 'D':
                add_define(optarg);
				break;

			case 'h':
				usage();
				exit(0);

			case 'o':
                outmode = outmode_dump;
				break;

			case 't':
				sscanf(optarg, "%lf", &timeout);
				break;

			case 'v':
				verbose++;
				break;

			case 'V':
				printf("tmtest version %s\n", stringify(VERSION));
				exit(0);

                /*
			case 0:
			case '?':
				break;
                */

			default:
				exit(argument_error);
		}
	}

	if(verbose) {
        switch(outmode) {
            case outmode_test:
                printf("Running tests.\n");
                break;
            case outmode_dump:
                printf("Dumping tests.\n");
                break;
            case outmode_diff:
                printf("Diffing tests.\n");
                break;
            default:
                assert(0);
        }
		printf("Timeout is %lf seconds\n", timeout);
	}
}


int main(int argc, char **argv)
{
	process_args(argc, argv);

    start_tests();
    if(optind < argc) {
        process_ents(argv+optind, 1);
    } else {
        if(outmode == outmode_test) {
            printf("\nProcessing .\n");
        }
        process_dir();
    }
    stop_tests();

    if(outmode == outmode_test) {
        print_test_summary();
    }

	return 0;
}

