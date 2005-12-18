/* stscan.re
 * Scott Bronson
 *
 * This file is covered by the MIT license.
 *
 * Scanner for status files files.
 * This file needs to be processed by re2c, http://re2c.org
 */

#include "stscan.h"


/* This scanner scans lines.  When it finds a line that begins
 * with a recognized token, it returns that token with the text
 * of the entire line.  Otherwise, it returns stGARBAGE.
 *
 * Note that this scanner only returns complete lines.  If the file
 * doesn't end with a newline, then the last line will be lost.
 * I think.
 */

int stscan_start(scanstate *ss)
{
    scanner_enter(ss);
    inc_line(ss);

/*!re2c

WS      = [ \t];
ANYN    = [\000-\377]\[\n];
NOARG   = "\n"|(WS+ ANYN* "\n");
HASARG  = WS* ":" ANYN* "\n";

"START"   NOARG     { return stSTART; }
"CONFIG"  HASARG    { return stCONFIG; }
"PREPARE" NOARG     { return stPREPARE; }
"RUNNING" HASARG    { return stRUNNING; }
"DONE"    NOARG     { return stDONE; }

"ABORTED"  HASARG   { return stABORTED; }
"DISABLED" HASARG   { return stDISABLED; }

ANYN* "\n"          { return stGARBAGE; }

*/
}


/** Prepares the given scanner to scan a status file.
 *
 *  @param ss the scanstate to attach to.  Passing NULL is safely ignored.
 *  @returns ss.  Always.  This routine makes no calls that can fail.
 */

scanstate* stscan_attach(scanstate *ss)
{
    if(ss) {
        ss->state = stscan_start;
    }

    return ss;
}

