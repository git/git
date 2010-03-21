/*
 * Low level 3-way in-core file merge.
 */

#ifndef LL_MERGE_H
#define LL_MERGE_H

int ll_merge(mmbuffer_t *result_buf,
	     const char *path,
	     mmfile_t *ancestor, const char *ancestor_label,
	     mmfile_t *ours, const char *our_label,
	     mmfile_t *theirs, const char *their_label,
	     int flag);

int ll_merge_marker_size(const char *path);

#endif
