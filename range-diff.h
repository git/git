#ifndef RANGE_DIFF_H
#define RANGE_DIFF_H

#include "diff.h"

#define RANGE_DIFF_CREATION_FACTOR_DEFAULT 60

int show_range_diff(const char *range1, const char *range2,
		    int creation_factor, int dual_color,
		    struct diff_options *diffopt);

#endif
