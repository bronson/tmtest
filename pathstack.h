/* pathstack.h
 * 14 Feb 2007
 * Copyright (C) 2007 Scott Bronson
 * 
 * Some simple path handling routines.
 * See pathstack.c for license.
 */

struct pathstack {
    char *buf;
    int curlen;
    int maxlen;	///< size of buffer - 1 (for null byte)
};

struct pathstate {
	int oldlen;
};


void pathstack_init(struct pathstack *path, char *buf,
                   int bufsiz, const char * str);

// There is no need to ever delete a pathstack because
// it's 100% client-allocated.

int pathstack_push(struct pathstack *ps, const char *newpath,
                   struct pathstate *state);
int pathstack_pop(struct pathstack *ps, struct pathstate *state);

void pathstack_normalize(struct pathstack *ps);

#define pathstack_absolute(ps) ((ps)->buf)
