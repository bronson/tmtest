/* compare.c
 * Scott Bronson
 * 31 Dec 2004
 *
 * File comparison.
 *
 * This uses all re2c's mechanisms for creating buffers and loading
 * them with data, but it's not an re2c scanner.
 *
 * Here's what you do:
 * - Create a scanstate that reads from one of the files (or
 *   whatever) that you want to compare and pass it to compare_start.
 * - Call compare_continue with any size data block from the other
 *   file you want to compare.  Keep calling until you're out of
 *   data.  Check compare_in_progress to see if it failed prematurely.
 * - Finally, when you're out of data in the other file, call
 *   compare_end.  It will verify that it's at eof on the first
 *   file.
 * - Read the final result from the matchval.
 */

#include <string.h>
#include "compare.h"

#include <stdio.h>
#include <stdlib.h>


static int compare_fill(scanstate *ss)
{
    return (*ss->read)(ss);
}

static void compare_halt(scanstate *ss, matchval newval)
{
    *(matchval*)ss->scanref = newval;
    ss->scanref = NULL;
}



void compare_attach(scanstate *ss, matchval *mv)
{
    *mv = match_inprogress;
    ss->scanref = mv;
    ss->state = NULL;   // can't call compare by the state var.
}


void compare_continue(scanstate *ss, const char *ptr, int len)
{
    int n;

    if(!ss->scanref) {
        // we already decided the files don't match
        // so don't waste time comparing more.
        return;
    }

    while(len > 0) {
        n = ss->limit - ss->cursor;
        if(!n) {
            n = compare_fill(ss);
            if(n < 0) {
                // there was an error while trying to fill the buffer
                // TODO: this should be propagated to the client somehow.
                perror("compare_continue");
                exit(10);
            }
            if(n <= 0) {
                // there's more input data but either there's an error
                // or we're at eof, so we'll fail the match and stop.
                compare_halt(ss, match_no);
                return;
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


void compare_end(scanstate *ss)
{
    if(!ss->scanref) {
        // we already decided the files don't match
        // so don't waste time comparing more.
        return;
    }

    // if we have no data left in our buffer
    if(ss->limit - ss->cursor == 0) {
        // and our input file is at eof
        if(compare_fill(ss) == 0) {
            // then the two data streams match
            compare_halt(ss, match_yes);
            return;
        }
    }

    // otherwise, they don't.
    compare_halt(ss, match_no);
}


