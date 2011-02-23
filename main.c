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

#define DIFFPROG "/usr/bin/diff"
#define SHPROG   "/bin/bash"


enum {
    outmode_test,
    outmode_dump,
    outmode_diff
};

/* configuration options */
int outmode = outmode_test;
int allfiles = 0;     // run a testfile even if it begins with a dash
int dumpscript = 0;   // print the script instead of running it
int quiet = 0;
int verbose = 0;
char *config_file;    // absolute path to the user-specified config file
                      // null if user didn't specify a config file.

const char *orig_cwd; // tmtest changes dirs before running a test

// The testdir contains fifos, tempfiles, etc for running the test.
#define TESTDIR "/tmp/tmtest-XXXXXX"
char g_testdir[sizeof(TESTDIR)];

#define OUTNAME "stdout"
#define ERRNAME "stderr"
#define STATUSNAME "status"
#define TESTHOME "test"
char g_outname[sizeof(TESTDIR)+sizeof(OUTNAME)];
char g_errname[sizeof(TESTDIR)+sizeof(ERRNAME)];
char g_statusname[sizeof(TESTDIR)+sizeof(STATUSNAME)];
char g_testhome[sizeof(TESTDIR)+sizeof(TESTHOME)];

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


static void copy_string(char *dst, const char *src, int dstsiz)
{
    dst[0] = '\0';
    strncat(dst, src, dstsiz - 1);
}


static void cat_path(char *dst, const char *s1, const char *s2, int siz)
{
    // copies s1 and s2 into dst, separated with a "/" character.
    // Generally s1 will be an absolute path and s2 will be relative.

    int s1len = strlen(s1);
    int s2len = strlen(s2);
    if(s1len + s2len + 2 > siz) {
        fprintf(stderr, "Path overflow!\n");
        exit(argument_error);
    }

    memcpy(dst, s1, s1len);
    dst[s1len] = '/';
    memcpy(dst + s1len + 1, s2, s2len + 1); // include null terminator
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


static int open_file(char *fn, int fnsiz, const char *name, int flags)
{
    int fd;

    cat_path(fn, g_testdir, name, fnsiz);
    fd = open(fn, flags|O_RDWR|O_CREAT/*|O_EXCL*/, S_IRUSR|S_IWUSR);
    if(fd < 0) {
        fprintf(stderr, "couldn't open %s: %s\n", fn, strerror(errno));
        exit(runtime_error);
    }

    return fd;
}


static void write_stdin_to_tmpfile(struct test *test)
{
    int diffsiz;
    int fd;

    diffsiz = sizeof(TESTDIR) + sizeof(DIFFNAME);
    test->diffname = malloc(diffsiz);
    if(!test->diffname) {
        perror("malloc");
        exit(runtime_error);
    }

    fd = open_file(test->diffname, diffsiz, DIFFNAME, 0);
    assert(strlen(test->diffname) == sizeof(TESTDIR)+sizeof(DIFFNAME)-1);
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

    assert(test->testfile);
    // if the test is coming from stdin, we need to copy it to a
    // real file before we can diff against it.
    if(is_dash(test->testfile)) {
        // first, write all of our stdin to a tmpfile.
        write_stdin_to_tmpfile(test);
        // then, read the test from this file instead of stdin.
        filename = test->diffname;
        assert(filename);
        assert(filename[0]);
    } else {
        filename = test->testpath;
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


static int open_test_file(struct test *test)
{
    int fd;

    // If the filename is a dash then we just use stdin.
    if(is_dash(test->testfile)) {
        return STDIN_FILENO;
    }

    fd = open(test->testfile, O_RDONLY);
    if(fd < 0) {
        fprintf(stderr, "Could not open %s: %s\n", test->testfile, strerror(errno));
        exit(runtime_error);
    }

    return fd;
}


static int select_no_pseudo_dirs(const struct dirent *d)
{
    if(d->d_name[0] == '.' && d->d_name[1] == '\0') return 0;
    if(d->d_name[0] == '.' && d->d_name[1] == '.' && d->d_name[2] == '\0') return 0;
    return 1;
}


static int remove_subdirs(struct test *test, struct pathstack *stack, char *start, char *msg, int msgsiz)
{
    struct stat st;
    char **ents;
    char *path;
    struct pathstate state;
    int count = 0;
    int i, subcnt;

    ents = qscandir(pathstack_absolute(stack), select_no_pseudo_dirs, qdirentcoll);
    if(!ents) {
        // qscandir has already printed the error message
        exit(runtime_error);
    }

    for(i=0; ents[i]; i++) {
        count += 1;
        subcnt = 0;

        if(pathstack_push(stack, ents[i], &state) != 0) {
            fprintf(stderr, "path too long: %s\n", ents[i]);
            exit(runtime_error);
        }
        path = pathstack_absolute(stack);

        if(stat(path, &st) < 0) {
            test_abort(test, "Could not locate %s: %s\n", path, strerror(errno));
        }

        if(S_ISDIR(st.st_mode)) {
            subcnt = remove_subdirs(test, stack, start, msg, msgsiz);
            if(rmdir(path) < 0) {
                test_abort(test, "Could not rmdir %s: %s\n", path, strerror(errno));
            }
        } else {
            if(unlink(path) < 0) {
                test_abort(test, "Could not unlink %s: %s\n", path, strerror(errno));
            }
        }

        if(subcnt == 0) {
            // only add a dir to the message if we didn't remove any subdirs
            // we don't care if there's truncation here since it's only being
            // displayed to the user and the dirs are being deleted anyway.
            if(msg[0]) {
                strncat(msg, ", ", msgsiz - strlen(msg) - 1);
            } else {
                strncat(msg, "not deleted: ", msgsiz - strlen(msg) - 1);
            }
            strncat(msg, start, msgsiz - strlen(msg) - 1);
        }

        pathstack_pop(stack, &state);
        free(ents[i]);
    }

    free(ents);
    return count;
}


/** Ensures no files or dirs were left behind in the testhome.
 *  If there were, it deletes them and marks the test as failed.
 */

static void check_testhome(struct test *test)
{
    char buf[PATH_MAX];
    struct pathstack stack;
    char message[BUFSIZ];

    if(pathstack_init(&stack, buf, sizeof(buf), g_testhome) != 0) {
        fprintf(stderr, "path too long: %s\n", g_testhome);
        exit(runtime_error);
    }
    message[0] = '\0';
    remove_subdirs(test, &stack, buf+strlen(g_testhome)+1, message, sizeof(message));

    if(message[0] && test->status == test_was_started) {
        test->status = test_has_failed;
        test->status_reason = strdup(message);
    }
}


// quick sanity check to be absolutely certain we're not starting
// a test with files and dirs left over from a previous run.
static void verify_testhome(struct test *test)
{
    DIR *directory;
    struct dirent *entry;

    directory = opendir(g_testhome);
    if(directory == NULL) {
        test_abort(test, "Could not open directory '%s': %s\n",
                g_testhome, strerror(errno));
    }

    while((entry = readdir(directory)) != NULL) {
        if(select_no_pseudo_dirs(entry)) {
            test_abort(test, "Almost started test with files in %s: %s", g_testhome, entry->d_name);
        }
    }

    closedir(directory);
}


int valid_filename(const char *name)
{
    return is_dash(name) || allfiles || strcmpend(name, ".test") == 0;
}


/** Runs the named testfile.
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

static int run_test(const char *abspath, const char *relpath)
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

    if(!valid_filename(abspath)) {
        return 1;
    }

    // so that we can safely single quote filenames in the shell.
    if(strchr(abspath, '\'') || strchr(abspath, '"')) {
        fprintf(stderr, "%s was skipped because its file name contains a quote character.\n", relpath);
        return 1;
    }

    test_init(&test);
    if(setjmp(abort_jump)) {
        // test was aborted.
        fprintf(stderr, "Test aborted: %s\n", test.status_reason);
        exit(runtime_error);
    }

    test.testfile = relpath;
    test.testpath = abspath;
    test.outfd = g_outfd;
    test.errfd = g_errfd;
    test.statusfd = g_statusfd;

    verify_testhome(&test);

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

        if(chdir(g_testhome) != 0) {
            fprintf(stderr, "Could not chdir 2 to %s: %s\n", g_testhome, strerror(errno));
            exit(runtime_error);
        }

        execl(SHPROG, SHPROG, "-s", (char*)NULL);
        perror("executing " SHPROG " for test");
        exit(runtime_error);
    }

    // create the testfile scanner.  it will either scan from
    // the testfile itself or from stdin if filename is "-".
    scanstate_init(&test.testscanner, buf, sizeof(buf));
    if(test.diffname) {
        if(lseek(test.diff_fd, 0, SEEK_SET) < 0) {
            fprintf(stderr, "Couldn't seek to start of %s: %s\n",
                    test.diffname, strerror(errno));
            exit(runtime_error);
        }
        readfd_attach(&test.testscanner, test.diff_fd);
    } else {
        readfd_attach(&test.testscanner, open_test_file(&test));
    }
    tfscan_attach(&test.testscanner);

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
        check_testhome(&test);

        // process and output the test results
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

        keepontruckin = !was_aborted(test.status);
    }

    usleep(10000); // TODO: this is really weird.  it slows us way down.  Get rid of it!!!
        // without this we get "shell-init: error retrieving current directory: getcwd: cannot access parent directories: No such file or directory"
        // get rid of this when switching to event based handling.

    test_free(&test);

    return keepontruckin;
}


int process_file(const char *path, int print_absolute)
{
    char buf[PATH_MAX];

    if(print_absolute) {
        return run_test(path, path);
    }

    // We do the treewalk using absolute paths so that ../.. and friends
    // don't mess us up and we don't walk off the end of a string.
    // Need to convert back to relative before running the test.

    if(!abs2rel(path, orig_cwd, buf, sizeof(buf))) {
        fprintf(stderr, "Could not convert %s to relative from %s\n", path, orig_cwd);
        exit(runtime_error);
    }

    return run_test(path, buf);
}


int is_file(const char *path)
{
    struct stat st;

    if(stat(path, &st) < 0) {
        fprintf(stderr, "Could not locate %s: %s\n", path, strerror(errno));
        exit(runtime_error);
    }

    if(!i_have_permission(&st, 0444)) {
        fprintf(stderr, "Could not open %s: permission denied!\n", path);
        exit(runtime_error);
    }

    return S_ISREG(st.st_mode);
}


/** This routine filters out any dirents that begin with '.'.
 *  We don't want to process any hidden files or special directories.
 */

static int select_nodots(const struct dirent *d)
{
    // common case: if the filename doesn't begin with a dot, use it.
    if(d->d_name[0] != '.') {
        return 1;
    }

    // otherwise, ignore it.
    return 0;
}


void shift_down(char **entry)
{
    do {
        entry[0] = entry[1];
    } while(*entry++);
}


/** Runs all tests in the current directory and all its subdirectories.
 *  This routine operates on absolute paths and then converts back
 *  to relative so that paths are always normalized properly.
 */

int process_directory(struct pathstack *ps, int print_absolute)
{
    int keepontruckin = 1;
    char **entries, **entry;
    struct pathstate save;

    entries = qscandir(pathstack_absolute(ps), select_nodots, qdirentcoll);
    if(!entries) {
        // qscandir has already printed the error message
        exit(runtime_error);
    }

    // first process files in dir
    for(entry=entries; *entry && keepontruckin; ) {
        if(pathstack_push(ps, *entry, &save) != 0) {
            fprintf(stderr, "path too long: %s\n", *entry);
            exit(runtime_error);
        }
        if(is_file(pathstack_absolute(ps))) {
            keepontruckin = process_file(pathstack_absolute(ps), print_absolute);
            free(*entry);
            shift_down(entry);
        } else {
            entry += 1;
        }
        pathstack_pop(ps, &save);
    }

    // then process the subdirectories
    for(entry=entries; *entry; entry++) {
        if(keepontruckin) {
            if(pathstack_push(ps, *entry, &save) != 0) {
                fprintf(stderr, "path too long: %s\n", *entry);
                exit(runtime_error);
            }
            keepontruckin = process_directory(ps, print_absolute);
            pathstack_pop(ps, &save);
        }
        free(*entry);
    }

    free(entries);
    return keepontruckin;
}


static int process_path(const char *path)
{
    char buf[PATH_MAX];
    struct pathstack stack;
    int print_absolute = 0;

    if(is_dash(path)) {
        return run_test(path, path);
    }

    if(path[0] == '/') {
        print_absolute = 1;
        if(pathstack_init(&stack, buf, sizeof(buf), path)) {
            fprintf(stderr, "path too long: %s\n", path);
            exit(runtime_error);
        }
    } else {
        if(pathstack_init(&stack, buf, sizeof(buf), orig_cwd)) {
            fprintf(stderr, "path too long: %s\n", orig_cwd);
            exit(runtime_error);
        }
        pathstack_push(&stack, path, NULL);
    }

    // nasty hack to normalize the path
    normalize_absolute_path(buf);
    stack.curlen = strlen(buf);

    if(is_file(buf)) {
        // make sure user gets a reason if a test is explicitly named on the cmdline but not run
        if(!valid_filename(path)) {
            fprintf(stderr, "%s was skipped because it doesn't end in '.test'.\n", path);
        }

        return process_file(buf, print_absolute);
    } else {
        return process_directory(&stack, print_absolute);
    }
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

    // the test already ensured this dir is empty
    checkerr(rmdir(g_testhome), "deleting", g_testhome);

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
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sig_int);

    memcpy(g_testdir, TESTDIR, sizeof(g_testdir));
    if(!mkdtemp(g_testdir)) {
        fprintf(stderr, "Could not call mkdtemp() on %s: %s\n", g_testdir, strerror(errno));
        exit(initialization_error);
    }

    // errors are handled by open_file.
    g_outfd = open_file(g_outname, sizeof(g_outname), OUTNAME, 0);
    assert(strlen(g_outname) == sizeof(g_outname)-1);
    g_errfd = open_file(g_errname, sizeof(g_errname), ERRNAME, 0);
    assert(strlen(g_errname) == sizeof(g_errname)-1);
    g_statusfd = open_file(g_statusname, sizeof(g_statusname), STATUSNAME, O_APPEND);
    assert(strlen(g_statusname) == sizeof(g_statusname)-1);

    cat_path(g_testhome, g_testdir, TESTHOME, sizeof(g_testhome));

    if(mkdir(g_testhome, 0700) < 0) {
        fprintf(stderr, "couldn't create %s: %s\n", g_testhome, strerror(errno));
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
        copy_string(buf, cfg, sizeof(buf));
    } else {
        cat_path(buf, orig_cwd, cfg, sizeof(buf));
    }

    normalize_absolute_path(buf);

    // need to ensure as well as we can that the file is readable because
    // we don't open it ourselves.  Bash does.  And that can lead to some
    // really cryptic error messages.
    if(!is_file(buf)) {
        fprintf(stderr, "Could not open %s: not a file!\n", buf);
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
            "  -q --quiet: be quiet when running tests\n"
            "  -v --verbose: print more when running tests\n"
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
        {"verbose", 0, 0, 'v'},
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

            case 'h':
                usage();
                exit(0);

            case 'o':
                outmode = outmode_dump;
                break;

            case 'q':
                quiet++;
                break;

            case 'v':
                verbose++;
                break;

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


static const char* dup_cwd()
{
    char buf[PATH_MAX];

    // The Gnu version of getcwd can do this for us but that's not portable.
    if(!getcwd(buf, PATH_MAX)) {
        perror("Couldn't get current working directory");
        exit(runtime_error);
    }

    return strdup(buf);
}


void start_treewalk()
{
    char buf[PATH_MAX];
    struct pathstack pathstack;

    if(pathstack_init(&pathstack, buf, sizeof(buf), orig_cwd)) {
        fprintf(stderr, "path too long: %s\n", orig_cwd);
        exit(runtime_error);
    }

    process_directory(&pathstack, 0);
}


int main(int argc, char **argv)
{
    orig_cwd = dup_cwd();
    process_args(argc, argv);
    argv += optind;

    start_tests();
    if(optind < argc) {
        for(; *argv; argv++) {
            if(!process_path(*argv)) break;
        }
    } else {
        start_treewalk();
    }
    stop_tests();

    if(outmode == outmode_test) {
        print_test_summary(&test_start_time, &test_stop_time);
    }

    free((char*)orig_cwd);
    return test_get_exit_value();
}

