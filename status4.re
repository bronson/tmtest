/* scan.re
 * Scott Bronson
 * 30 Dec 2004
 *
 * This file needs to be processed by re2c, http://re2c.org
 */

#include "status.h"


/** This is the file substate of the cb scanner.
 * It tries to scan a filename.  It returns either
 * CBFILE if the filename name could be scanned
 * or GARBAGE if it couldn't.
 *
 * Well, it's been simplified a lot now that we don't attempt to quote
 * filenames.  The filename just extends from the current cursor position
 * to the end of the line.  This scanner always returns a CBFILE token
 * and resets the start state.  It could go away completely if re2c
 * would support capturing parentheses (but that would probably require
 * a rewrite...)
 */

int cb_scanner_file(scanstate *ss)
{
    ss->token = ss->cursor;
    ss->line++;

/*!re2c
ANYN    = [\000-\377]\[\n];

ANYN* "\n"    { ss->tokend = ss->cursor;
                RETURN(); return CBFILE;
              }
*/
}



/*
we're not quoting the filenames anyway.

ANYNSQ  = ANYN\['];
ANYNDQ  = ANYN\["];
NONWS   = ANYN\[\t ];

["] (ANYNDQ | "\\\"")+ ["] { ss->token++; ss->tokend = ss->cursor-1;
                             RETURN(); return CBFILE;
                           }
['] (ANYNDQ | "\\\'")+ ['] { ss->token++; ss->tokend = ss->cursor-1;
                             RETURN(); return CBFILE;
                           }
*/
