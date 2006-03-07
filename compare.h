/* compare.h
 * Scott Bronson
 * 31 Dec 2004
 *
 * See compare.c for license.
 */

#include "re2c/scan.h"


/**
 * Unless you get a return value of cmp_full_match, the streams
 * were not exactly equal.
 */

typedef enum {
	cmp_in_progress = -1,	///< internal state; will never be returned.
	cmp_full_match = 0,		///< data doesn't match at all
	cmp_no_match,			///< data matches exactly
	cmp_ptr_has_extra_nl,	///< the scanner has an extra newline
	cmp_ptr_has_more_nls,	///< ok, this is a problem...  the app needs to know if the ss ended in a newline so it can suppress the -n warning.  So, cmp_ss_has_extra_nl means that ss didn't end in a nl, ptr did, but other than that they were identical.  cm_ptr_has_more_nls means that ss did end in a nl, ptr did too, and ptr and ss were exactly the same except ptr has one more newline.  In summary: both of these mean that ptr has one more nl than ss.  It's just whether ss ended with a newline (cmp_ptr_has_more_nls) or not (cmp_ptr_has_extra_nl).
	cmp_ss_has_extra_nl,	///< the data passed to continue has ex. CR
	cmp_ss_has_more_data	///< ptr and ss matched up to now
} compare_result;


void compare_attach(scanstate *ss);
int compare_continue(scanstate *ss, const char *ptr, size_t len);
compare_result compare_check(scanstate *ss);
compare_result compare_check_newlines(scanstate *ss);
