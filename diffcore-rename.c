/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"
#include "count-delta.h"

/* Table of rename/copy destinations */

static struct diff_rename_dst {
	struct diff_filespec *two;
	struct diff_filepair *pair;
} *rename_dst;
static int rename_dst_nr, rename_dst_alloc;

static struct diff_rename_dst *locate_rename_dst(struct diff_filespec *two,
						 int insert_ok)
{
	int first, last;

	first = 0;
	last = rename_dst_nr;
	while (last > first) {
		int next = (last + first) >> 1;
		struct diff_rename_dst *dst = &(rename_dst[next]);
		int cmp = strcmp(two->path, dst->two->path);
		if (!cmp)
			return dst;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	/* not found */
	if (!insert_ok)
		return NULL;
	/* insert to make it at "first" */
	if (rename_dst_alloc <= rename_dst_nr) {
		rename_dst_alloc = alloc_nr(rename_dst_alloc);
		rename_dst = xrealloc(rename_dst,
				      rename_dst_alloc * sizeof(*rename_dst));
	}
	rename_dst_nr++;
	if (first < rename_dst_nr)
		memmove(rename_dst + first + 1, rename_dst + first,
			(rename_dst_nr - first - 1) * sizeof(*rename_dst));
	rename_dst[first].two = two;
	rename_dst[first].pair = NULL;
	return &(rename_dst[first]);
}

/* Table of rename/copy src files */
static struct diff_rename_src {
	struct diff_filespec *one;
	unsigned src_stays : 1;
} *rename_src;
static int rename_src_nr, rename_src_alloc;

static struct diff_rename_src *register_rename_src(struct diff_filespec *one,
						   int src_stays)
{
	int first, last;

	first = 0;
	last = rename_src_nr;
	while (last > first) {
		int next = (last + first) >> 1;
		struct diff_rename_src *src = &(rename_src[next]);
		int cmp = strcmp(one->path, src->one->path);
		if (!cmp)
			return src;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}

	/* insert to make it at "first" */
	if (rename_src_alloc <= rename_src_nr) {
		rename_src_alloc = alloc_nr(rename_src_alloc);
		rename_src = xrealloc(rename_src,
				      rename_src_alloc * sizeof(*rename_src));
	}
	rename_src_nr++;
	if (first < rename_src_nr)
		memmove(rename_src + first + 1, rename_src + first,
			(rename_src_nr - first - 1) * sizeof(*rename_src));
	rename_src[first].one = one;
	rename_src[first].src_stays = src_stays;
	return &(rename_src[first]);
}

static int is_exact_match(struct diff_filespec *src, struct diff_filespec *dst)
{
	if (src->sha1_valid && dst->sha1_valid &&
	    !memcmp(src->sha1, dst->sha1, 20))
		return 1;
	if (diff_populate_filespec(src, 1) || diff_populate_filespec(dst, 1))
		return 0;
	if (src->size != dst->size)
		return 0;
	if (diff_populate_filespec(src, 0) || diff_populate_filespec(dst, 0))
		return 0;
	if (src->size == dst->size &&
	    !memcmp(src->data, dst->data, src->size))
		return 1;
	return 0;
}

struct diff_score {
	int src; /* index in rename_src */
	int dst; /* index in rename_dst */
	int score;
};

static int estimate_similarity(struct diff_filespec *src,
			       struct diff_filespec *dst,
			       int minimum_score)
{
	/* src points at a file that existed in the original tree (or
	 * optionally a file in the destination tree) and dst points
	 * at a newly created file.  They may be quite similar, in which
	 * case we want to say src is renamed to dst or src is copied into
	 * dst, and then some edit has been applied to dst.
	 *
	 * Compare them and return how similar they are, representing
	 * the score as an integer between 0 and MAX_SCORE.
	 *
	 * When there is an exact match, it is considered a better
	 * match than anything else; the destination does not even
	 * call into this function in that case.
	 */
	void *delta;
	unsigned long delta_size, base_size, src_copied, literal_added;
	unsigned long delta_limit;
	int score;

	/* We deal only with regular files.  Symlink renames are handled
	 * only when they are exact matches --- in other words, no edits
	 * after renaming.
	 */
	if (!S_ISREG(src->mode) || !S_ISREG(dst->mode))
		return 0;

	delta_size = ((src->size < dst->size) ?
		      (dst->size - src->size) : (src->size - dst->size));
	base_size = ((src->size < dst->size) ? src->size : dst->size);

	/* We would not consider edits that change the file size so
	 * drastically.  delta_size must be smaller than
	 * (MAX_SCORE-minimum_score)/MAX_SCORE * min(src->size, dst->size).
	 *
	 * Note that base_size == 0 case is handled here already
	 * and the final score computation below would not have a
	 * divide-by-zero issue.
	 */
	if (base_size * (MAX_SCORE-minimum_score) < delta_size * MAX_SCORE)
		return 0;

	if (diff_populate_filespec(src, 0) || diff_populate_filespec(dst, 0))
		return 0; /* error but caught downstream */

	delta_limit = base_size * (MAX_SCORE-minimum_score) / MAX_SCORE;
	delta = diff_delta(src->data, src->size,
			   dst->data, dst->size,
			   &delta_size, delta_limit);
	if (!delta)
		/* If delta_limit is exceeded, we have too much differences */
		return 0;

	/* A delta that has a lot of literal additions would have
	 * big delta_size no matter what else it does.
	 */
	if (base_size * (MAX_SCORE-minimum_score) < delta_size * MAX_SCORE)
		return 0;

	/* Estimate the edit size by interpreting delta. */
	if (count_delta(delta, delta_size, &src_copied, &literal_added)) {
		free(delta);
		return 0;
	}
	free(delta);

	/* Extent of damage */
	if (src->size + literal_added < src_copied)
		delta_size = 0;
	else
		delta_size = (src->size - src_copied) + literal_added;

	/*
	 * Now we will give some score to it.  100% edit gets 0 points
	 * and 0% edit gets MAX_SCORE points.
	 */
	score = MAX_SCORE - (MAX_SCORE * delta_size / base_size); 
	if (score < 0) return 0;
	if (MAX_SCORE < score) return MAX_SCORE;
	return score;
}

static void record_rename_pair(struct diff_queue_struct *renq,
			       int dst_index, int src_index, int score)
{
	struct diff_filespec *one, *two, *src, *dst;
	struct diff_filepair *dp;

	if (rename_dst[dst_index].pair)
		die("internal error: dst already matched.");

	src = rename_src[src_index].one;
	one = alloc_filespec(src->path);
	fill_filespec(one, src->sha1, src->mode);

	dst = rename_dst[dst_index].two;
	two = alloc_filespec(dst->path);
	fill_filespec(two, dst->sha1, dst->mode);

	dp = diff_queue(renq, one, two);
	dp->score = score;
	dp->source_stays = rename_src[src_index].src_stays;
	rename_dst[dst_index].pair = dp;
}

/*
 * We sort the rename similarity matrix with the score, in descending
 * order (the most similar first).
 */
static int score_compare(const void *a_, const void *b_)
{
	const struct diff_score *a = a_, *b = b_;
	return b->score - a->score;
}

void diffcore_rename(int detect_rename, int minimum_score)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct renq, outq;
	struct diff_score *mx;
	int i, j;
	int num_create, num_src, dst_cnt;

	if (!minimum_score)
		minimum_score = DEFAULT_RENAME_SCORE;
	renq.queue = NULL;
	renq.nr = renq.alloc = 0;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!DIFF_FILE_VALID(p->one))
			if (!DIFF_FILE_VALID(p->two))
				continue; /* unmerged */
			else
				locate_rename_dst(p->two, 1);
		else if (!DIFF_FILE_VALID(p->two)) {
			/* If the source is a broken "delete", and
			 * they did not really want to get broken,
			 * that means the source actually stays.
			 */
			int stays = (p->broken_pair && !p->score);
			register_rename_src(p->one, stays);
		}
		else if (detect_rename == DIFF_DETECT_COPY)
			register_rename_src(p->one, 1);
	}
	if (rename_dst_nr == 0)
		goto cleanup; /* nothing to do */

	/* We really want to cull the candidates list early
	 * with cheap tests in order to avoid doing deltas.
	 */
	for (i = 0; i < rename_dst_nr; i++) {
		struct diff_filespec *two = rename_dst[i].two;
		for (j = 0; j < rename_src_nr; j++) {
			struct diff_filespec *one = rename_src[j].one;
			if (!is_exact_match(one, two))
				continue;
			record_rename_pair(&renq, i, j, MAX_SCORE);
			break; /* we are done with this entry */
		}
	}
	diff_debug_queue("done detecting exact", &renq);

	/* Have we run out the created file pool?  If so we can avoid
	 * doing the delta matrix altogether.
	 */
	if (renq.nr == rename_dst_nr)
		goto cleanup;

	num_create = (rename_dst_nr - renq.nr);
	num_src = rename_src_nr;
	mx = xmalloc(sizeof(*mx) * num_create * num_src);
	for (dst_cnt = i = 0; i < rename_dst_nr; i++) {
		int base = dst_cnt * num_src;
		struct diff_filespec *two = rename_dst[i].two;
		if (rename_dst[i].pair)
			continue; /* dealt with exact match already. */
		for (j = 0; j < rename_src_nr; j++) {
			struct diff_filespec *one = rename_src[j].one;
			struct diff_score *m = &mx[base+j];
			m->src = j;
			m->dst = i;
			m->score = estimate_similarity(one, two,
						       minimum_score);
		}
		dst_cnt++;
	}
	/* cost matrix sorted by most to least similar pair */
	qsort(mx, num_create * num_src, sizeof(*mx), score_compare);
	for (i = 0; i < num_create * num_src; i++) {
		struct diff_rename_dst *dst = &rename_dst[mx[i].dst];
		if (dst->pair)
			continue; /* already done, either exact or fuzzy. */
		if (mx[i].score < minimum_score)
			break; /* there is no more usable pair. */
		record_rename_pair(&renq, mx[i].dst, mx[i].src, mx[i].score);
	}
	free(mx);
	diff_debug_queue("done detecting fuzzy", &renq);

 cleanup:
	/* At this point, we have found some renames and copies and they
	 * are kept in renq.  The original list is still in *q.
	 */
	outq.queue = NULL;
	outq.nr = outq.alloc = 0;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		struct diff_filepair *pair_to_free = NULL;

		if (!DIFF_FILE_VALID(p->one) && DIFF_FILE_VALID(p->two)) {
			/*
			 * Creation
			 *
			 * We would output this create record if it has
			 * not been turned into a rename/copy already.
			 */
			struct diff_rename_dst *dst =
				locate_rename_dst(p->two, 0);
			if (dst && dst->pair) {
				diff_q(&outq, dst->pair);
				pair_to_free = p;
			}
			else
				/* no matching rename/copy source, so
				 * record this as a creation.
				 */
				diff_q(&outq, p);
		}
		else if (DIFF_FILE_VALID(p->one) && !DIFF_FILE_VALID(p->two)) {
			/*
			 * Deletion
			 *
			 * We would output this delete record if:
			 *
			 * (1) this is a broken delete and the counterpart
			 *     broken create remains in the output; or
			 * (2) this is not a broken delete, and renq does
			 *     not have a rename/copy to move p->one->path
			 *     out.
			 *
			 * Otherwise, the counterpart broken create
			 * has been turned into a rename-edit; or
			 * delete did not have a matching create to
			 * begin with.
			 */
			if (DIFF_PAIR_BROKEN(p)) {
				/* broken delete */
				struct diff_rename_dst *dst =
					locate_rename_dst(p->one, 0);
				if (dst && dst->pair)
					/* counterpart is now rename/copy */
					pair_to_free = p;
			}
			else {
				for (j = 0; j < renq.nr; j++)
					if (!strcmp(renq.queue[j]->one->path,
						    p->one->path))
						break;
				if (j < renq.nr)
					/* this path remains */
					pair_to_free = p;
			}

			if (pair_to_free)
				;
			else
				diff_q(&outq, p);
		}
		else if (!diff_unmodified_pair(p))
			/* all the usual ones need to be kept */
			diff_q(&outq, p);
		else
			/* no need to keep unmodified pairs */
			pair_to_free = p;

		if (pair_to_free)
			diff_free_filepair(pair_to_free);
	}
	diff_debug_queue("done copying original", &outq);

	free(renq.queue);
	free(q->queue);
	*q = outq;
	diff_debug_queue("done collapsing", q);

	free(rename_dst);
	rename_dst = NULL;
	rename_dst_nr = rename_dst_alloc = 0;
	free(rename_src);
	rename_src = NULL;
	rename_src_nr = rename_src_alloc = 0;
	return;
}
