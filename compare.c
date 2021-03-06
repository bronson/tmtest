/* compare.c
 * Scott Bronson
 * 31 Dec 2004
 *
 * This file is distrubuted under the MIT License
 * See http://en.wikipedia.org/wiki/MIT_License for more.
 *
 *
 * File comparison.
 *
 * This uses all re2c's mechanisms for creating buffers and loading
 * them with data, but it's not actually an re2c scanner.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "compare.h"


#define STATE (*(int*)&(ss)->scanref)

/**
 * Sets up the scanstate for a new comparison.
 *
 * Here's how you use the comparison code:
 * - Create a scanstate attached to one stream and pass it to compare_start().
 * - Read some data from the other stream and pass it to compare_continue().
 * - Keep reading until you're out of data or compare_continue() returns 1.
 *     (no harm to keep passing data except that you're just wasting time)
 * - When you're out of data, call compare_end to obtain the result.
 */

void compare_attach(scanstate *ss)
{
	// STATE is -1 while ss still has data.  If not -1, then it tells
	// us how many bytes ago it ran out of data.
	STATE = cmp_in_progress;
}


// Returns true if and only if the stream has exactly one character
// in it, a newline.

static int has_extra_nl(const char *ptr, size_t len)
{
	if(len == 1 && ptr[0] == '\n') {
		return 1;
	}

	return 0;
}



/**
 * Feeds more bytes to the comparison engine.
 * 
 * @param ss The scanstate from compare_attach.
 * @param ptr The start of the data to compare.
 * @param len The number of bytes to compare, from 0 to MAXINT.
 *
 * @returns 0 if we still don't have an answer, 1 if the match
 * failed.
 */

int compare_continue(scanstate *ss, const char *ptr, size_t len)
{
	int prev_had_nl = 0;
    int n;

    if(STATE != cmp_in_progress) {
		if(len > 0) {
			// if the only difference to this point was a \n, state
			// is has_extra_nl.  If there's more data, though, then no match.
			STATE = cmp_no_match;
		}
        return 1;
    }

    while(len > 0) {
        n = ss->limit - ss->cursor;
        if(!n) {

			// need to remember if the previous (and possibly the last)
			// buffer ended in a newline so we can set the proper flag.
			if(ss->cursor > ss->bufptr && ss->cursor[-1] == '\n') {
				prev_had_nl = 1;
			}

			ss->token = ss->cursor;
            n = (*ss->read)(ss);
            ss->line += n;
            if(n < 0) {
                // there was an error while trying to fill the buffer
				return -1;
            }
            if(n == 0) {
				// banged into the EOF
				if(has_extra_nl(ptr,len)) {
					if(prev_had_nl) {
						STATE = cmp_ptr_has_more_nls;
					} else {
						STATE = cmp_ptr_has_extra_nl;
					}
				} else {
					STATE = cmp_no_match;
				}
                return 1;
            }
        }

        if(len < n) {
            n = len;
        }

        // compare
        if(memcmp(ptr, ss->cursor, n) != 0) {
			STATE = cmp_no_match;
			return 1;
        }

        ptr += n;
        ss->cursor += n;
        len -= n;
    }

	return 0;
}


/**
 * Returns an appropriate code for how well matched the two streams
 * are.  Assumes that you're at EOF on the ptr stream.
 */

compare_result compare_check(scanstate *ss)
{
	if(STATE != cmp_in_progress) {
		return STATE;
	}

	if(scan_is_finished(ss)) {
		return cmp_full_match;
	} else {
		return cmp_ss_has_more_data;
	}
}


/**
 * This is a little complex...  It checks the newline status of the
 * streams.  If one stream had exactly one more newline at this point
 * than the other, it returns a custom value.  Otherwise, if the streams
 * are byte-for-byte identical, it returns cmp_full_match,
 * otherwise compare_no_match.
 *
 * If you call this routine when neither stream is at EOF then it
 * always returns compare_no_match because it can't be sure that
 * either stream will match.  The lesson?  Only call this function
 * when one of the streams is out of data.
 *
 * @param ss the comparison scanner set up by compare_attach().
 * @param ptr The final data from the stream.  If NULL, treated as EOF
 * 	(though typically a 0 len indicates EOF).
 * @param len the amount of data in ptr.  0 indicates EOF.
 *
 * @returns the appropriate value from compare_result.
 */

compare_result compare_check_newlines(scanstate *ss)
{
	if(STATE != cmp_in_progress) {
		return STATE;
	}

	if(scan_is_finished(ss)) {
		return cmp_full_match;
	} else if(has_extra_nl(ss->cursor,ss->limit-ss->cursor)) {
		return cmp_ss_has_extra_nl;
	} else {
		return cmp_ss_has_more_data;
	}
}

