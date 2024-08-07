/*
 * Copyright (C) 2005 Junio C Hamano
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "diffcore.h"
#include "hash.h"
#include "object.h"
#include "promisor-remote.h"

static int should_break(struct repository *r,
			struct diff_filespec *src,
			struct diff_filespec *dst,
			int break_score,
			int *merge_score_p)
{
	/* dst is recorded as a modification of src.  Are they so
	 * different that we are better off recording this as a pair
	 * of delete and create?
	 *
	 * There are two criteria used in this algorithm.  For the
	 * purposes of helping later rename/copy, we take both delete
	 * and insert into account and estimate the amount of "edit".
	 * If the edit is very large, we break this pair so that
	 * rename/copy can pick the pieces up to match with other
	 * files.
	 *
	 * On the other hand, we would want to ignore inserts for the
	 * pure "complete rewrite" detection.  As long as most of the
	 * existing contents were removed from the file, it is a
	 * complete rewrite, and if sizable chunk from the original
	 * still remains in the result, it is not a rewrite.  It does
	 * not matter how much or how little new material is added to
	 * the file.
	 *
	 * The score we leave for such a broken filepair uses the
	 * latter definition so that later clean-up stage can find the
	 * pieces that should not have been broken according to the
	 * latter definition after rename/copy runs, and merge the
	 * broken pair that have a score lower than given criteria
	 * back together.  The break operation itself happens
	 * according to the former definition.
	 *
	 * The minimum_edit parameter tells us when to break (the
	 * amount of "edit" required for us to consider breaking the
	 * pair).  We leave the amount of deletion in *merge_score_p
	 * when we return.
	 *
	 * The value we return is 1 if we want the pair to be broken,
	 * or 0 if we do not.
	 */
	unsigned long delta_size, max_size;
	unsigned long src_copied, literal_added, src_removed;

	struct diff_populate_filespec_options options = { 0 };

	*merge_score_p = 0; /* assume no deletion --- "do not break"
			     * is the default.
			     */

	if (S_ISREG(src->mode) != S_ISREG(dst->mode)) {
		*merge_score_p = (int)MAX_SCORE;
		return 1; /* even their types are different */
	}

	if (src->oid_valid && dst->oid_valid &&
	    oideq(&src->oid, &dst->oid))
		return 0; /* they are the same */

	if (r == the_repository && repo_has_promisor_remote(the_repository)) {
		options.missing_object_cb = diff_queued_diff_prefetch;
		options.missing_object_data = r;
	}

	if (diff_populate_filespec(r, src, &options) ||
	    diff_populate_filespec(r, dst, &options))
		return 0; /* error but caught downstream */

	max_size = ((src->size > dst->size) ? src->size : dst->size);
	if (max_size < MINIMUM_BREAK_SIZE)
		return 0; /* we do not break too small filepair */

	if (!src->size)
		return 0; /* we do not let empty files get renamed */

	if (diffcore_count_changes(r, src, dst,
				   &src->cnt_data, &dst->cnt_data,
				   &src_copied, &literal_added))
		return 0;

	/* sanity */
	if (src->size < src_copied)
		src_copied = src->size;
	if (dst->size < literal_added + src_copied) {
		if (src_copied < dst->size)
			literal_added = dst->size - src_copied;
		else
			literal_added = 0;
	}
	src_removed = src->size - src_copied;

	/* Compute merge-score, which is "how much is removed
	 * from the source material".  The clean-up stage will
	 * merge the surviving pair together if the score is
	 * less than the minimum, after rename/copy runs.
	 */
	*merge_score_p = (int)(src_removed * MAX_SCORE / src->size);
	if (*merge_score_p > break_score)
		return 1;

	/* Extent of damage, which counts both inserts and
	 * deletes.
	 */
	delta_size = src_removed + literal_added;
	if (delta_size * MAX_SCORE / max_size < break_score)
		return 0;

	/* If you removed a lot without adding new material, that is
	 * not really a rewrite.
	 */
	if ((src->size * break_score < src_removed * MAX_SCORE) &&
	    (literal_added * 20 < src_removed) &&
	    (literal_added * 20 < src_copied))
		return 0;

	return 1;
}

void diffcore_break(struct repository *r, int break_score)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;

	/* When the filepair has this much edit (insert and delete),
	 * it is first considered to be a rewrite and broken into a
	 * create and delete filepair.  This is to help breaking a
	 * file that had too much new stuff added, possibly from
	 * moving contents from another file, so that rename/copy can
	 * match it with the other file.
	 *
	 * int break_score; we reuse incoming parameter for this.
	 */

	/* After a pair is broken according to break_score and
	 * subjected to rename/copy, both of them may survive intact,
	 * due to lack of suitable rename/copy peer.  Or, the caller
	 * may be calling us without using rename/copy.  When that
	 * happens, we merge the broken pieces back into one
	 * modification together if the pair did not have more than
	 * this much delete.  For this computation, we do not take
	 * insert into account at all.  If you start from a 100-line
	 * file and delete 97 lines of it, it does not matter if you
	 * add 27 lines to it to make a new 30-line file or if you add
	 * 997 lines to it to make a 1000-line file.  Either way what
	 * you did was a rewrite of 97%.  On the other hand, if you
	 * delete 3 lines, keeping 97 lines intact, it does not matter
	 * if you add 3 lines to it to make a new 100-line file or if
	 * you add 903 lines to it to make a new 1000-line file.
	 * Either way you did a lot of additions and not a rewrite.
	 * This merge happens to catch the latter case.  A merge_score
	 * of 80% would be a good default value (a broken pair that
	 * has score lower than merge_score will be merged back
	 * together).
	 */
	int merge_score;
	int i;

	/* See comment on DEFAULT_BREAK_SCORE and
	 * DEFAULT_MERGE_SCORE in diffcore.h
	 */
	merge_score = (break_score >> 16) & 0xFFFF;
	break_score = (break_score & 0xFFFF);

	if (!break_score)
		break_score = DEFAULT_BREAK_SCORE;
	if (!merge_score)
		merge_score = DEFAULT_MERGE_SCORE;

	DIFF_QUEUE_CLEAR(&outq);

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		int score;

		/*
		 * We deal only with in-place edit of blobs.
		 * We do not break anything else.
		 */
		if (DIFF_FILE_VALID(p->one) && DIFF_FILE_VALID(p->two) &&
		    object_type(p->one->mode) == OBJ_BLOB &&
		    object_type(p->two->mode) == OBJ_BLOB &&
		    !strcmp(p->one->path, p->two->path)) {
			if (should_break(r, p->one, p->two,
					 break_score, &score)) {
				/* Split this into delete and create */
				struct diff_filespec *null_one, *null_two;
				struct diff_filepair *dp;

				/* Set score to 0 for the pair that
				 * needs to be merged back together
				 * should they survive rename/copy.
				 * Also we do not want to break very
				 * small files.
				 */
				if (score < merge_score)
					score = 0;

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

				diff_free_filespec_blob(p->one);
				diff_free_filespec_blob(p->two);
				free(p); /* not diff_free_filepair(), we are
					  * reusing one and two here.
					  */
				continue;
			}
		}
		diff_free_filespec_data(p->one);
		diff_free_filespec_data(p->two);
		diff_q(&outq, p);
	}
	free(q->queue);
	*q = outq;

	return;
}

static void merge_broken(struct diff_filepair *p,
			 struct diff_filepair *pp,
			 struct diff_queue_struct *outq)
{
	/* p and pp are broken pairs we want to merge */
	struct diff_filepair *c = p, *d = pp, *dp;
	if (DIFF_FILE_VALID(p->one)) {
		/* this must be a delete half */
		d = p; c = pp;
	}
	/* Sanity check */
	if (!DIFF_FILE_VALID(d->one))
		die("internal error in merge #1");
	if (DIFF_FILE_VALID(d->two))
		die("internal error in merge #2");
	if (DIFF_FILE_VALID(c->one))
		die("internal error in merge #3");
	if (!DIFF_FILE_VALID(c->two))
		die("internal error in merge #4");

	dp = diff_queue(outq, d->one, c->two);
	dp->score = p->score;
	/*
	 * We will be one extra user of the same src side of the
	 * broken pair, if it was used as the rename source for other
	 * paths elsewhere.  Increment to mark that the path stays
	 * in the resulting tree.
	 */
	d->one->rename_used++;
	diff_free_filespec_data(d->two);
	diff_free_filespec_data(c->one);
	free(d);
	free(c);
}

void diffcore_merge_broken(void)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	int i, j;

	DIFF_QUEUE_CLEAR(&outq);

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!p)
			/* we already merged this with its peer */
			continue;
		else if (p->broken_pair &&
			 !strcmp(p->one->path, p->two->path)) {
			/* If the peer also survived rename/copy, then
			 * we merge them back together.
			 */
			for (j = i + 1; j < q->nr; j++) {
				struct diff_filepair *pp = q->queue[j];
				if (pp->broken_pair &&
				    !strcmp(pp->one->path, pp->two->path) &&
				    !strcmp(p->one->path, pp->two->path)) {
					/* Peer survived.  Merge them */
					merge_broken(p, pp, &outq);
					q->queue[j] = NULL;
					goto next;
				}
			}
			/* The peer did not survive, so we keep
			 * it in the output.
			 */
			diff_q(&outq, p);
		}
		else
			diff_q(&outq, p);
next:;
	}
	free(q->queue);
	*q = outq;

	return;
}
