/* scan.re
 * Scott Bronson
 * 30 Dec 2004
 *
 * This file needs to be processed by re2c, http://re2c.org
 * It contains all the scanners required by tmtest.
 */

#include "scan.h"


/** This is the file substate of the cb scanner.
 * It tries to scan a filename.  It returns either
 * CBFILE if the filename name could be scanned
 * or GARBAGE if it couldn't.
 */

int cb_scanner_file(scanstate *ss)
{
    ss->token = ss->cursor;
    ss->line++;

/*!re2c
ANYN    = [\000-\377]\[\n];
ANYNSQ  = ANYN\['];
ANYNDQ  = ANYN\["];

["] (ANYNDQ | "\\\"")+ ["] { ss->token++; ss->tokend = ss->cursor-1;
                             RETURN(); return CBFILE;
                           }
*/
}

