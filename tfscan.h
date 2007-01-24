/* tfscan.h (testfile scanner)
 * Scott Bronson
 */

/** @file tfscan.h
 *
 * This file declares the interface to the expected results test
 * file scanner.
 */


#include "re2c/scan.h"


/** This lists all the tokens that the test file scanner may return.
 */

enum {
    // command sections are numbered between 1 and 63
    exCOMMAND = 1,			///< marks a new line in the command section.  (in the future, we might actually start parsing the command section)
    exCOMMAND_TOKEN_END,	///< never returned.  this token is always one higher than the highest-numbered command token.

    // result sections are numbered from 64 through 127.
    exSTDOUT = 64,			///< marks a line in the stdout section.
    exSTDERR,				///< marks a line in the stderr section.
    exRESULT_TOKEN_END,		///< never returned.  this token is always one higher than the highest-numbered section token.

    exNEW = 0x100,			///< flag added to the section token that specifies that this is the start of a new section.
};


#define EX_TOKEN(x) ((x)&0xFF)
#define EX_ISNEW(x) ((x)&exNEW)
#define is_command_token(x) (EX_TOKEN(x)>=exCOMMAND && EX_TOKEN(x)<exCOMMAND_TOKEN_END)
#define is_section_token(x) (EX_TOKEN(x)>=exSTDOUT && EX_TOKEN(x)<exRESULT_TOKEN_END)

scanstate* tfscan_attach(scanstate *ss);

