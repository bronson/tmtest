/* qtemp.h
 * Scott Bronson
 * 29 Dec 2004
 */

int exists(const char *file);
char* gen_tempname(char *name);
char* qtempnam(const char *file, const char *suffix);

