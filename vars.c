/* vars.c
 * 29 Dec 2004
 * Copyright (C) 2004 Scott Bronson
 * This entire file is covered by the GNU GPL V2.
 * 
 * Generates default values for all the variables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include "test.h"
#include "vars.h"
#include "qtempfile.h"


static int var_title(struct test *test, FILE* fp, const char *var)
{
    assert(test->filename);
    fprintf(fp, test->filename);
    return 0;
}


static int var_author(struct test *test, FILE *fp, const char *var)
{
    char *u = getlogin();
    if(u) {
        fputs(u, fp);
    } else {
        fprintf(stderr, "getlogin() returned null!\n");
        return 1;
    }

    return 0;
}


static int var_date(struct test *test, FILE *fp, const char *var)
{
    time_t now;
    struct tm* tm;

    time(&now);
    tm = localtime(&now);
    fprintf(fp, "%d-%d-%d %d:%d:%d",
            tm->tm_year+1900, tm->tm_mon, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);

    return 0;
}


static int var_tmpfile(struct test *test, FILE *fp, const char *var)
{
    char buf[64];
    char *fn;

    snprintf(buf, sizeof(buf), "tmtest-%s-XXXXXX", var);
    fn = qtempnam(buf, NULL);
    if(!fn) {
        // the tempnam already printed the reason why to stdout
        return 1;
    }

    // store the generated filename in the test structure
    if(strcmp(var, "STDOUT") == 0) {
        test->outfile = fn;
    } else if(strcmp(var, "STDERR") == 0) {
        test->errfile = fn;
    } else if(strcmp(var, "OUT_OF_BAND") == 0) {
        test->oobfile = fn;
    } else {
        fprintf(stderr, "Unknown variable in var_tmpfile(): %s\n", var);
        return 1;
    }

    fputs(fn, fp);
    return 0;
}


static int var_oob_separator(struct test *test, FILE *fp, const char *var)
{
    // todo: make this thing random.  and less stupid.
    static const char *sep =
        "    -  - - --  dp TMTEST SEPARATOR pd  -- - -  -";

    test->oobsep = sep;
    fprintf(fp, sep);
    return 0;
}


static int var_config_files(struct test *test, FILE *fp, const char *var)
{
    char *cwd;
    char *cp;

    cwd = getcwd(NULL, 0);
    if(!cwd) {
        perror("Could not getcwd");
        return 1;
    }

    fprintf(fp, "'/etc/tmtest.conf' ");
    fprintf(fp, "'~/.tmtestrc' ");

    for(cp=cwd; (cp=strchr(cp,'/')); cp++) {
        fprintf(fp, "'%.*s/tmtest.conf' ", cp-cwd, cwd);
    }
    fprintf(fp, "'%s/tmtest.conf' ", cwd);

    free(cwd);

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
        { "OUT_OF_BAND",    var_tmpfile },
        { "OOB_SEPARATOR",  var_oob_separator },
        { "STDOUT",         var_tmpfile },
        { "STDERR",         var_tmpfile },
        { "TITLE",          var_title },
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

