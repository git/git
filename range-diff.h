#ifndef RANGE_DIFF_H
#define RANGE_DIFF_H

#include "diff.h"

#define RANGE_DIFF_CREATION_FACTOR_DEFAULT 60

/*
 * Compare series of commmits in RANGE1 and RANGE2, and emit to the
 * standard output.  NULL can be passed to DIFFOPT to use the built-in
 * default.
 */
int show_range_diff(const char *range1, const char *range2,
		    int creation_factor, int dual_color,
		    struct diff_options *diffopt);

#endif
