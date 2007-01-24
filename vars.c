/* vars.c
 * Scott Bronson
 * 29 Dec 2004
 *
 * Interpolates $(VARS) in the test template (template.sh).
 * This file is covered by the MIT License.
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
	if(test->testfilename[0] == '-' && test->testfilename[1] == '\0') {
		fprintf(fp, "(STDIN)");
	} else if(test->testfilename[0] == '/') {
		fprintf(fp, "%s", test->testfilename);
	} else {
		fprintf(fp, "%s/%s", curabsolute(), test->testfilename);
	}

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
		// bash3 doesn't support setting LINENO anymore.  Bash2 did.
		// what the hell, it's worth a shot.
		fprintf(fp, "LINENO=0\n");
        test_command_copy(test, fp);
    } else {
        test_command_copy(test, NULL);
		if(test->testfilename[0] == '/') {
			fprintf(fp, ". %s", test->testfilename);
		} else {
			fprintf(fp, ". '%s/%s'", curabsolute(), test->testfilename);
		}
    }

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

static char* get_home_dir(struct test *test)
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

	test_abort(test, "Could not locate your home directory!  Please set $HOME.\n");
	return NULL; // will never be executed
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
 *  @param base The path.
 *  @param len The number of characters from base to use.
 *  @param name The filename.  It will be concatenated with a '/'
 *  onto the end of base.  Optional: if name is null then base will
 *  be used directly.  This is a 0-terminated string.
 *
 *  @see var_config_files()
 */

static void check_config(struct test *test, FILE *fp,
		const char *base, int len, const char *name)
{
	char buf[PATH_MAX];
	
	// if the buffer isn't big enough then don't even try.
	if(len+(name?strlen(name):0)+2 > sizeof(buf)) return;

	// assemble the file name
	memcpy(buf, base, len);
	buf[len]='\0';

	// append the filename if supplied
	if(name) {
		buf[len] = '/';
		buf[len+1] = 0;
		strcat(buf+len+1, name);
	}

	if(config_file && strcmp(buf,config_file) == 0) {
		// If buf == config_file then it means the user must have
		// specified a config file within the current search path.
		// This ensures that we don't include it twice.
		return;
	}

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
    char *cp, *oldcfg;
	int confbaselen;

	// check global configuration files
	if(config_file) {
		// Need to temporarily forget config_file, otherwise it will think
		// that it has included config_file twice and refuse to include it.
		oldcfg = config_file;
		config_file = NULL;

		strncpy(buf, oldcfg, sizeof(buf));
		buf[sizeof(buf)-1] = '\0';
		cp = strrchr(buf, '/');
		if(cp == NULL) {
			test_abort(test, "Illegal config_file: '%s'\n", buf); 
		}
		*cp = '\0';
		check_config_str(test, fp, buf, cp+1);
		config_file = oldcfg;
	} else {
		check_config_str(test, fp, "/etc", CONFIG_FILE);
		check_config_str(test, fp, "/etc/tmtest", CONFIG_FILE);
		check_config_str(test, fp, get_home_dir(test), HOME_CONFIG_FILE);
	}

	// check config files in the current hierarchy
	strncpy(buf, curabsolute(), sizeof(buf));
	buf[sizeof(buf)-1] = '\0';
	if(config_file) {
		confbaselen = strrchr(config_file, '/') - config_file;
	}
    for(cp=buf; (cp=strchr(cp,'/')); cp++) {
		// If the user specifies a config file, we only check directories
		// not above the given config file.  i.e. if user specifies
		// "tmtest -c /a/b/cc /a/t/u/t.test", we will look for config files
		// in /a/t/tmtest.conf and /a/t/u/tmtest.conf.
		if(config_file && cp-buf <= confbaselen &&
				memcmp(buf, config_file, cp-buf)==0) {
			continue;
		}
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
        { "CONFIG_FILES",   var_config_files },
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

