/* r2read-fd.h
 * Scott Bronson
 * 30 Dec 2004
 *
 * This allows you to feed an re2c scanner directly from a
 * Unix file descriptor.
 */


#include "r2read.h"


scanstate* readfd_attach(scanstate *ss, int fd);
scanstate* readfd_open(const char *path, int bufsiz);
void readfd_close(scanstate *ss);

