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

        execl(DIFFPROG, DIFFPROG, "-u", test->testfile, "-", (char*)NULL);
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

static int run_test(const char *name, int warn_suffix)
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

    test.testfile = name;
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


/** Processes a directory specified using an absolute or deep
 * path.  We need to save and restore curpath to do this.
 *
 * It's illegal to call this routine with a path that doesn't
 * include at least one / character.
 *
 * @returns 1 if we should continue testing, 0 if we should abort.
 */

static int process_absolute_file(const char *abspath, int warn_suffix)
{
    char buf[PATH_MAX];
    struct pathstack stack;

    if(pathstack_init(&stack, buf, sizeof(buf), abspath) != 0) {
        fprintf(stderr, "path too long: %s\n", abspath);
        exit(runtime_error);
    }

    // this hack needs to go away
    normalize_absolute_path(stack.buf);
    stack.curlen = strlen(stack.buf);

    return run_test(pathstack_absolute(&stack), warn_suffix);
}


// forward declaration for recursion
int process_dir(struct pathstack *ps, int print_absolute);


static int process_absolute_dir(const char *abspath, const char *origpath, int print_absolute)
{
    char buf[PATH_MAX];
    struct pathstack stack;

    if(pathstack_init(&stack, buf, sizeof(buf), abspath) != 0) {
        fprintf(stderr, "path too long: %s\n", abspath);
        exit(runtime_error);
    }

    // this hack needs to go away
    normalize_absolute_path(stack.buf);
    stack.curlen = strlen(stack.buf);

    return process_dir(&stack, print_absolute);
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
    int i, n;
    int keepontruckin = 1;

    for(n=0; ents[n]; n++) { } // count n

    modes = malloc(n * sizeof(mode_t));
    if(!modes) {
        fprintf(stderr, "Could not allocate %d mode_t objects.\n", n);
        exit(runtime_error);
    }

    // first collect the stat info for each entry and perform a quick sanity check
    for(i=0; i<n; i++) {
        if(!is_dash(ents[i])) {
            const char *cp = ents[i];

            if(strcmp(ents[i], ".tmtest-ignore") == 0) {
                // skip this directory but continue processing
                goto abort;
            }

            if(ents[i][0] != '/') {
                if(pathstack_push(ps, ents[i], &save) != 0) {
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
                assert(print_absolute == -1); // it should be impossible to get here if we've already recursed
                keepontruckin = process_absolute_file(ents[i], 1);
            } else {
                if(pathstack_push(ps, ents[i], &save) != 0) {
                    fprintf(stderr, "path too long: %s\n", ents[i]);
                    exit(runtime_error);
                }
                keepontruckin = process_absolute_file(pathstack_absolute(ps), print_absolute == -1);
                pathstack_pop(ps, &save);
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
                assert(print_absolute == -1); // it should be impossible to get here if we've already recursed
                keepontruckin = process_absolute_dir(ents[i], ents[i], 1);
            } else {
                if(print_absolute == -1) print_absolute = 0;
                if(strchr(ents[i], '.') || strchr(ents[i], '/')) {
                    // if there are potential non-normals in the path, we need to normalize it.
                    if(pathstack_push(ps, ents[i], &save)) {
                        fprintf(stderr, "path too long: %s\n", ents[i]);
                        exit(runtime_error);
                    }
                    keepontruckin = process_absolute_dir(pathstack_absolute(ps), ents[i], print_absolute);
                    pathstack_pop(ps, &save);
                } else {
                    // Otherwise, we just push the path and chug.
                    if(pathstack_push(ps, ents[i], &save)) {
                        fprintf(stderr, "path too long: %s\n", ents[i]);
                        exit(runtime_error);
                    }
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
 *
 * NOTE: we DO allow .tmtest-ignore files through.  Otherwise, how
 * would the directory scanner know which directories to skip?
 */

static int select_nodots(const struct dirent *d)
{
    // common case: if the filename doesn't begin with a dot, use it.
    if(d->d_name[0] != '.') {
        return 1;
    }

    // if it's named '.tmtest-ignore', use it
    if(strcmp(d->d_name, ".tmtest-ignore") == 0) {
        return 1;
    }

    // otherwise, ignore it.
    return 0;
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
 */

static void normalize_path(struct pathstack *ps, char *original, char **outpath)
{
    char buf[PATH_MAX];
    char normalized[PATH_MAX];

    if(original[0] == '/') {
        int origlen = strlen(original);
        if(origlen > PATH_MAX - 1) {
            fprintf(stderr, "Path is too long: %s\n", original);
            exit(runtime_error);
        }
        memcpy(normalized, original, origlen);
        normalize_absolute_path(normalized);
    } else {
        cat_path(buf, pathstack_absolute(ps), original, sizeof(buf));
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

    for(n=0; argv[n]; n++) { }  // count n

    ents = malloc((n+1)*sizeof(*ents));
    ents[n] = NULL;

    for(i=0; i<n; i++) { normalize_path(ps, argv[i], &ents[i]); }
    oldlen = ps->curlen;
    process_ents(ps, ents, -1);
    assert(oldlen == ps->curlen); // process_ents needs to not modify the pathstack.
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

    if(pathstack_init(&pathstack, buf, sizeof(buf), orig_cwd)) {
        fprintf(stderr, "path too long: %s\n", orig_cwd);
        exit(runtime_error);
    }

    start_tests();
    if(optind < argc) {
        process_argv(&pathstack, argv+optind);
    } else {
        process_dir(&pathstack, 0);
    }
    stop_tests();

    if(outmode == outmode_test) {
        print_test_summary(&test_start_time, &test_stop_time);
    }

    free((char*)orig_cwd);
    return test_get_exit_value();
}

