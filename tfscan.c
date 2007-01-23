/* tfscan.c
 * Scott Bronson
 * 30 Dec 2004
 *
 * Scanner for test files.  This used to be a re2c scanner but
 * I couldn't get it to work with no LF at the end of file.
 *
 * This file is covered by the MIT License.
 */

// STDOUT\n starts a new section.  STDOUT my be followed by
// either whitespace or a colon -- nothing else.  If it's followed
// by anything else, it's interpreted as data.


// TOTEST: >8K token not containing a cr
// STDOUT:, STDERR:, etc at the EOF with no data.
// STDOUT at the beginning of the file.
// keyword without a colon
// 		NO NO NO keyword without a colon is still the keyword.
// 		And a keyword without a NL is still the keyword.
// 		But it must always start at the beginning of a new line.
// exit clauses with invalid numbers
// DOS/Mac/Unix line endings.
// 		What happes when platform doesn't match the testfile?
// 	Get rid of rewrite_command_section

#include "tfscan.h"


#define START(x) (ss->scanref=(void*)(long int)(x))

#ifndef NULL
#define NULL ((void*)0)
#endif


int tfscan_tok_start(scanstate *ss);
int tfscan_nontok_start(scanstate *ss);


/*!re2c
 
  // The following is almost the scanner that this file implements, except that
  // this file handles data at EOF correctly.

WS      = [ \t];
ANYN    = [\000-\377]\[\n];

"STDOUT" WS* ":" ANYN* "\n"  { START(exSTDOUT); return exNEW|exSTDOUT; }
"STDERR" WS* ":" ANYN* "\n"  { START(exSTDERR); return exNEW|exSTDERR; }
"RESULT" WS* ":" ANYN* "\n"  { START(exRESULT); return exNEW|exRESULT; }

ANYN* "\n"                  { return (int)ss->scanref; }


// What it does:
// Returns exCOMMAND for all data chunks.
// When it sees the start of a section, it returns exNEW|TOK
// (i.e. (exNEW|exSTDOUT).  This tells you that the old section
// has ended and a new one is starting.  Then it continues
// returning exSTDOUT without the exNEW flag until another
// seciton starts.

*/


static int nontok_start(scanstate *ss)
{
	if(YYCURSOR >= YYLIMIT) {
		int r = (*ss->read)(ss);
		// if there was an error, return an error token.
		if(r < 0) return r;
		// if we're completely out of data, return eof.
		// (this is why we can't use re2c for this scanner)
		if(ss->token >= ss->limit) return 0;
	}

	// Since it's impossible to have a token at this point so we
	// scan forward to the next CR/LF.
	while(YYCURSOR < YYLIMIT) {
		if(*YYCURSOR == '\r' || *YYCURSOR == '\n') break;
		YYCURSOR++;
	}
	if(YYCURSOR >= YYLIMIT) {
		// We have to assume that we previously read as much data as
		// possible.  So the entire buffer is just data with no tokens
		// and no CR/LF.
		return (long int)ss->scanref;
	}

	if(*YYCURSOR == '\r') YYCURSOR++;
	if(*YYCURSOR == '\n') YYCURSOR++;

    ss->line += 1;

	// We have potential for finding a token at this point.
	ss->state = tfscan_tok_start;
	return (long int)ss->scanref;
}



static int scan_to_end_of_keyword(scanstate *ss, int tok)
{
    int r;

	// We assume that we're immediately at the end of a keyword
	// section.  The first six bytes just guarantees the keyword.

	// skip all characters up to the final nl.

    // there's a chance we can be called with an empty buffer.
    // If so, we need to fill it before proceeding.
    if(YYCURSOR >= YYLIMIT) {
        r = (*ss->read)(ss);
        if(r < 0) return r;
        // if we're at eof, then the current token is just data.
        if(r == 0) return (long int)ss->scanref;
    }

    if(*YYCURSOR != '\r' && *YYCURSOR != '\n' && *YYCURSOR != ':' &&
            *YYCURSOR != ' ' && *YYCURSOR != '\t')
    {
        // We had a keyword but it didn't end in a proper delimiter.
        // Therefore, it's data, not a keyword.
        ss->state = tfscan_nontok_start;
        return nontok_start(ss);
    }

	while(*YYCURSOR != '\r' && *YYCURSOR != '\n') {
		YYCURSOR++;
		if(YYCURSOR >= YYLIMIT) {
            // try to fill the buffer (maybe it's a really long keyword)
            r = (*ss->read)(ss);
            if(r < 0) return r;
            // if we're at eof, then the current token is just data.
            if(r == 0) return (long int)ss->scanref;
		}
	}

	if(*YYCURSOR == '\r') YYCURSOR++;
	if(*YYCURSOR == '\n') YYCURSOR++;
    ss->line += 1;

	START(tok);
	return exNEW|tok;
}


/* When it finds a line that begins
 * with a new section, it returns the token name of that section
 * with the exNEW flag turned on.  After that, it returns each
 * line in the section with the token's identifier.  Then, when it
 * finds a new section, you get a exNEW+TOKEN of the new section.
 */

int tfscan_tok_start(scanstate *ss)
{
    int r;

    scanner_enter(ss);

    // if we can read at least 8 more bytes from the current buffer,
    // we won't bother reloading it.  This should cut down drastically
    // on the number of small reads we make.  The constant in the if
    // statement is an arbitrary number; if we have less than that
    // number of bytes available in the buffer, we read some more data.
	if(YYCURSOR+16 >= YYLIMIT) {
		r = (*ss->read)(ss);
		// if there was an error, return an error token.
		if(r < 0) return r;
		// Only if we're _completely_ out of data, return eof.
		// (this is why we can't use re2c for this scanner)
        // we can handle cursor==limit; we just return the
        // final token.  But, if token==limit, we're out of data.
        // (except, at this point in the function, cursor == token).
		if(ss->token >= ss->limit) return 0;
	}

	// At this point in the scanner, we know that we are at the beginning
    // of a line (previous character was either start-of-file or \n).
	// So check to see if there's a token.

	if(YYCURSOR + 7 <= YYLIMIT) {
		// There's enough data in this buffer to contain a keyword.
		// If there are less than 8 bytes in the buffer then it means
		// that we're 7 bytes from the EOF and there's no chance that
		// there's another keyword to scan.  (6 bytes for the keyword,
		// 1 byte for the colon, one byte for the newline).
		switch(*YYCURSOR) {
			case 'S':
				if(YYCURSOR[1] == 'T' && YYCURSOR[2] == 'D') {
					if(YYCURSOR[3]=='O' && YYCURSOR[4]=='U' && YYCURSOR[5]=='T') {
                        YYCURSOR += 6;
						return scan_to_end_of_keyword(ss, exSTDOUT);
					}
					if(YYCURSOR[3]=='E' && YYCURSOR[4]=='R' && YYCURSOR[5]=='R') {
                        YYCURSOR += 6;
						return scan_to_end_of_keyword(ss, exSTDERR);
					}
				}
				// else it wasn't a token so we can just keep scanning.
				break;
			case 'R':
				if(YYCURSOR[1]=='E' && YYCURSOR[2]=='S' &&
					YYCURSOR[3]=='U' && YYCURSOR[4]=='L' && YYCURSOR[5]=='T')
				{
                    YYCURSOR += 6;
					return scan_to_end_of_keyword(ss, exRESULT);
				}
				break;
			default:
				break;
		}
	}

	// So there wasn't a keyword at this point in the buffer.
	// We just treat it as random data.  Since we haven't moved the
    // cursor we can just call straight into the nontok routine.
	ss->state = tfscan_nontok_start;
	return tfscan_nontok_start(ss);
}


int tfscan_nontok_start(scanstate *ss)
{
	scanner_enter(ss);
    return nontok_start(ss);
}
	


/** Prepares the given scanner to scan a testfile.
 *
 *  @param ss the scanstate to attach to.  Passing NULL is safely ignored.
 *  @returns ss.  Always.  This routine makes no calls that can fail.
 */

scanstate* tfscan_attach(scanstate *ss)
{
    if(ss) {
        START(exCOMMAND);
        ss->state = tfscan_tok_start;
    }

    return ss;
}

/*

SOME MORE THOUGHTS ON SCANNING:


We know we're at the beginning of the buffer or immediately
after a newline.
	Do we have enough data for a keyword?
		No: fill buffer with more data.
	We know we have enough data for a keyword.  Use strstr.
	
See if a keyword is here.
If 


So what's the problem?
	Read might return:
		error, just return error code as negative number.
		0, eof.
			If we have more data in the buffer, we need to make sure
			to return that data.
			(actaully, with re2c, that's not the case.  It screws up the
			last token in the file unless the file ends on a token delimiter).
			- If there's no more data in the buffer then we just return 0.
		positive number: we read data.

So the problem is that re2c scanners can't handle it if the file doesn't
end on a token delim.  We can't return if we hit eol and haven't scanned
more data even if there's more data in the buffer.  The stupid re2c
scanner will just start scanning in the garbage past the end of the buffer.
Sigh.

That means I can't use it for the testfile scanner.

That furthermore means that I can't use YYFILL on the non-broken scanner.
I should probably add a different macro that can be used everywhere,
zero-length reads (as in a network packet), file ending with a non-delim
character, etc.

*/

