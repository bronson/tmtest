/* memfile.h
 * Scott Bronson
 * 1 Jan 2005
 *
 * Just a chunk of memory that will grow to acoomodate
 * all data written.
 */


typedef struct {
    char *writeptr;
    char *bufptr;
    char *buflimit;
} memfile;


memfile* memfile_alloc(size_t initial_size);
void memfile_free(memfile *mm);
void memfile_realloc(memfile *mm, size_t len);

void memfile_write(memfile *mm, const char *ptr, size_t len);
void memfile_freeze(memfile *mm);

