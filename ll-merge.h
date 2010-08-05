/*
 * Low level 3-way in-core file merge.
 */

#ifndef LL_MERGE_H
#define LL_MERGE_H

#define LL_OPT_VIRTUAL_ANCESTOR	(1 << 0)
#define LL_OPT_FAVOR_MASK	((1 << 1) | (1 << 2))
#define LL_OPT_FAVOR_SHIFT 1
#define LL_OPT_RENORMALIZE	(1 << 3)

static inline int ll_opt_favor(int flag)
{
	return (flag & LL_OPT_FAVOR_MASK) >> LL_OPT_FAVOR_SHIFT;
}

static inline int create_ll_flag(int favor)
{
	return ((favor << LL_OPT_FAVOR_SHIFT) & LL_OPT_FAVOR_MASK);
}

int ll_merge(mmbuffer_t *result_buf,
	     const char *path,
	     mmfile_t *ancestor, const char *ancestor_label,
	     mmfile_t *ours, const char *our_label,
	     mmfile_t *theirs, const char *their_label,
	     int flag);

int ll_merge_marker_size(const char *path);

#endif
