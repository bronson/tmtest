/* pathstack.h
 * 14 Feb 2007
 * Copyright (C) 2007 Scott Bronson
 * 
 * Some simple path handling routines.
 * See pathstack.c for license.
 */

struct pathstack {
    char *buf;
    int bufsiz;
    int curlen;  // not including null terminator
};

struct pathstate {
    int oldlen;
};


int pathstack_init(struct pathstack *path, char *buf,
        int bufsiz, const char * str);

// There is no need to ever delete a pathstack because
// it's 100% client-allocated.

int pathstack_push(struct pathstack *ps, const char *newpath,
        struct pathstate *state);
int pathstack_pop(struct pathstack *ps, struct pathstate *state);

#define pathstack_absolute(ps) ((ps)->buf)
