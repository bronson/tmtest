/* pathstack.c
 * 14 Feb 2007
 * Copyright (C) 2007 Scott Bronson
 *
 * This file is distrubuted under the MIT License
 * See http://en.wikipedia.org/wiki/MIT_License for more.
 *
 * Some simple path handling routines
 *
 * A pathstack is just like a regular stack, except you push and pull
 * path fragments.  The only slightly strange concept is the pathstack_state
 * used to "rewind" a push.
 */


#include <assert.h>
#include <string.h>
#include <limits.h>
#include "pathstack.h"
#include "pathconv.h"


static int trim_slashes(const char **strp)
{
    const char *str = *strp;
    int len;

    // remove any leading '/' from newpath
    while(str[0] == '/') {
        str += 1;
    }

    len = strlen(str);

    while(len && str[len-1] == '/') {
        len -= 1;
    }

    *strp = str;
    return len;
}


/** pathstack_init
 *
 * Initializes a pathstack for use.  You must supply a buffer and an
 * optional string to initialize it with.  The buffer must be able to
 * hold at least a single character (for the null terminator).
 *
 * We never allocate any memory so there's no need for a pathstack_free.
 */

int pathstack_init(struct pathstack *ps, char *buf,
                    int bufsiz, const char *str)
{
    if(!ps || bufsiz < 2) {
        return 1;
    }

    ps->buf = buf;
    ps->bufsiz = bufsiz;
    ps->curlen = 1;
    ps->buf[0] = '/';

    if(str) {
        ps->curlen = 1 + trim_slashes(&str);  // slash
        if(ps->curlen + 1 > bufsiz) {         // null terminator
            return 1;
        }
        memcpy(ps->buf + 1, str, ps->curlen - 1);
    }

    ps->buf[ps->curlen] = '\0';
    return 0;
}


/** pathstack_push
 *
 * Adds the given relative path onto the end of the absolute path.
 * If the relative path won't fit then nothing is changed and an
 * error value is returned.
 *
 * Slashes are normalized. It doesn't matter if the paths begin or end
 * with any number of slashes, they're still treated as relative.
 *
 * @param state is optional but, if supplied, specifies a place to hold state.
 * The state can then be passed to pathstack_pop() to return the pathstack to
 * its state before pushing.
 */

int pathstack_push(struct pathstack *ps, const char *newpath,
                   struct pathstate *state)
{
    int newlen;

    assert(ps);
    assert(newpath);

    // First, save the state if requested
    if (state) {
        state->oldlen = ps->curlen;
    }

    newlen = trim_slashes(&newpath);
    if(newpath[0] == '\0') {
        return 0;
    }
    if(ps->curlen + newlen + 2 > ps->bufsiz) {
        return 1;
    }

    ps->buf[ps->curlen++] = '/';
    memcpy(ps->buf+ps->curlen, newpath, newlen);
    ps->curlen += newlen;

    ps->buf[ps->curlen] = '\0';
    return 0;
}


/** pathstack_pop
 *
 * Removes the most recent addition from the pathstack.
 *
 * Returns -1 if the state was invalid.
 *
 * If you don't supply a state, this function is a no-op.
 */


int pathstack_pop(struct pathstack *ps, struct pathstate *state)
{
    if(state) {
        if(state->oldlen > ps->curlen) {
            // we can't enlarge the string using state; we'd expose invalid data.
            return -1;
        }
        ps->curlen = state->oldlen;
        state->oldlen = INT_MAX;        // ensure this state can never be used again
    } else {
        // popping without supplying state is currently a no-op...
        // implement this later if needed.
    }

    ps->buf[ps->curlen] = '\0';
    return 0;
}


/** pathstack_normalize
 *
 * Normalizes the path stored in the pathstack.
 * NOTE: Never ever call pathstack_push, normalize the stack, and then
 * call pathstack_pop!!  Normalizing will potentially change the path,
 * invalidating the offsets stored in the pathstate structs.
 * 
 * TODO: add some unit tests for this!
 */
 
void pathstack_normalize(struct pathstack *ps)
{
    normalize_absolute_path(ps->buf);
    ps->curlen = strlen(ps->buf);
}

