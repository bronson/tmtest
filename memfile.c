/* memfile.c
 * Scott Bronson
 * 1 Jan 2005
 *
 * Just a chunk of memory that will grow to acoomodate
 * all data written.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "memfile.h"



/** Allocates and initializes the memfile for use.
 *
 * Pass 0 for initial_size to hold off allocating any memory
 * for the memfile until the first write (or you call memfile_realloc).
 *
 * Note that you never need to check for failed allocations.
 * Memfile just calls exit if anything goes wrong.
 */

memfile* memfile_alloc(size_t initial_size)
{
    memfile *mm = malloc(sizeof(memfile));
    if(!mm) {
        return NULL;
    }

    memset(mm, 0, sizeof(memfile));

    if(initial_size) {
        memfile_realloc(mm, initial_size);
    }

    return mm;
}


/** Releases the resources used by the memfile.
*/

void memfile_free(memfile *mm)
{
    free(mm->bufptr);
    free(mm);
}


/** Call realloc to shrink the buffer down to exactly the size of
 * the data it contains.  This is useful to free up a little memory
 * when you know you won't be writing to the memfile anymore.
 */

void memfile_realloc(memfile *mm, size_t newlen)
{
    size_t oldlen = mm->writeptr - mm->bufptr;
    mm->bufptr = realloc(mm->bufptr, newlen);
    if(!mm->bufptr) {
        fprintf(stderr, "Could not resize memfile from %d to %d bytes: %s\n",
                oldlen, newlen, strerror(errno));
        exit(100);  // TODO consolidate exit codes somehow
    }

    if(oldlen > newlen) {
        oldlen = newlen;
    }

    mm->buflimit = mm->bufptr + newlen;
    mm->writeptr = mm->bufptr + oldlen;
}


/** Appends a chunk of data to the memfile.
 *
 * There's no need for a read routine because you can access the
 * data directly (it's all the bytes between bufptr and writeptr).
 */

void memfile_write(memfile *mm, const char *ptr, size_t len)
{
    size_t avail = mm->buflimit - mm->writeptr;
    if(avail < len) {
        memfile_realloc(mm, mm->writeptr - mm->bufptr + len);
    }

    memcpy(mm->writeptr, ptr, len);
    mm->writeptr += len;
}


void memfile_freeze(memfile *mm)
{
    memfile_realloc(mm, mm->writeptr - mm->bufptr);
}


