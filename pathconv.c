/* Copyright (c) 1997 Shigio Yamaguchi. All rights reserved.
   Copyright (c) 1999 Tama Communications Corporation. All rights reserved.

   The Pathconvert Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The Pathconvert Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "pathconv.h"


/*
 * abs2rel: convert an absolute path name into relative.
 *
 *	i)	path	absolute path
 *	i)	base	base directory (must be absolute path)
 *	o)	result	result buffer
 *	i)	size	size of result buffer
 *	r)		!= NULL: relative path
 *			== NULL: error
 */
char *
abs2rel(path, base, result, size)
	const char *path;
	const char *base;
	char *result;
	const size_t size;
{
	const char *pp, *bp, *branch;
	/*
	 * endp points the last position which is safe in the result buffer.
	 */
	const char *endp = result + size - 1;
	char *rp;

	if (*path != '/') {
		if (strlen(path) >= size)
			goto erange;
		strcpy(result, path);
		goto finish;
	} else if (*base != '/' || !size) {
		errno = EINVAL;
		return (NULL);
	} else if (size == 1)
		goto erange;
	/*
	 * seek to branched point.
	 */
	branch = path;
	for (pp = path, bp = base; *pp && *bp && *pp == *bp; pp++, bp++)
		if (*pp == '/')
			branch = pp;
	if ((*pp == 0 || (*pp == '/' && *(pp + 1) == 0)) &&
	    (*bp == 0 || (*bp == '/' && *(bp + 1) == 0))) {
		rp = result;
		*rp++ = '.';
		if (*pp == '/' || *(pp - 1) == '/')
			*rp++ = '/';
		if (rp > endp)
			goto erange;
		*rp = 0;
		goto finish;
	}
	if ((*pp == 0 && *bp == '/') || (*pp == '/' && *bp == 0))
		branch = pp;
	/*
	 * up to root.
	 */
	rp = result;
	for (bp = base + (branch - path); *bp; bp++)
		if (*bp == '/' && *(bp + 1) != 0) {
			if (rp + 3 > endp)
				goto erange;
			*rp++ = '.';
			*rp++ = '.';
			*rp++ = '/';
		}
	if (rp > endp)
		goto erange;
	*rp = 0;
	/*
	 * down to leaf.
	 */
	if (*branch) {
		if (rp + strlen(branch + 1) > endp)
			goto erange;
		strcpy(rp, branch + 1);
	} else
		*--rp = 0;
finish:
	return result;
erange:
	errno = ERANGE;
	return (NULL);
}


// Copyright (c) 2006 Scott Bronson
// The following code can be distributed under the LGPL as above
// or, at your option, the much simpler MIT license.

/**
 * Normalizes the absolute path in-place.
 * A path can only get shorter when it's normalized, never longer.
 *
 * This is not the way I'd like to write this routine.  I'd like
 * to walk two pointers through the string copying bits from the
 * src to the dst so I don't spend so much time moving the same
 * bytes over and over.  Alas, I'm out of time so I'm going to
 * resort to this loathesome coding style for less chance of bugs.
 *
 * @returns 1 if this was a relative path and therefore the function
 * did nothing.  Otherwise returns 0.
 *
 * TODO: split this out into a unit and make some unit tests for it.
 */

int normalize_absolute_path(char *buf)
{
	char *cp;
	int end;

	if(buf[0] != '/') {
		// refuse to normalize a relative path
		return 1;
	}

	cp = buf + 1;
	end = strlen(buf) - 1;

	while(end > 0) {
		// we're just past the slash of the prev directory
		if(cp[0] == '/') {
			// turn '//' into '/'
			memmove(cp, cp+1, end);
			end -= 1;
		} else if(cp[0] == '.' && (cp[1] == '/' || end == 1)) {
			// turn '/./' into '/'
			memmove(cp, cp+2, end-1);
			end -= 2;
		} else if(cp[0] == '.' && cp[1] == '.' && (cp[2] == '/' || end == 2)) {
			// save our position
			char *src = cp+3;
			// back up one dir, not moving past the base dir
			cp -= 1;
			while(cp > buf+1 && cp[-1] != '/') cp -= 1;
			memmove(cp, src, end-2);
			end -= 3;
		} else {
			// skip a directory name
			while(*cp != '/' && end > 0) {
				cp += 1;
				end -= 1;
			}
			if(*cp == '/' && end > 0) {
				cp += 1;
				end -= 1;
			}
		}
	}

	if(cp[-1] == '/') {
		cp -= 1;
	}

	*cp = '\0';

	return 0;
}


