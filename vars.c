/* vars.c
 * 29 Dec 2004
 * Copyright (C) 2004 Scott Bronson
 * This file is released under the MIT license.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <assert.h>

#include "test.h"
#include "vars.h"
#include "curdir.h"

#define CONFIG_FILE "tmtest.conf"
#define HOME_CONFIG_FILE ".tmtestrc"


/** @file vars.c
 *
 * Generates values for all the variables that appear in the
 * shell template.
 *
 * The functions in this file return errors only when there's a config
 * error (i.e. bad template variable, can't get user's login, etc).
 * They should ignore write errors.  They will happpen whenever the
 * test (or a config file) exits early and should properly be ignored.
 *
 * NOTE: this file is the only place in this project that we use
 * fileptrs.  Well, other than printing status information from main.c,
 * and, by proxy, the test_command_copy() routine.
 * Everywhere else we use Unix I/O.  Ensure they never mix.
 */

static int var_testfile(struct test *test, FILE* fp, const char *var)
{
	fprintf(fp, "%s/%s", curabsolute(), test->testfilename);
    return 0;
}

static int var_testdir(struct test *test, FILE* fp, const char *var)
{
	fputs(curabsolute(), fp);
    return 0;
}


static int var_testexec(struct test *test, FILE* fp, const char *var)
{
    // If the filename is a dash, it means we should feed the test
    // from stdin.  Otherwise, just have the shell execute the testfile.

    if(test->testfilename[0] == '-' && test->testfilename[1] == '\0') {
        test_command_copy(test, fp);
    } else {
        test_command_copy(test, NULL);
        fprintf(fp, ". '%s/%s'", curabsolute(), test->testfilename);
    }

    return 0;
}


static int var_author(struct test *test, FILE *fp, const char *var)
{
    char *u = getlogin();
    if(u) {
        fputs(u, fp);
    } else {
        // probably not using a login shell...?
        fputs("UNKNOWN", fp);
    }

    return 0;
}


static int var_date(struct test *test, FILE *fp, const char *var)
{
    time_t now;
    struct tm* tm;

    time(&now);
    tm = localtime(&now);
    fprintf(fp, "%d-%02d-%02d %2d:%02d:%02d",
            tm->tm_year+1900, tm->tm_mon, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);

    return 0;
}


static int var_outfd(struct test *test, FILE *fp, const char *var)
{
    fprintf(fp, "%d", test->outfd);
    return 0;
}

static int var_errfd(struct test *test, FILE *fp, const char *var)
{
    fprintf(fp, "%d", test->errfd);
    return 0;
}

static int var_statusfd(struct test *test, FILE *fp, const char *var)
{
    fprintf(fp, "%d", test->statusfd);
    return 0;
}


/** Returns the full path to the user's home directory.
 */

static char* get_home_dir()
{
	char *cp;
	struct passwd *entry;
	
	cp = getenv("HOME");
	if(cp) return cp;
	cp = getenv("LOGDIR");
	if(cp) return cp;

	entry = getpwuid(getuid());
	if(entry) {
		cp = entry->pw_dir;
		if(cp) return cp;
	}

	fprintf(stderr, "Could not locate your home directory!\n");
	exit(10);
}


/** Returns true if the given file exists, false if not.
 */

int file_exists(char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}



#define check_config_str(t,f,s,n) check_config((t),(f),(s),strlen(s),(n))


/** Checks to see if the file exists and, if it does, then it
 *  outputs the appropriate commands.
 *
 *  @see var_config_files()
 */

static void check_config(struct test *test, FILE *fp,
		const char *base, int len, const char *name)
{
	char buf[PATH_MAX];
	int namelen = strlen(name);
	
	// if the buffer isn't big enough then don't even try.
	if(len + namelen + 2 > sizeof(buf)) return;

	// assemble the file name
	memcpy(buf, base, len);
	buf[len] = '/';
	memcpy(buf+len+1, name, namelen);
	buf[len+1+namelen]='\0';

	if(file_exists(buf)) {
		fprintf(fp, "echo 'CONFIG: %s' >&%d\n", buf, test->statusfd);
		fprintf(fp, "MYDIR='%.*s'\nMYFILE='%s'\n. '%s'\n", len, buf, buf, buf);
	}
}


/** Prints the shell commands needed to read in all available config files.
 *
 * for instance:
 *
 *     echo 'CONFIG: /etc/tmtest' >&5
 *     . '/etc/tmtest'
 *     echo 'CONFIG: /home/bronson/tmtest/tmtest.conf' >&5
 *     . '/home/bronson/tmtest/tmtest.conf'
 *
 *     and so on...
 */

static int var_config_files(struct test *test, FILE *fp, const char *var)
{
	char buf[PATH_MAX];
    char *cp;

	check_config_str(test, fp, "/etc", CONFIG_FILE);
	check_config_str(test, fp, get_home_dir(), HOME_CONFIG_FILE);

	strncpy(buf, curabsolute(), sizeof(buf));
    for(cp=buf; (cp=strchr(cp,'/')); cp++) {
		check_config(test, fp, buf, cp-buf, CONFIG_FILE);
    }
	check_config_str(test, fp, buf, CONFIG_FILE);

    return 0;
}


/** Prints the value for variable varname to file fp.
 * Returns zero if successful, nonzero if not.
 */

int printvar(struct test *test, FILE *fp, const char *varname)
{
    int i;

    struct {
        char *name;
        int (*func)(struct test *test, FILE *fp, const char *var);
    } funcs[] = {
        { "AUTHOR",         var_author },
        { "CONFIG_FILES",   var_config_files },
        { "DATE",           var_date },
        { "OUTFD",          var_outfd },
        { "ERRFD",          var_errfd },
        { "STATUSFD",       var_statusfd },
        { "TESTFILE",       var_testfile },
        { "TESTEXEC",       var_testexec },
        { "TESTDIR",        var_testdir },
    };

    for(i=0; i<sizeof(funcs)/sizeof(funcs[0]); i++) {
        if(strcmp(varname, funcs[i].name) == 0) {
            if(funcs[i].func) {
                return (*funcs[i].func)(test, fp, varname);
            } else {
                fprintf(fp, "<<<%s>>>", varname);
                return 0;
            }
        }
    }

    fprintf(stderr, "Unknown variable '%s' in template.\n", varname);
    return 1;
}

