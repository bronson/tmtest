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
 *
 * Well, the line buffering for the pcrs library has utterly destroyed
 * the simplicity of this file.
 *
 */

#include <string.h>
#include "compare.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>


typedef struct {
	matchval *output;
	pcrs_job *jobs;
	const char *pbuf;
	int pcursor;
	int plimit;
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


void compare_attach(scanstate *ss, matchval *mv, pcrs_job *jobs, int nonl)
{
	compare_state *cmp = malloc(sizeof(compare_state));
	if(cmp == NULL) {
		perror("compare_attach");
		exit(10);
	}
	memset(cmp, 0, sizeof(compare_state));

    *mv = match_inprogress;

	cmp->output = mv;
	cmp->jobs = jobs;
    cmp->no_trailing_newline = nonl;
    cmp->nl_suppressed = 0;
    cmp->warn_no_newline = 0;
    ss->scanref = cmp;
}



static void compare_continue_bytes(scanstate *ss, const char *ptr, int len)
{
	compare_state *cmp = (compare_state*)ss->scanref;
    int n;

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


char* substitute_string(pcrs_job *job, const char *cp, const char *ce, size_t *newsize)
{
	char *old, *new;
	int nsubs;
	int cnt;

	cnt = 0;
	old = (char*)cp;
	*newsize = ce - cp;

	while(job) {
		// Now, run the line through the substitutions.
		nsubs = pcrs_execute(job, old, *newsize, &new, newsize);
		if(nsubs < 0) {
			fprintf(stderr, "error while substituting expr %d: %s (%d).\n",
					cnt, pcrs_strerror(nsubs), nsubs);
            if(new) {
                free(new);
                new = NULL;
            }
			break;
		}

		assert(strlen(new) == *newsize);

		if(old != cp) {
			// free the intermediate result.
			free(old);
		}
		old = new;
		job = job->next;
		cnt += 1;
	}

    return new;
}


// This routine is hellish because neither stream is line buffered.
// Therefore, we line buffer the file and compare in chunks against
// the input.  "pbuf" is the substitution-modified buffer allocated
// by the pcre library.

static void compare_continue_lines(scanstate *ss, compare_state *cmp,
		const char *ptr, size_t len)
{
	int n;
	const char *p;
    char *new;
    size_t newsize;

	while(len > 0) {
		n = cmp->plimit - cmp->pcursor;
		assert(n >= 0);
		if(n) {
			// there's more data in the pbuf.  use it for comparison.
			if(len < n) n = len;
			if(memcmp(ptr, cmp->pbuf+cmp->pcursor, n) != 0) {
				compare_halt(ss, match_no);
				return;
			}
			cmp->pcursor += n;
			ptr += n;
			len -= n;
			assert(len >= 0);
			if(!len) break;
		}

		// we've emptied the pbuf so we need to regenerate it.
		// first see if there's anther line in the line buffer.
		p = memchr(ss->cursor, '\n', ss->limit - ss->cursor);
		if(!p) {
			// there's no newline in the buffer.  try to refill.
			n = compare_fill(ss);
			if(n < 0) {
				// there was an error while trying to fill the buffer
				// TODO: this should be propagated to the client somehow.
				perror("compare_continue_lines");
				exit(10);
			}
			if(n == 0) {
                compare_halt(ss, match_no);
                return;
			}

			p = memchr(ss->cursor, '\n', ss->limit - ss->cursor);
			if(!p) {
				// we'll just have to treat the entire buffer as a full
				// line and hope for the best.  Subtract one because
				// the very next line will add one.
				p = ss->limit - 1;
			}
		}

		// move p past the newline so it's at the end of the current line.
		p += 1;
		new = substitute_string(cmp->jobs, ss->cursor, p, &newsize);
        if(!new) {
            // there must have been an error
            compare_halt(ss, match_no);
            return;
        }

        if(cmp->pbuf) free((char*)cmp->pbuf);
        cmp->pbuf = new;
        cmp->pcursor = 0;
        cmp->plimit = newsize;
		ss->cursor = p;
	}
}


void compare_continue(scanstate *ss, const char *ptr, int len)
{
	compare_state *cmp = (compare_state*)ss->scanref;

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
            compare_continue_bytes(ss, "\n", 1);
            cmp->nl_suppressed = 0;
        }

        if(ptr[len-1] == '\n') {
            cmp->nl_suppressed = 1;
            len -= 1;
        }
    }

	if(cmp->jobs) {
		compare_continue_lines(ss, cmp, ptr, len);
	} else {
		compare_continue_bytes(ss, ptr, len);
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
        assert(cmp->pcursor <= cmp->plimit);
        assert(ss->cursor <= ss->limit);

        *cmp->output = match_no;
        if(cmp->jobs && cmp->plimit - cmp->pcursor != 0) {
            // get rid of this asap.
            *cmp->output = match_no;
        } else if(scan_finished(ss)) {
            // if we're totally out of data and we still don't know
            // if they match, then they do match.
            *cmp->output = match_yes;
        }
    }

	if(cmp->pbuf) {
        free((char*)cmp->pbuf);
    }

	free(cmp);
    ss->scanref = NULL;
}

