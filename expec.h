#include "r2scan.h"

/* This file declares the interface to the expected results test
 * file scanner.
 */


/** Tells the scanstate to use the expec scanner.
 * Simply returns its input argument.  It's legal to pass NULL.
 */

scanstate* expec_scanner_attach(scanstate *ss);


// tokens for the config bail checker
enum {
    exGARBAGE = 1,

    exSTDOUT = 0x10,
    exSTDERR,
    exRESULT,
    exMODIFY,

    exNEW = 0x100,
};


#define EX_TOKEN(x) ((x)&0xFF)
#define EX_ISNEW(x) ((x)&exNEW)

