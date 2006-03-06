/* read-rand.h
 * Scott Bronson
 * 6 Mar 2006
 *
 * This allows you to feed an re2c scanner with random numbers.
 */


#include "read.h"


scanstate* readrand_attach(scanstate *ss, int seed);

