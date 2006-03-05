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
 * Here's what you do:
 * - Create a scanstate attached to one stream and pass it to compare_start.
 * - Read some data from the other stream and pass it to compare_continue.
 * - Keep reading until you're out of data.  You can check compare_in_progress
 *   to see if the match already failed and you can bail out early.
 * - When you're out of data, call compare_end to obtain the result.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "compare.h"


// TODO: these are all 1-bit flags.  No need for malloc and free!
// get rid of the output field.

typedef struct {
	matchval *output;
	const char *pbuf;
    int no_trailing_newline;    ///< true if this section isn't supposed to end with a newline
    int nl_suppressed;          ///< if no_trailing_newline is true, we need to suppress the newline in the execpted output when matching against the actual (because of the way heredocs work, the expected always ends in a newline).
    int warn_no_newline;             ///< true if the section expected a trailing newline (no_trailing_newline == false), but the last buffer seen (presumably the end of the file) didn't end with a newline.
} compare_state;


static int compare_fill(scanstate *ss)
{
    // need to pretend like we're updating the token,
    // otherwise the readproc will think we need to keep
    // the token and we quickly run out of buffer room.
    ss->token = ss->cursor;

    return (*ss->read)(ss);
}


static void compare_halt(scanstate *ss, matchval newval)
{
	compare_state *cmp = (compare_state*)ss->scanref;
    *cmp->output = newval;
}


void compare_attach(scanstate *ss, matchval *mv, int nonl)
{
	compare_state *cmp = malloc(sizeof(compare_state));
	if(cmp == NULL) {
		perror("compare_attach");
		exit(10);
	}
	memset(cmp, 0, sizeof(compare_state));

    *mv = match_inprogress;

	cmp->output = mv;
    cmp->no_trailing_newline = nonl;
    cmp->nl_suppressed = 0;
    cmp->warn_no_newline = 0;
    ss->scanref = cmp;
}



void compare_continue(scanstate *ss, const char *ptr, int len)
{
	compare_state *cmp = (compare_state*)ss->scanref;
    int n;

    if(*cmp->output != match_inprogress) {
        // we already decided an answer
        // so don't waste time comparing more.
        return;
    }

    assert(len >= 0);

    if(cmp->no_trailing_newline) {
        // If the incoming buffer ends in a nl, we need to suppress it
        // for the comparison.  Note that this won't work well for
        // MODIFY clauses but I don't care because MODIFY will never
        // work with -n and, anyway, MODIFY is about to disappear.

        if(cmp->nl_suppressed) {
            cmp->nl_suppressed = 0;
			if(ss->cursor < ss->limit) {
				if(ss->cursor[0] != '\n') {
					compare_halt(ss, match_no);
					return;
				}
				ss->cursor += 1;
			} else {
				compare_continue(ss, "\n", 1);
			}
        }

        if(ptr[len-1] == '\n') {
            cmp->nl_suppressed = 1;
            len -= 1;
        }
    }

    while(len > 0) {
        n = ss->limit - ss->cursor;
        if(!n) {
            n = compare_fill(ss);
            ss->line += n;
            if(n < 0) {
                // there was an error while trying to fill the buffer
                // TODO: this should be propagated to the client somehow.
                perror("compare_continue_bytes");
                exit(10);
            }
            if(n == 0) {
                compare_halt(ss, match_no);
                return;
            }
           
            if(n > 0) {
                // shouldn't force user to put a -n on empty sections!
                // therefore, we'll only issue the warning if we've seen data.
                cmp->warn_no_newline = (ss->limit[-1] != '\n' && ss->limit[-1] != '\r');
            }
        }

        if(len < n) {
            n = len;
        }

        // compare
        if(memcmp(ptr, ss->cursor, n) != 0) {
            compare_halt(ss, match_no);
            return;
        }
        ptr += n;
        ss->cursor += n;
        len -= n;
    }
}


void compare_end(scanstate *ss, int *warn_nl)
{
	compare_state *cmp = (compare_state*)ss->scanref;

    // Tell the caller to emit a warning if the expected section was marked
    // as having a trailing newline but the actual section didn't have it.
    if(warn_nl && !cmp->no_trailing_newline) {
        *warn_nl = cmp->warn_no_newline;
    }

    if(*cmp->output == match_inprogress) {
        assert(ss->cursor <= ss->limit);

        *cmp->output = match_no;
        if(scan_finished(ss)) {
            // if we're totally out of data and we still don't know
            // if they match, then they do match.
            *cmp->output = match_yes;
        }
    }

	free(cmp);
    ss->scanref = NULL;
}

