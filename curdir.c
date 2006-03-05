/* curdir.c
 * 29 Jan 2005
 * Copyright (C) 2005 Scott Bronson
 *
 * This file is distrubuted under the MIT License
 * See http://en.wikipedia.org/wiki/MIT_License for more.
 *
 * Some simple path handling routines
 *
 * Unfortunately this file got hacked to shreds when tmtest was
 * made to run with /tmp as the constant cwd.  It's pretty
 * incomprehensible and in dire dire DIRE need of a rewrite.
 *
 * There's also now a pretty serious impedance mismatch...
 * The caller must handle all occurrences of "//" "/.", ".."
 * etc. in the partial path before passing it here.  But
 * that's stupid because properly handling those requires
 * converting the parital to an absolute path before normalizing.
 * This library should normalize the path after catting, then
 * return the number of normalized directories added.  This is
 * pretty decidedly nontrivial, of course, because if I push
 * "..", a directory gets removed from the path, then I push
 * "dir", then I pop twice, I need to return to the original
 * directory.
 *
 * So for now, alas, it's hacked in main.c.
 *
 * What this file should have been:
 *
 * A completely flexible path lib.  You can init the path with an
 * arbitrary string or from the cwd.  You can push an arbitrary
 * path onto the end (including "..") and it will return a pointer
 * to an "undo" struct.  Then, when you want to pop the pushed
 * path, you pass the undo struct, and everything is reverted to
 * the way it was before.  You can push an arbitrary number of
 * times.  Push/pop must always be nested of course.  And, of
 * course, you must be able to save and restore the state of the
 * curdir.  Finally, it should not use globals so multiple threads
 * etc. can all maintain their own curdirs.
 *
 * If we had a module that could do this, main.c could be
 * *drastically* cleaned up.  Sigh.
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



