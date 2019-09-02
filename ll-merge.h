/*
 * Low level 3-way in-core file merge.
 */

#ifndef LL_MERGE_H
#define LL_MERGE_H

#include "xdiff/xdiff.h"

struct index_state;

struct ll_merge_options {
	unsigned virtual_ancestor : 1;
	unsigned variant : 2;	/* favor ours, favor theirs, or union merge */
	unsigned renormalize : 1;
	unsigned extra_marker_size;
	long xdl_opts;
};

int ll_merge(mmbuffer_t *result_buf,
	     const char *path,
	     mmfile_t *ancestor, const char *ancestor_label,
	     mmfile_t *ours, const char *our_label,
	     mmfile_t *theirs, const char *their_label,
	     struct index_state *istate,
	     const struct ll_merge_options *opts);

int ll_merge_marker_size(struct index_state *istate, const char *path);
void reset_merge_attributes(void);

#endif
