/* stscan.h
 * Scott Bronson
 */

/** @file stscan.h
 *
 * This file declares the interface to the status file scanner.
 */

#include "re2c/scan.h"


/** This lists all the tokens that the status file scanner may return.
 */

enum {
    stSTART = 1,	///< marks the start of the status file.  this is the first thing we do.  no arguments.  always printed.
	stCONFIG,		///< gives the name of the config file we're about to read ("CONFIG: file").  printed once per config file read.
	stPREPARE,		///< we're done reading config files, so we're preparing to run the test.  no arguments, always printed.
	stRUNNING,		///< says we're running the test.  the argument gives the name of the testfile we're running (a single dash if the file was supplied over stdin).  always printed.
	stDONE,			///< and we're done.  this is the last thing we do.  always printed, no args.

	stGARBAGE,		///< returned if we couldn't recognize the status entry.  the line should probably be ignored and we move on.
};

scanstate* stscan_attach(scanstate *ss);

