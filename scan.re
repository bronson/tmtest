/* scan.re
 * Scott Bronson
 * 30 Dec 2004
 *
 * This file needs to be processed by re2c, http://re2c.org
 * It contains all the scanners required by tmtest.
 *
 * This file could be shortened a great deal if re2c supported
 * capturing parentheses.  The cb_scanner_file state could be
 * eradicated.
 */

#include "scan.h"


// because I'm used to lex...  This is how we enter the
// various start states.

#define STATE(st)           (cb_scanner_##st)
#define BEGIN(next)         (ss->state = STATE(next))
#define BEGINSUB(next,ret)  (ss->scanref = STATE(ret), ss->state=STATE(next))
#define RETURN()            (ss->state = ss->scanref)


// common re2c declarations:
/*!re2c
WS      = [ \t];
ANYN    = [\000-\377]\[\n];
 */



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
START   = "start.";

START WS* "\n"   { BEGIN(config); return CBSTART; }
ANYN* "\n"       { return GARBAGE; }
*/
}


// matches "config: " at the beginning of the line.
int cb_scanner_config(scanstate *ss)
{
    ss->token = ss->cursor;
    ss->line++;

/*!re2c
CONFIG  = "config";

CONFIG WS* ":" WS* { BEGINSUB(file,ready); return CBCONFIG; }
ANYN* "\n"         { return GARBAGE;                  }
*/
}


int cb_scanner_file(scanstate *ss)
{
    ss->token = ss->cursor;
    ss->line++;

/*!re2c
ANYQN   = ANYN\["];
FILE    = ["] ANYQN ["];

START WS* [\n]   { ss->state = cb_scanner_config; return CBSTART; }
ANYN* "\n"       { return GARBAGE; }
*/
}


