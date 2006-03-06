/* read-rand.c
 * Scott Bronson
 * 6 Mar 2006
 */

#include <stdlib.h>
#include <string.h>

#include "read-rand.h"


/**
 * The internal function that performs the read.  You never call
 * it directly.  Instead, it is called automatically by the scanner
 * whenever it needs more data.
 *
 * This reader will never hit EOF.  It just keeps supplying data
 * from rand_r(3).
 */

static ssize_t readrand_read(scanstate *ss)
{
    int avail = read_shiftbuf(ss);
	int orig = avail;
	int num;

	while(avail >= sizeof(int)) {
		// There appears to be some sort of cast bug in GCC4...?
		// Anyhow, the memcpy works, casting pointers didn't.
		num = rand_r((unsigned int*)&ss->readref);
		memcpy((char*)ss->limit, &num, sizeof(int));
		ss->limit += sizeof(int);
		avail -= sizeof(int);
	}

    return orig-avail;
}


/**
 * Attaches the seed to the existing scanstate object.
 * Cannot possibly fail.
 */

scanstate* readrand_attach(scanstate *ss, int seed)
{
    *(unsigned int*)&ss->readref = seed;
    ss->read = readrand_read;
    return ss;
}

