/* expec.re
 * Scott Bronson
 * 30 Dec 2004
 *
 * This file needs to be processed by re2c, http://re2c.org
 */

#include "expec.h"


#define START(x) (ss->scanref=(void*)(x))


/* This scanner scans lines.  When it finds a line that begins
 * with a new section, it returns the token name of that section
 * with the exNEW flag turned on.  After that, it returns each
 * line in the section with the token's identifier.  When it
 * finds a new section, you get a exNEW+TOKEN of the new section.
 */

int expec_scanner_start(scanstate *ss)
{
    ss->token = ss->cursor;
    ss->line++;

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


scanstate* expec_scanner_attach(scanstate *ss)
{
    if(ss) {
        ss->line = 0;
        START(exGARBAGE);
        ss->state = expec_scanner_start;
    }

    return ss;
}

