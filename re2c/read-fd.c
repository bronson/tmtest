/* read-fd.c
 * Scott Bronson
 * 28 Dec 2004
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include "read-fd.h"
#include "scan-dyn.h"


/**
 * The internal function that performs the read.  You never call
 * it directly.  Instead, it is called automatically by the scanner
 * whenever it needs more data.
 */

static ssize_t readfd_read(scanstate *ss)
{
    int n, avail;

	if(ss->at_eof) {
		// on some platforms, hammering on the eof can have bad consequences.
		return 0;
	}

    avail = read_shiftbuf(ss);

    // ensure we get a full read
    do {
        n = read((long int)ss->readref, (void*)ss->limit, avail);
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

    ss->readref = (void*)(long int)fd;
    ss->read = readfd_read;
    return ss;
}


/* Opens the file and creates a new scanner to scan it.
 * This is just a convenience routine.  You can create a scanner
 * yourself and attach to it using readfd_attach().
 *
 * If you do use this routine, you should call readfd_close() to close
 * the file and deallocate the scanner.
 *
 * Bufsiz tells how big in bytes the scan buffer will be.  No single
 * token may be larger than bufsiz.
 */

scanstate* readfd_open(const char *path, size_t bufsiz)
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


/**
 * Closes the file and deallocates the memory allocated by readfd_open().
 */

void readfd_close(scanstate *ss)
{
    close((long int)ss->readref);
    dynscan_free(ss);
}

