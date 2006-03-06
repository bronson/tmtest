/* re2c-mem.c
 * Scott Bronson
 * 30 Dec 2004
 *
 * Allows you to feed an re2c scanner from a memory block.
 */

#include "read.h"

scanstate* readmem_init(scanstate *ss, const char *data, size_t len);
scanstate* readmem_attach(scanstate *ss, const char *data, size_t len);

// convenience functions:
#define readmem_init_str(ss,str) readmem_init(ss,str,strlen(str))
#define readmem_init_strconst(ss,str) readmem_init(ss,str,sizeof(str)-1)

