/* compare.h
 * Scott Bronson
 * 31 Dec 2004
 *
 * File comparison prototypes.
 *
 * See compare.c for license.
 */

#include "re2c/scan.h"



/**
 * a tristate that tells whether something
 *    - matches
 *    - doesn't match
 *    - hasn't been checked yet.
 */

typedef enum {
    match_inprogress = -2,
    match_unknown = -1,
    match_no = 0,
    match_yes = 1,
} matchval;




/** Returns zero if the compare has stopped (i.e. the files differed),
 *  one if we're still unsure.
 */

#define compare_in_progress(ss) ((ss)->scanref)


void compare_attach(scanstate *ss, matchval *mv, int nonl);
void compare_continue(scanstate *ss, const char *ptr, int len);
void compare_end(scanstate *cmp, int *warn_nl);

