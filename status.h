#include "r2scan.h"

/* @file r2scan.h
 * This file declares the interface to the status file parser.
 * It's actually a lot simpler than it looks -- if I could fit
 * multiple re2c parsers into a single file, this would all be
 * very straightforward.  Alas...
 */

/** Attaches the given scanstate to the config bailed scanner.
 * (it's now called the status scanner)
 *
 * The routine simply returns its input argument.
 * It's legal to pass NULL -- the routine simply returns NULL.
 */

scanstate* cb_scanner_attach(scanstate *ss);



// tokens for the config bail checker
enum {
    CBSTART = 1,
    CBCONFIG = 2,
    CBREADY = 3,
    CBRUNNING = 4,
    CBFILE = 5,
    GARBAGE = 100,
    NEWLINE = 101,
    WHITESPACE = 101,
};


// macros for the scanners

// because I'm used to lex...  This is how we enter the
// various start states.

#define STATE(st)           (cb_scanner_##st)
#define BEGIN(next)         (ss->state = STATE(next))
#define BEGINSUB(next)      (ss->scanref = ss->state, ss->state=STATE(next))
#define RETURN()            (ss->state = ss->scanref)

// to be used internally only
int cb_scanner_config(scanstate *ss);
int cb_scanner_file(scanstate *ss);
int cb_scanner_running(scanstate *ss);


