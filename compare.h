/* compare.h
 * Scott Bronson
 * 31 Dec 2004
 *
 * File comparison.
 */

#include "pcrs.h"
#include "matchval.h"
#include "r2scan.h"


/** Returns zero if the compare has stopped (i.e. the files differed).
 */

#define compare_in_progress(ss) ((ss)->scanref)


void compare_attach(scanstate *ss, matchval *mv, pcrs_job *joblist);
void compare_continue(scanstate *ss, const char *ptr, int len);
void compare_end(scanstate *cmp);

// no better place to put this for now...
char* substitute_string(pcrs_job *job, const char *cp, const char *ce, int *newsize);

