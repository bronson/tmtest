/* curdir.h
 * 29 Jan 2005
 * Copyright (C) 2005 Scott Bronson
 * 
 * Some simple path handling routines.
 * See curdir.c for license.
 */


#define CURDIR_SIZE PATH_MAX


struct cursave {
	char buf[CURDIR_SIZE];
	char *part;
};

int curinit(const char *path);
int curpush(const char *dir);
void curpop(int keep);
void curreset();
void cursave(struct cursave *save);
void currestore(struct cursave *save);
const char *curabsolute();
const char *currelative();

