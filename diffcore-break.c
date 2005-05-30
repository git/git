/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"
#include "count-delta.h"

static int very_different(struct diff_filespec *src,
			  struct diff_filespec *dst,
			  int min_score)
{
	/* dst is recorded as a modification of src.  Are they so
	 * different that we are better off recording this as a pair
	 * of delete and create?  min_score is the minimum amount of
	 * new material that must exist in the dst and not in src for
	 * the pair to be considered a complete rewrite, and recommended
	 * to be set to a very high value, 99% or so.
	 *
	 * The value we return represents the amount of new material
	 * that is in dst and not in src.  We return 0 when we do not
	 * want to get the filepair broken.
	 */
	void *delta;
	unsigned long delta_size, base_size;

	if (!S_ISREG(src->mode) || !S_ISREG(dst->mode))
		return 0; /* leave symlink rename alone */

	if (diff_populate_filespec(src, 1) || diff_populate_filespec(dst, 1))
		return 0; /* error but caught downstream */

	delta_size = ((src->size < dst->size) ?
		      (dst->size - src->size) : (src->size - dst->size));

	/* Notice that we use max of src and dst as the base size,
	 * unlike rename similarity detection.  This is so that we do
	 * not mistake a large addition as a complete rewrite.
	 */
	base_size = ((src->size < dst->size) ? dst->size : src->size);

	/*
	 * If file size difference is too big compared to the
	 * base_size, we declare this a complete rewrite.
	 */
	if (base_size * min_score < delta_size * MAX_SCORE)
		return MAX_SCORE;

	if (diff_populate_filespec(src, 0) || diff_populate_filespec(dst, 0))
		return 0; /* error but caught downstream */

	delta = diff_delta(src->data, src->size,
			   dst->data, dst->size,
			   &delta_size);

	/* A delta that has a lot of literal additions would have
	 * big delta_size no matter what else it does.
	 */
	if (base_size * min_score < delta_size * MAX_SCORE)
		return MAX_SCORE;

	/* Estimate the edit size by interpreting delta. */
	delta_size = count_delta(delta, delta_size);
	free(delta);
	if (delta_size == UINT_MAX)
		return 0; /* error in delta computation */

	if (base_size < delta_size)
		return MAX_SCORE;

	return delta_size * MAX_SCORE / base_size; 
}

void diffcore_break(int min_score)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	int i;

	if (!min_score)
		min_score = DEFAULT_BREAK_SCORE;

	outq.nr = outq.alloc = 0;
	outq.queue = NULL;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		int score;

		/* We deal only with in-place edit of non directory.
		 * We do not break anything else.
		 */
		if (DIFF_FILE_VALID(p->one) && DIFF_FILE_VALID(p->two) &&
		    !S_ISDIR(p->one->mode) && !S_ISDIR(p->two->mode) &&
		    !strcmp(p->one->path, p->two->path)) {
			score = very_different(p->one, p->two, min_score);
			if (min_score <= score) {
				/* Split this into delete and create */
				struct diff_filespec *null_one, *null_two;
				struct diff_filepair *dp;

				/* deletion of one */
				null_one = alloc_filespec(p->one->path);
				dp = diff_queue(&outq, p->one, null_one);
				dp->score = score;
				dp->broken_pair = 1;

				/* creation of two */
				null_two = alloc_filespec(p->two->path);
				dp = diff_queue(&outq, null_two, p->two);
				dp->score = score;
				dp->broken_pair = 1;

				free(p); /* not diff_free_filepair(), we are
					  * reusing one and two here.
					  */
				continue;
			}
		}
		diff_q(&outq, p);
	}
	free(q->queue);
	*q = outq;

	return;
}
