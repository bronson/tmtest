/* qscandir.c
 * Scott Bronson
 * 29 Dec 2004
 *
 * This file, like its predecessor, is in the public domain.
 *
 * libc's scandir is poorly standardized so I'll use a public domain version.
 * well... And modify it to return an argv rather than a dirent array.
 * The only field you can portably use in the dirent is the name anyway.
 * This uses a whole lot less memory than the original version.
 */


/* Subject:	scandir, ftw REDUX
 * Date: 	1 Jan 88 00:47:01 GMT
 * From: 	rsalz@pebbles.bbn.com
 * Newsgroups:  comp.sources.misc
 *
 * ... these are public-domain implementations of the SystemV ftw()
 * routine, the BSD scandir() and alphasort() routines, and documentation for
 * same.  The FTW manpage could be more readable, but so it goes.
 *
 * Anyhow, feel free to post these, and incorporate them into your existing
 * packages.  I have readdir() routiens for MSDOS and the Amiga if anyone
 *  wants them, and should have them for VMS by the end of January; let me
 *  know if you want copies.
 *
 *                        Yours in filesystems,
 *                                /r$
 */


#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include "qscandir.h"


/* Initial guess at directory allocation.  Since we only allocate
 * 4 bytes per unused directory, this number can be quite large.
 */

#define INITIAL_ALLOCATION 100


int qdirentcmp(char **d1, char **d2)
{
    return strcmp(*d1, *d2);
}


int qdirentcoll(char **d1, char **d2)
{
    return strcoll(*d1, *d2);
}


char** qscandir(const char *directory_name,
        int (*select_function) (const struct dirent *),
        int (*compare_function) (char **d1, char **d2)
        )
{
    DIR *directory;
    struct dirent *entry;
    char **array;
    char *copy;
    int allocated = INITIAL_ALLOCATION;
    int counter = 0;

    /* Get initial list space and open directory.  */

    directory = opendir (directory_name);
    if(directory == NULL) {
        fprintf(stderr, "Could not access directory '%s': %s\n", 
                directory_name, strerror(errno));
        return NULL;
    }

    array = malloc (allocated * sizeof(char*));
    if(array == NULL) {
        fprintf(stderr, "qscandir(): could not allocate %lu bytes.\n",
                (unsigned long)(allocated * sizeof(char*)));
        return NULL;
    }

    /* Read entries in the directory.  */

    while (entry = readdir (directory), entry) {
        if (select_function == NULL || (*select_function)(entry)) {
            /* User wants them all, or he wants this one.  Copy the entry.  */
            copy = strdup(entry->d_name);
            if (copy == NULL) {
                fprintf(stderr, "qscandir(): could not allocate memory for %s\n",
                        entry->d_name);
                closedir (directory);
                free (array);
                return NULL;
            }

            if (counter + 1 == allocated)
            {
                allocated <<= 1;
                array = realloc (array, allocated * sizeof(char*));
                if (array == NULL)
                {
                    fprintf(stderr, "qscandir(): could not reallocate %lu bytes.\n",
                            (unsigned long)(allocated * sizeof(char*)));
                    closedir (directory);
                    free (array);
                    free (copy);
                    return NULL;
                }
            }
            array[counter++] = copy;
        }
    }

    /* Close things off.  */

    array[counter] = NULL;
    array = realloc(array, (counter+1) * sizeof(char*));
    closedir (directory);

    /* Sort?  */

    if(counter > 1 && compare_function)
        qsort(array, counter, sizeof(struct dirent*), (void*)compare_function);

    return array;
}

