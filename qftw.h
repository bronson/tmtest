/** qftw.h
 * Scott Bronson
 *
 * Defines the entrypoint for the nftw code.
 */

struct stat;
typedef int (*qftwproc)(const char *file, struct stat *sb, int flag);

int qftw(const char *directory, qftwproc func, int depth);

