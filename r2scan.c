/* re2c.c
 * Scott Bronson
 * 28 Dec 2004
 */

#include <string.h>
#include <assert.h>
#include "r2scan.h"


/** Initializes the given scanstate.
 * Note that you must attach it to a reader after calling this routine.
 */

void scanstate_init(scanstate *ss, const char *bufptr, int bufsiz)
{
    ss->cursor = bufptr;
    ss->limit = bufptr;
    ss->marker = NULL;
    ss->token = bufptr;
    ss->bufptr = bufptr;
    ss->bufsiz = bufsiz;
    ss->readref = NULL;
    ss->read = NULL;
    ss->scanref = NULL;
    ss->state = NULL;
}

