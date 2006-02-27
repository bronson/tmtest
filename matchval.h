/* matchval.h
 * Scott Bronson
 * 31 Dec 2004
 *
 */

#ifndef MATCHVAL_H
#define MATCHVAL_H


/**
 * a tristate that tells whether something
 *    - matches
 *    - doesn't match
 *    - hasn't been checked yet.
 */

typedef enum {
    match_inprogress = -2,
    match_unknown = -1,
    match_no = 0,
    match_yes = 1,
} matchval;


#endif

