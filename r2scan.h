/* r2scan.h
 * Scott Bronson
 * 27 Dec 2004
 *
 * This part of support code to make writing re2c scanners much easier.
 */

/** @file r2scan.h
 *
 * This is the central file for the readers.  They provide data
 * for scanners.
 *
 * NOTES
 *
 * All charptrs in the scanstate structure are declared const to help
 * ensure that you don't
 * accidentally end up modifying the buffer as it's being scanned.
 * This means that your read routine must cast them to be mutable
 * (char*) before read them.
 *
 * TERMINOLOGY
 *
 * allocate: scanstates can be dynamically (dynamicscan_create()) or
 * statically (scanstate_init()) allocated.  The buffers they use to
 * hold data may also be either dynamic or static.  Of course, any
 * time you allocate something dynamically, you must call the
 * corresponding free routine when you're done with it.
 *
 * attach: when the scanner is first initialized (scanstate_init())
 * or allocated (dynamicscan_create()), it is blank.  Trying to
 * pass it to a scanner would result in an assert or a crash.
 * You need to first attach a READER to provide data.
 *
 * initialize: prepare an already-allocated scanner for use.
 * After initializing it, you must ATTACH the scanner to a
 * READER.
 *
 * reader: reads data into the scanstate for the scanner.
 * Examples are readmem.c (read from a contiguous block in
 * memory), readfp.c (read from a FILE*), readfd.c (read
 * from a Unix file descriptor), etc.
 *
 * scanner: the function that actually performs the scanning.
 * It may or may not be written with the assistance of re2c.
 * It accepts a scanstate data structure and returns the next
 * token in the stream.
 *
 * scanstate: the data structure that retains complete state for the
 * scanner.  Scanners are thread safe: they never, ever use global
 * state.
 */


#ifndef R2SCAN_H
#define R2SCAN_H

#include <assert.h>


// for re2c...
#define YYCTYPE     char
#define YYCURSOR    (ss->cursor)
#define YYLIMIT     (ss->limit)
#define YYMARKER    (ss->marker)
#define YYFILL(n)   do {                    \
        int tt;                             \
        assert(ss->read);                  \
        tt = (*ss->read)(ss,n);           \
        assert(tt <= 0 || tt >= n);         \
        if(tt <= 0) return tt;              \
    } while(0)


// forward declaration
struct scanstate;


/** Prototype of read function
 *
 * You only need to read this if you're writing your own read functions.
 *
 * This function is used to fetch more data for the scanner.  It must
 * first shift the pointers in ss to make room (see read_shiftbuffer())
 * then load new data into the unused bytes at the end of the buffer.
 *
 * This routine returns 0 when there's no more data (EOF).
 * If it returns a value less than 0, that value will be returned
 * to the caller instead of a token.  This can indicate an error
 * condition, or just a situation such as EWOULDBLOCK.
 *
 * minbytes tells the minimum number of bytes that must be loaded.  If
 * you cannot load at least this many bytes, you must return 0.  It is
 * illegal for the scanner to return value between 0 and minbytes.
 * If you hit eof, and you can't satisfy minbytes, then return 0.
 * Otherwise, negative means error, and positive means the read was
 * successful and the scanner now has enough data to continue.
 *
 * All charptrs in the scanstate structure are declared const to help
 * ensure that you don't
 * accidentally end up modifying the buffer as it's being scanned.
 * This means that your read routine must cast them to be mutable
 * (char*) before reading them.
 *
 * It's too bad this declaration can't be in r2read.c.
 */

typedef int (*readproc)(struct scanstate *ss, int minbytes);


/** Prototype of scanner function
 *
 * A scanner is simply a function that accepts a scanstate
 * object and returns the next token in that stream.
 * The function will typically be generated with the
 * assistance of re2c, but it doesn't have to be!
 *
 * Once you have created the scanstate data structure,
 * pass it to the scanner.  If the scanner returns 0,
 * you hit EOF.  If the scanner returns a negative number,
 * then some sort of error was encountered.  Or, if you're
 * doing nonblocking I/O, then it just might mean that this
 * there's not enough data available to determine the next
 * token.
 */

typedef int (*scanproc)(struct scanstate *ss);


/** Represents the current state for a single scanner.
 *
 * This structure is used by a scanner to preserve its state.
 *
 * All charptrs are declared const to help ensure that you don't
 * accidentally end up modifying the buffer as it's being scanned.
 * This means that any time you want to read the buffer, you need
 * to cast the pointers to be nonconst.
 */


typedef struct scanstate {
    const char *cursor;   ///< The current character being looked at by the scanner
    const char *limit;    ///< The last valid character in the current buffer.  If the previous read was short, then this will not point to the end of the actual buffer (bufptr + bufsiz).
    const char *marker;   ///< Used internally by re2c engine to handle backtracking.

    // (these do a poor job of simulating capturing parens)
    const char *token;    ///< The start of the current token (manually updated by the scanner).
    const char *tokend;   ///< The end of the current token

    const char *bufptr;   ///< The buffer currently in use
    int bufsiz;         ///< The maximum number of bytes that the buffer can hold

    void *readref;      ///< Data specific to the reader (i.e. for readfp_attach() it's a FILE*).
    readproc read;      ///< The routine the scanner calls when the buffer needs to be reread.

    void *scanref;      ///< Data specific to the scanner
    scanproc state;     ///< some scanners are made up of multiple individual scan routines.  They store their state here.
    int line;           ///< The scanner may or may not use this to tell the current line number.
} scanstate;


void scanstate_init(scanstate *ss, const char *bufptr, int bufsiz);


#define get_next_token(ss) ((*((ss)->state))(ss))

#endif

