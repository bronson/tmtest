/* qtempnam.c
 * Scott Bronson
 * 29 Dec 2004
 *
 * Why oh why does nobody agree on how to create a tempfile??
 * This file has elements of Octave, Swish, and glibc in it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include "qtempfile.h"

#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif


// We'll try at least this many times
#define OUR_MIN (64*64*64)

#ifndef TMP_MAX
#define TMP_MAX OUR_MIN
#endif

// Conform to POSIX: we must try at least TMP_MAX times.
#if OUR_MIN < TMP_MAX
#define ATTEMPTS TMP_MAX
#else
#define ATTEMPTS OUR_MIN
#endif


static const char letters[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";


int exists(const char *file)
{
    // lifted from gnu octave
    struct stat st;
    int save = errno;

    if(stat (file, &st) == 0) {
        return 1;
    } else {
        // We report that the file exists if stat failed for a reason other
        // than nonexistence.  In this case, it may or may not exist, and we
        // don't know; but reporting that it does exist will never cause any
        // trouble, while reporting that it doesn't exist when it does would
        // violate the interface of gen_tempname.
        int exists = (errno != ENOENT);
        errno = save;
        return exists;
    }
}


char* gen_tempname(char *name)
{
    char *cp, *x;
    int i, j, num;
    int val1, val2;
    int v1, v2;
    struct timeval tv1, tv2;
    int pid = getpid();

    gettimeofday(&tv1, NULL);

    // back up so cp is pointing to the start of the Xs.
    num = 0;
    cp = name + strlen(name);
    while(cp[-1] == 'X' && num < 10) cp--, num++;
    if(num & 0x01) cp++, num--;

    // this is hardly cryptographic.  I'd like to seed from
    // a better random pool, and I'd definitely like to
    // munge the bits round a lot more.
    val1 = (int)tv1.tv_usec ^ (int)tv1.tv_sec ^ pid;
    gettimeofday(&tv2, NULL);
    val2 = (int)tv2.tv_usec ^ (int)tv2.tv_sec ^ pid;

    for(i=0; i<ATTEMPTS; i++) {
        x = cp;
        v1 = val1;
        v2 = val2;

        for(j = 0; j < num; j+=2) {
            x[j+0] = letters[v1 % 62];
            x[j+1] = letters[v2 % 62];
            v1 /= 62;
            v2 /= 62;
        }

/*
case OPEN_FILE:
fd = __open (tmpl, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
break;

case OPEN_BIGFILE:
fd = __open64 (tmpl, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
break;

case OPEN_DIR:
fd = __mkdir (tmpl, S_IRUSR | S_IWUSR | S_IXUSR);
break;

case NO_OPEN:
*/

        if(!exists(name)) {
            return name;
        }

        val1 += 2131;
        val2 += 2131;
    }

    return NULL;
}


/** Returns a unique filename in the most appropriate temporary directory.
 * template must be the same as that supplied to the mkstemp routine
 * except that it can be any even number of Xs less than 10.  It won't
 * return EINVAL if you supply too few or an odd number of Xs.
 *
 * Suffix is the string to put on the end (".doc") or null.
 *
 * You must free the returned filename using free() when you're done with it.
 * If this call fails, it returns NULL.  There are two reasons for it to
 * fail: out of memory to hold the generated fullpath, or could not find a
 * unique file name.
 *
 * todo: should add the ability to open a file, bigfile, or dir
 * and return the fileno, as well as returning the name...?
 */

char *qtempnam(const char *file, const char *suffix)
{
    char *dir;
    int dirlen;
    int filelen;
    int suffixlen;
    int bufsiz;
    char *ret;

    // try to get the temporary directory from the environment.
    if(!(dir = getenv("TMPDIR"))) {
        if(!(dir = getenv("TMP"))) {
            dir = getenv("TEMP");
        }
    }

    if(dir) {
        dirlen = strlen(dir);
        while(dirlen>0 && dir[dirlen-1] == '/') dir[--dirlen] = '\0';
    }

    // if the environment didn't supply one or gave us the empty
    // string (or one of all slashes), we'll use a hard-coded one.
    if(!dir || !*dir) {
        dir = P_tmpdir;
        dirlen = sizeof(P_tmpdir)-1;
        assert(dirlen > 0 && dir[dirlen-1] != '/');
    }

    filelen = strlen(file);
    suffixlen = suffix ? strlen(suffix) : 0;

    bufsiz = dirlen + 1 + filelen + suffixlen + 1;
    ret = malloc(bufsiz);
    if(!ret) {
        fprintf(stderr, "qtempnam(): could not allocate tempnam (%d bytes.\n", bufsiz);
        return NULL;
    }

    ret[0] = '\0';
    strncat(ret, dir, dirlen);
    strcat(ret, "/");
    strncat(ret, file, filelen);
    if(!gen_tempname(ret)) {
        fprintf(stderr, "qtempnam(): could not generate a unique filename for %s.\n", ret);
        free(ret);
        return NULL;
    }
    if(suffix) {
        strncat(ret, suffix, suffixlen);
    }

    assert(strlen(ret) == bufsiz-1);

    return ret;
}

