#ifndef RANGE_DIFF_H
#define RANGE_DIFF_H

#include "diff.h"
#include "strvec.h"

#define RANGE_DIFF_CREATION_FACTOR_DEFAULT 60
#define RANGE_DIFF_MAX_MEMORY_DEFAULT \
	(sizeof(void*) >= 8 ? \
		((size_t)(1024L * 1024L) * (size_t)(4L * 1024L)) : /* 4GB on 64-bit */ \
		((size_t)(1024L * 1024L) * (size_t)(2L * 1024L)))   /* 2GB on 32-bit */

/*
 * A much higher value than the default, when we KNOW we are comparing
 * the same series (e.g., used when format-patch calls range-diff).
 */
#define CREATION_FACTOR_FOR_THE_SAME_SERIES 999

struct range_diff_options {
	int creation_factor;
	unsigned dual_color:1;
	unsigned left_only:1, right_only:1;
	unsigned include_merges:1;
	size_t max_memory;
	const struct diff_options *diffopt; /* may be NULL */
	const struct strvec *log_arg; /* may be NULL */
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
