#include "cache.h"
#include "commit.h"
#include "revision.h"
#include "interdiff.h"

void show_interdiff(struct rev_info *rev)
{
	struct diff_options opts;

	memcpy(&opts, &rev->diffopt, sizeof(opts));
	opts.output_format = DIFF_FORMAT_PATCH;
	diff_setup_done(&opts);

	diff_tree_oid(rev->idiff_oid1, rev->idiff_oid2, "", &opts);
	diffcore_std(&opts);
	diff_flush(&opts);
}
