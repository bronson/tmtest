/* scan1.re
 * Scott Bronson
 * 30 Dec 2004
 *
 * This file needs to be processed by re2c, http://re2c.org
 *
 * If re2c supported capturing parentheses, the file state could be removed.
 * I also wish it supported multiple scanners per file.
 */

#include "status.h"


/* This is the first state in the cb scanners.  It just looks for
 * the "start." token.
 *
 *  It skips every line until it finds "start." on a line by
 *  itself (trailing whitespace is OK).  When it finds it, it
 *  returns the CBSTART token and goes to the config state.
 */

int cb_scanner_start(scanstate *ss)
{
    ss->token = ss->cursor;
    ss->line++;

/*!re2c
WS      = [ \t];
ANYN    = [\000-\377]\[\n];
START   = "start.";

START WS* "\n"   { BEGIN(config); return CBSTART; }
ANYN* "\n"       { return GARBAGE; }
*/
}


scanstate* cb_scanner_attach(scanstate *ss)
{
    if(ss) {
        ss->line = 1;
        ss->state = cb_scanner_start;
    }

    return ss;
}

