/* r2read.h
 * Scott Bronson
 * 27 Dec 2004
 *
 * This defines readers that read data into r2scanner buffers.
 */


#ifndef R2READ_H
#define R2READ_H

#include <assert.h>
#include "r2scan.h"

/** @file r2read.h
 *
 * The readproc is documented in r2scan.h.
 */

void read_shiftbuf(scanstate *ss);

#endif

