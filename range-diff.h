#ifndef RANGE_DIFF_H
#define RANGE_DIFF_H

#include "diff.h"
#include "strvec.h"

#define RANGE_DIFF_CREATION_FACTOR_DEFAULT 60

struct range_diff_options {
	int creation_factor;
	unsigned dual_color:1;
	unsigned left_only:1, right_only:1;
	const struct diff_options *diffopt; /* may be NULL */
	const struct strvec *other_arg; /* may be NULL */
};

/*
 * Compare series of commits in `range1` and `range2`, and emit to the
 * standard output.
 */
int show_range_diff(const char *range1, const char *range2,
		    struct range_diff_options *opts);

/*
 * Determine whether the given argument is usable as a range argument of `git
 * range-diff`, e.g. A..B.
 */
int is_range_diff_range(const char *arg);

#endif
