/* tfscan.re
 * Scott Bronson
 * 30 Dec 2004
 *
 * Scanner for test files.
 * This file needs to be processed by re2c, http://re2c.org
 */

#include "tfscan.h"


#define START(x) (ss->scanref=(void*)(x))


/* This scanner scans lines.  When it finds a line that begins
 * with a new section, it returns the token name of that section
 * with the exNEW flag turned on.  After that, it returns each
 * line in the section with the token's identifier.  When it
 * finds a new section, you get a exNEW+TOKEN of the new section.
 */

int tfscan_start(scanstate *ss)
{
    ss->token = ss->cursor;
    inc_line(ss);

/*!re2c

WS      = [ \t];
ANYN    = [\000-\377]\[\n];
ANYNWS  = ANYN\WS;

"STDOUT" WS+ ":" ANYN* "\n"  { START(exSTDOUT); return exNEW|exSTDOUT; }
"STDERR" WS+ ":" ANYN* "\n"  { START(exSTDERR); return exNEW|exSTDERR; }
"RESULT" WS+ ":" ANYN* "\n"  { START(exRESULT); return exNEW|exRESULT; }
"MODIFY" WS+ ":" ANYN* "\n"  { START(exMODIFY); return exNEW|exMODIFY; }

ANYN* "\n"                  { return (int)ss->scanref; }
*/
}


scanstate* tfscan_attach(scanstate *ss)
{
    if(ss) {
        START(exCOMMAND);
        ss->state = tfscan_start;
    }

    return ss;
}

