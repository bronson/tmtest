/* r2read-fd.c
 * Scott Bronson
 * 28 Dec 2004
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "r2scan-dyn.h"
#include "r2read-fd.h"


static int readfd_read(scanstate *ss)
{
    int n, avail;

    // try to avoid hammering on the file's eof.
    assert(!ss->at_eof);

    avail = read_shiftbuf(ss);

    // ensure we get a full read
    do {
        n = read((int)ss->readref, (void*)ss->limit, avail);
    } while(n < 0 && errno == EINTR);
    ss->limit += n;

    if(n == 0) {
        ss->at_eof = 1;
    }

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


/* Creates a new scanner and opens the given file.
 *
 * You need to call readfd_close() to close the file and
 * deallocate the scanner.
 *
 * Bufsiz tells how big the scan buffer will be.  No single
 * token may be larger than bufsiz (it will be broken up
 * and the scanner may return strange things if it is).
 */

scanstate* readfd_open(const char *path, int bufsiz)
{
    scanstate *ss;
    int fd;

    fd = open(path, O_RDONLY);
    if(fd < 0) {
        return NULL;
    }

    ss = dynscan_create(bufsiz);
    if(!ss) {
        close(fd);
        return NULL;
    }

    return readfd_attach(ss, fd);
}


void readfd_close(scanstate *ss)
{
    close((int)ss->readref);
    dynscan_free(ss);
}

