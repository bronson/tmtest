/* curdir.c
 * 29 Jan 2005
 * Copyright (C) 2005 Scott Bronson
 * 
 * Some simple path handling routines
 *
 * Unfortunately this file got hacked to shreds when tmtest was
 * made to run with /tmp as the constant cwd...
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <assert.h>

#include "curdir.h"


static char curdir[CURDIR_SIZE];
static char *curpart;


int curinit(const char *path)
{
	if(path) {
		if(strlen(path)+1 > sizeof(curdir)) {
			return -1;
		}
		strcpy(curdir, path);
		curpart = NULL;	// don't use partials when inited from a path
	} else {
		if(!getcwd(curdir, sizeof(curdir))) {
			return -1;
		}
		curpart = curdir + strlen(curdir) + 1;
		*curpart = '\0';
	}

	return 0;
}


/**
 * @returns the number of directories you just pushed.
 * You'll need to pass this value to curpop if you want to pop
 * the same number of directories when you're done.
 */

int curpush(const char *dir)
{
	int clen = strlen(curdir);
	int dlen = strlen(dir);
	int keep = 1;
	const char *cp;

	assert(dir);
	assert(dir[0]);
	assert(dir[0] != '/');

	// count the number of '/' characters in dir
	for(cp=dir; (cp=strchr(cp,'/'))!=0; cp++) {
		keep++;
	}

	if(clen + dlen + 2 > sizeof(curdir)) {
		return -1;
	}

	strcat(curdir, "/");
	strcat(curdir, dir);

	return keep;
}


void curpop(int keep)
{
	assert(keep>0);

	while(keep) {
		char *cp = strrchr(curdir, '/');
		assert(curpart <= cp+1);
		if(!cp) {
			// we've run out of slashes.
			assert(!"out of slashes -- that's bad.");
			return;
		}

		cp[0] = '\0';	// get rid of the slash.
		cp[1] = '\0';	// blank out curpart or any other ptrs to this dir.
		keep -= 1;
	}
}


/** Resets the curdir so that curabsolute() and currelative()
 *  return what they did after curinit() was first called.
 */

void curreset()
{
	if(curpart) {
		curpart[-1] = '\0';
		curpart[0] = '\0';
	}
}


void cursave(struct cursave *save)
{
	strcpy(save->buf, curdir);
	save->part = curpart;
}


void currestore(struct cursave *save)
{
	strcpy(curdir, save->buf);
	curpart = save->part;
}


const char *curabsolute()
{
	return curdir;
}


const char *currelative()
{
	return curpart ? curpart : curdir;
}



