/* scan.re
 * Scott Bronson
 * 30 Dec 2004
 *
 * This file needs to be processed by re2c, http://re2c.org
 * It contains all the scanners required by tmtest.
 */

#include "scan.h"


/** This is the third and last state in the scan progression.
 * It looks for "running: " at the start of the line.
 * If found, it returns CBRUNNING, then gosubs to the
 * file state to read the filename.  It locks into this
 * state, returning either CBRUNNING/CBFILE or garbage
 * until EOF.
 */

int cb_scanner_running(scanstate *ss)
{
    ss->token = ss->cursor;
    ss->line++;

/*!re2c
WS      = [ \t];
ANYN    = [\000-\377]\[\n];
RUNNING = "running";
READY   = "ready.";

RUNNING WS* ":" WS* { BEGINSUB(file); return CBRUNNING; }
ANYN* "\n"          { return GARBAGE;                   }
*/
}

