/* scan.re
 * Scott Bronson
 * 30 Dec 2004
 *
 * This file needs to be processed by re2c, http://re2c.org
 *
 * This file could be shortened a great deal if re2c supported
 * capturing parentheses.  The cb_scanner_file state could be
 * eradicated.
 */

#include "status.h"


/** This is the second state in the scan progression.
 * It looks for "config: " at the start of the line.
 * If found, it returns CBCONFIG, then gosubs to the
 * file state to read the filename.  It stops when it
 * finds "ready."
 */

int cb_scanner_config(scanstate *ss)
{
    ss->token = ss->cursor;

/*!re2c
WS      = [ \t];
ANYN    = [\000-\377]\[\n];
ANYNWS  = ANYN\WS;
CONFIG  = "config";
READY   = "ready.";

CONFIG WS* ":" WS* { BEGINSUB(file); return CBCONFIG; }
READY WS*          { BEGIN(running); return CBREADY; }
ANYNWS*            { return GARBAGE;                  }
WS*                { return WHITESPACE;             }
"\n"               { ss->line++; return NEWLINE;    }

*/
}

