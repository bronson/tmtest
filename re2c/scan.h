/* scan.h
 * Scott Bronson
 * 27 Dec 2004
 *
 * This part of support code to make writing re2c scanners much easier.
 *
 * TODO: add dispose procs.  Normally these will just be null but
 * if they're set, they will ensure that all resources are collected.
 *
 * TODO: probably want to split the re2c-specific code from the general
 * code.  This file is overall very useful, but it's got a few limitations
 * imposed by re2c that should probably be placed in its own layer.
 * That way, future versions of re2c won't have to suffer the same
 * limitations.
 */

// to pull in the definition for size_t
#include <sys/types.h>


/** @file scan.h
 *
 * This is the central file for the readers.  They provide data
 * for scanners.
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


// for re2c...
#define YYCTYPE     char
#define YYCURSOR    (ss->cursor)
#define YYLIMIT     (ss->limit)
#define YYMARKER    (ss->marker)

/** Fills the scan buffer with more data.
 *
 * This routine needs to force a return if 0 bytes were read because
 * otherwise the re2c scanner will end up scanning garbage way off
 * the end of the buffer.  There's no (good) way to tell the scanner
 * "the file is at eof so just finish the token that you're on" (right?).
 * It will always lose the token at the end of the file unless the file
 * ends in a token delimiter (usually a newline).
 *
 * We ignore n because there can be less than n bytes left in the file,
 * yet one or more tokens will still match.  Therefore, we should always
 * read as much data as we can, and we should return success even if we
 * have less than n bytes in the buffer.  N is totally useless.
 *
 * The last line is the limitation.  If it weren't there, YYFILL would
 * return with an empty buffer so re2c would know it's at EOF and
 * shut down gracefully.  But re2c can't handle that.
 *
 * If you're using the re2c lib but writing your own re2c scanners,
 * call ss->read directly.
 */

#define YYFILL(n)   do { \
		ssize_t r = (*ss->read)(ss); \
		if(r < 0) return r; \
		if((ss)->cursor >= (ss)->limit) return 0; \
	} while(0);


// forward declaration
struct scanstate;


/** Prototype of read function
 *
 * You only need to know this if you're writing your own read functions.
 *
 * This function is used to fetch more data for the scanner.  It must
 * first shift the pointers in ss to make room (see read_shiftbuffer())
 * then load new data into the unused bytes at the end of the buffer.
 *
 * I chose the shift technique over a ringbuffer because we should rarely
 * have to shift data.  If you find that your file has gigantic tokens
 * and you're burning a lot of cpu shifting partial tokens from the end
 * of the buffer to the start, you might want to use a ring buffer instead
 * of a shift buffer.  However, re2c itself can't handle ringbuffers or
 * split tokens (nor can most scanners that I'm aware of), so shift
 * buffers are the best we can do.
 *
 * This routine returns 0 when there's no more data (EOF).
 * If it returns a value less than 0, that value will be returned
 * to the caller instead of a token.  This can indicate an error
 * condition, or just a situation such as EWOULDBLOCK.
 *
 * Because of the way re2c handles buffering, it's possible for the
 * read routine to be called multiple times after it has returned eof.
 * This isn't an error.  If your read routine is called when
 * ss->at_eof is true, you should just return without doing anything.
 *
 * All charptrs in the scanstate structure are declared const to help
 * ensure that you don't
 * accidentally end up modifying the buffer as it's being scanned.
 * This means that your read routine must cast them to be mutable
 * (char*) before reading them.  Only the readproc should modify the
 * data that's in the scan buffer.
 *
 * The caller assumes that the read routine will always fill the buffer
 * up as much as possible.  Therefore, if the buffer isn't entirely full,
 * then it knows that the EOF is probably at the end of the data.  This
 * is a fine assumption for files but not so good for pipes, network
 * sockets, anything that is packetized or works in realtime.  It would
 * take a rewrite of re2c to remove this limitation. So, yes, your
 * scanner can assume that the read routine will always fill the buffer
 * up as much as it possibly can.
 */

typedef ssize_t (*readproc)(struct scanstate *ss);


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

struct scanstate {
    const char *cursor; ///< The current character being looked at by the scanner
    const char *limit;  ///< The last valid character in the current buffer.  If the previous read was short, then this will not point to the end of the actual buffer (bufptr + bufsiz).
    const char *marker; ///< Used internally by re2c engine to handle backtracking.

    // (these do a poor job of simulating capturing parens)
    const char *token;  ///< The start of the current token (manually updated by the scanner).
    int line;           ///< The scanner may or may not maintain the current line number in this field.
    int at_eof;         ///< You almost certainly don't want to be using this unless you're writing a readproc.  Use scan_finished() instead.  This field gets set when the readproc realizes it has hit eof.  by convention 1 means eof, 2 means yes, at_eof is true, but because of an unrecoverable read error (todo: this should be formalized?  TODO: audit code to see if this is indeed the case).

    const char *bufptr; ///< The buffer currently in use
    size_t bufsiz;         ///< The maximum number of bytes that the buffer can hold

    void *readref;      ///< Data specific to the reader (i.e. for readfp_attach() it's a FILE*).
    readproc read;      ///< The routine the scanner calls when the buffer needs to be reread.

    void *scanref;      ///< Data specific to the scanner
    scanproc state;     ///< The entrypoint for the scanning routine.  The name is now anachronistic but might still fit (some scanners are made up of multiple individual scan routines -- they store their state here).

    void *userref;      ///< Never touched by any of our routines (except scanstate_init, which clears both fields).  This can be used to associate a parser with this scanner.
    void *userproc;     ///< That's just a suggestion though.  These fields are totally under your control.
};
typedef struct scanstate scanstate;


void scanstate_init(scanstate *ss, const char *bufptr, size_t bufsiz);
void scanstate_reset(scanstate *ss);


/** Returns true when there's no more data to be scanned.
 *
 * How what this macro does:
 *
 * If there's still more data in the buffer, then we're not finished.
 * If there's no data in the buffer and we're at EOF, then we're finished.
 * If there's no data in the buffer but we're not at eof, then we need
 * to execute a read to see if there's more data available.  If so, we're
 * not finished.  Otherwise, we're all done.
 *
 * Potential problem: this routine might call read, and if read returns
 * an error token, you'll miss it.  So, only call this routine when
 * you're sure you're at EOF anyway.  To find out if you're at EOF
 * without missing the errors, just call scan_token() and see if it
 * returns 0.
 *
 * TODO: should this routine be removed entirely?
 */

#define scan_is_finished(ss) \
    (((ss)->cursor < (ss)->limit) ? 0 : \
		 ((ss)->at_eof || ((*(ss)->read)(ss) <= 0)) \
    )


/** Fetches the next token in the stream from the scanner.
 */

#define scan_next_token(ss) ((*((ss)->state))(ss))


/** Returns a pointer to the first character of the
 *  most recently scanned token.
 */

#define token_start(ss) ((ss)->token)
#define current_token_start(ss) ((ss)->token)

/** Returns a pointer to one past the last character of the
 *  most recently scanned token.
 *
 *  token_end(ss) - token_start(ss) == token_length(ss)
 */

#define token_end(ss) ((ss)->cursor)
#define current_token_end(ss) ((ss)->cursor)

/** Returns the length of the most recently scanned token.
 *  (on all compilers this should be a signed long int)
 */

#define token_length(ss) ((ss)->cursor - (ss)->token)
#define current_token_length(ss) ((ss)->cursor - (ss)->token)

/** Returns the current token in a malloc'd buffer.
 * (just calls strdup(3) internally).
 */

#define token_dup(ss) strndup(token_start(ss), token_length(ss))
#define current_token_dup(ss) token_dup(ss)


/** Pushes the current token back onto the stream
 *
 * Calling scan_pushback returns the scanner to the state it had
 * just before returning the current token.  If you decide that
 * you don't want to handle this token, you can push it back and
 * it will be returned again the next time scan_token() is called.
 *
 * Note that this only works once.  You cannot push multiple tokens back
 * into the scanner.  Also, the scanner may have internal state of its
 * own that does not get reset.  If so, the scanner may or may not provide
 * a routine to back its internal state up as well.  Beware!!
 *
 * Finally, this doesn't back the line number up.  If you're pushing
 * a token back and you care about having the correct line nubmer,
 * then you'll have to manually restore the line number to what it
 * was before you scanned the token that you're pushing back.
 *
 * i.e.
 *
 *     // First ensure that the scanner you're using doesn't
 *     // have internal state that will be screwed up if you
 *     // re-scan the current token!
 *
 *     oldline = ss->line;
 *     tok = scan_token(ss);
 *     if(tok == push_me_back) {
 *         scan_pushback(ss);
 *         ss->line = oldline;
 *     }
 *
 * Yes, it takes some effort to call this function safely.
 * But it can be worth it when you need it.
 */

#define scan_pushback(ss) ((ss)->cursor = (ss)->token)


/**
 * Sets the current line number in the scanner to the given value.
 * I think most people will just manipulate ss->line directly.
 * TODO: get rid of this macro?
 */

#define scan_set_line(ss,n) (ss->line=(n));


/** Increments the current line number by 1.
 */

#define scan_inc_line(ss)   (ss->line++);


/**
 * Scanners only!  Prepares the scanner to scan a new token.
 * This should be called at the beginning of every scanproc
 * and nowhere else.
 */

#define scanner_enter(ss) ((ss)->token = (ss)->cursor)

#endif

