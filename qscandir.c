/* qscandir.c
 * libc's scandir is poorly standardized
 * so I'll use a public domain version.
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
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "qscandir.h"


/* Initial guess at directory allocated.  */
#define INITIAL_ALLOCATION 20


int qalphasort(struct dirent **d1, struct dirent **d2)
{
    return strcmp(d1[0]->d_name, d2[0]->d_name);
}


int qscandir(const char *directory_name,
        struct dirent ***array_pointer,
        int (*select_function) (const struct dirent *),
        int (*compare_function) (struct dirent **d1, struct dirent **d2)
        )
{
    DIR *directory;
    struct dirent **array;
    struct dirent *entry;
    struct dirent *copy;
    int allocated = INITIAL_ALLOCATION;
    int counter = 0;

    /* Get initial list space and open directory.  */

    directory = opendir (directory_name);
    if(directory == NULL) return -1;

    array = (struct dirent **) malloc (allocated * sizeof (struct dirent *));
    if(array == NULL) return -1;

    /* Read entries in the directory.  */

    while (entry = readdir (directory), entry) {
        if (select_function == NULL || (*select_function)(entry)) {
            /* User wants them all, or he wants this one.  Copy the entry.  */
            copy = (struct dirent *) malloc (sizeof (struct dirent));
            if (copy == NULL) {
                closedir (directory);
                free (array);
                return -1;
            }
            copy->d_ino = entry->d_ino;
            copy->d_reclen = entry->d_reclen;
            strcpy (copy->d_name, entry->d_name);

            /* Save the copy.  */

            if (counter + 1 == allocated)
            {
                allocated <<= 1;
                array = (struct dirent **)
                    realloc ((char *) array, allocated * sizeof (struct dirent *));
                if (array == NULL)
                {
                    closedir (directory);
                    free (array);
                    free (copy);
                    return -1;
                }
            }
            array[counter++] = copy;
        }
    }

    /* Close things off.  */

    array[counter] = NULL;
    *array_pointer = array;
    closedir (directory);

    /* Sort?  */

    if(counter > 1 && compare_function)
        qsort(array, counter, sizeof(struct dirent*), (void*)compare_function);

    return counter;
}

