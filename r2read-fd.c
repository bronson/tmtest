/* r2read-fd.c
 * Scott Bronson
 * 28 Dec 2004
 */

#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include "r2read-fd.h"


static int readfd_read(scanstate *ss, int minneeded)
{
    int n, avail;

    read_shiftbuf(ss);
    avail = ss->bufsiz - (ss->bufptr - ss->limit);

    // ensure we get a full read
    do {
        n = read((int)ss->readref, (void*)ss->cursor, avail);
    } while(n < 0 && errno == EINTR);
    ss->limit += n;

    // n > 0: good read
    // n == 0: eof
    // n < 0: error
    //
    // TODO: it's possible to return fewer bytes than minneeded.
    // is this a problem?

    return n;
}


/** Attaches the existing fd to the existing scanstate object.
 * Note that this routine checks the fd and if it's less than 0
 * (indicating an error) it returns null.
 *
 * If you pass it a valid fd, there's no way it will possibly fail.
 */

scanstate* readfd_attach(scanstate *ss, int fd)
{
    if(!ss || fd < 0) {
        return 0;
    }

    ss->readref = (void*)fd;
    ss->read = readfd_read;
    return ss;
}

