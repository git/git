#ifndef RANGE_DIFF_H
#define RANGE_DIFF_H

#include "diff.h"
#include "argv-array.h"

#define RANGE_DIFF_CREATION_FACTOR_DEFAULT 60

/*
 * Compare series of commits in RANGE1 and RANGE2, and emit to the
 * standard output.  NULL can be passed to DIFFOPT to use the built-in
 * default.
 */
int show_range_diff(const char *range1, const char *range2,
		    int creation_factor, int dual_color,
		    const struct diff_options *diffopt,
		    const struct argv_array *other_arg);

#endif
