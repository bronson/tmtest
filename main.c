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
#include <unistd.h>
#include <getopt.h>
#include <dirent.h>

#include "qscandir.h"


int opt_d= 0;
int opt_o= 0;
int verbose = 0;
double timeout = 10.0;		// timeout in seconds


// exit values:
enum {
    no_error = 0,
    argument_error,
    runtime_error,
};


#define xstringify(x) #x
#define stringify(x) xstringify(x)


void add_define(char *def)
{
    printf("Adding define %s\n", def);
}


void process_file(const char *name)
{
    printf("-- Process %s\n", name);
}


/** This routine filters out any dirents that begin with '.'.
 *  We don't want to process any hidden files or special directories.
 */

int select_nodots(const struct dirent *d)
{
    return d->d_name[0] != '.';
}


/** Runs all tests in the current directory and all its subdirectories.
 *
 * This routine will potentially use a ton of memory.  It allocates
 * a struct stat and a struct dirent for every directory entry in
 * single path from root to leaf.  If that makes any sense...
 */

void process_dir()
{
    mode_t *modes;
    struct dirent **ents;
    struct stat st;
    int i, n;

    n = qscandir(".", &ents, select_nodots, qalphasort);
    if(n < 0) {
        fprintf(stderr, "Error scanning: %s\n", strerror(errno));
        exit(runtime_error);
    }

    modes = malloc(n * sizeof(mode_t));
    if(!modes) {
        fprintf(stderr, "Could not allocate %d mode_t objects.\n", n);
        exit(runtime_error);
    }
    
    // first collect the stat info for each entry
    for(i=0; i<n; i++) {
        if(stat(ents[i]->d_name, &st) < 0) {
            fprintf(stderr, "stat error on '%s': %s\n", ents[i]->d_name, strerror(errno));
            exit(runtime_error);
        }
        modes[i] = st.st_mode;
    }

    // process all files in dir
    for(i=0; i<n; i++) {
        if(S_ISREG(modes[i])) {
            process_file(ents[i]->d_name);
            free(ents[i]);
            ents[i] = NULL;
        }
    }

    // process all subdirs
    for(i=0; i<n; i++) {
        if(!ents[i]) continue;
        if(S_ISDIR(modes[i])) {
            chdir(ents[i]->d_name);
            process_dir();
        }
        free(ents[i]);
    }

    free(ents);
    free(modes);
}


void usage()
{
	printf(
			"Usage: tmtest [OPTION]... [DLDIR]\n"
			"  -o: output the test file with the new output.\n"
			"  -d: output a diff between the expected and actual outputs.\n"
            "  -D NAME=VAL: define a variable on the command line.\n"
            "  -f FILE: manually specify a configuration file to be read.\n"
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
            {"config", 1, 0, 'f'},
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
                opt_d++;
                break;

			case 'D':
                add_define(optarg);
				break;

			case 'h':
				usage();
				exit(0);

			case 'o':
				opt_o++;
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
        if(opt_o) {
            printf("Outputting tests.\n");
        } else if(opt_d) {
            printf("Diffing tests.\n");
        } else {
            printf("Running tests.\n");
        }
		printf("Timeout is %lf seconds\n", timeout);
	}
}


int main(int argc, char **argv)
{
    // char *cwd;

	process_args(argc, argv);

    if(optind < argc) {
        if(verbose) printf("Processing tests named on the command line.\n");
        while(optind < argc) {
            printf("Running %s\n", argv[optind]);
            optind++;
        }
    } else {
        if(verbose) printf("Processing current directory.\n");
        process_dir();
    }

	return 0;
}

