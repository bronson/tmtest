#include "r2scan.h"

/* This file declares the interface to the expected results test
 * file scanner.
 */


/** Tells the scanstate to use the expec scanner.
 * Simply returns its input argument.  It's legal to pass NULL.
 */

scanstate* tfscan_attach(scanstate *ss);


// tokens for the config bail checker
enum {
    // command sections are numbered between 1 and 63
    exCOMMAND = 1,

    exCOMMAND_TOKEN_END,

    // result sections are numbered from 64 through 127.
    exSTDOUT = 64,
    exSTDERR,
    exRESULT,
    exMODIFY,

    exRESULT_TOKEN_END,

    exNEW = 0x100,
};


#define EX_TOKEN(x) ((x)&0xFF)
#define EX_ISNEW(x) ((x)&exNEW)
#define is_command_token(x) (EX_TOKEN(x)>=exCOMMAND && EX_TOKEN(x)<=ex_COMMAND_TOKEN_END)
#define is_section_token(x) (EX_TOKEN(x)>=exSTDOUT && EX_TOKEN(x)<=exRESULT_TOKEN_END)

