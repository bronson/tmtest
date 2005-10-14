/* read.h
 * Scott Bronson
 * 27 Dec 2004
 *
 * This defines utilities that should be used by readers.
 * (readers are responsible for filling scanner buffers with data from
 * various locations -- files, sockets, memory buffers, generators, etc).
 */


#ifndef R2READ_H
#define R2READ_H

#include <assert.h>

#include "scan.h"


int read_shiftbuf(scanstate *ss);

#endif

