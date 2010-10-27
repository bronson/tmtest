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


/** pathstack_init
 *
 * Initializes a pathstack for use.  You must supply a buffer and an
 * optional string to initialize it with.  The buffer must be able to
 * hold at least a single character (for the null terminator).
 * 
 * We never allocate any memory so there's no need for a pathstack_free.
 */

void pathstack_init(struct pathstack *ps, char *buf,
                    int bufsiz, const char *str)
{
    assert(ps);
    assert(bufsiz > 1);	  // bufsiz must hold "/" and the null terminator.

    ps->buf = buf;
    ps->maxlen = bufsiz - 1;
    if(str) {
    	assert(str[0] == '/');	// ensure it's an absolute path
        ps->curlen = strlen(str);
        if(ps->curlen > ps->maxlen) {
            ps->curlen = ps->maxlen;
        }
        memcpy(ps->buf, str, ps->curlen);
    } else {
        ps->curlen = 1;
        ps->buf[0] = '/';
    }
    ps->buf[ps->curlen] = '\0';
}


/** pathstack_push
 * 
 * Adds the given relative path onto the end of the absolute path.
 * 
 * If the relative path won't fit, we'll push as much as possible and return -1.
 * It's illegal for the relative path to be NULL or empty.
 * 
 * If the existing path doesn't end with a '/' and the new path doesn't begin
 * with one, a slash will be automatically added to separate the two paths.
 * 
 * @param state is optional but, if supplied, specifies a place to hold state.
 * The state can then be passed to pathstack_pop() to return the pathstack to
 * its state before pushing.
 * 
 * @return 0 if everything went OK, -1 if the result had to be truncated.
 * It's perfectly safe to ignore the return value.
 */

int pathstack_push(struct pathstack *ps, const char *newpath,
                   struct pathstate *state)
{
    int pathlen = strlen(newpath);

    assert(ps);
    assert(newpath);

    // First, save the state if requested
    if (state) {
        state->oldlen = ps->curlen;
    }

    // if there's no room for even a single character, bail.
    if(ps->curlen == ps->maxlen) {
        return -1;
    }
    // if the new string is empty then we don't change a thing
    if(newpath[0] == '\0') {
    	return 0;
    }

    // ensure the two paths are separated by '/'
    if(ps->buf[ps->curlen-1] != '/' && newpath[0] != '/') {
        ps->buf[ps->curlen] = '/';
        ps->curlen += 1;
    }

	// and copy the new string
    if(ps->curlen + pathlen > ps->maxlen) {
    	pathlen = ps->maxlen - ps->curlen;
    }
    memcpy(ps->buf+ps->curlen, newpath, pathlen);
    ps->curlen += pathlen;
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
 * (originally it was going to remove the topmost path item)
 */


int pathstack_pop(struct pathstack *ps, struct pathstate *state)
{
	if(state) {
		if(state->oldlen > ps->curlen) {
			// we can't enlarge the string using state; we'd expose invalid data.
			return -1;
		}
		ps->curlen = state->oldlen;
		state->oldlen = INT_MAX;	// ensure this state can never be used again
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

