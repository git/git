/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"

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
	rename_dst[first].two = alloc_filespec(two->path);
	fill_filespec(rename_dst[first].two, two->sha1, two->mode);
	rename_dst[first].pair = NULL;
	return &(rename_dst[first]);
}

/* Table of rename/copy src files */
static struct diff_rename_src {
	struct diff_filespec *one;
	unsigned short score; /* to remember the break score */
	unsigned src_path_left : 1;
} *rename_src;
static int rename_src_nr, rename_src_alloc;

static struct diff_rename_src *register_rename_src(struct diff_filespec *one,
						   int src_path_left,
						   unsigned short score)
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
	rename_src[first].score = score;
	rename_src[first].src_path_left = src_path_left;
	return &(rename_src[first]);
}

static int is_exact_match(struct diff_filespec *src,
			  struct diff_filespec *dst,
			  int contents_too)
{
	if (src->sha1_valid && dst->sha1_valid &&
	    !memcmp(src->sha1, dst->sha1, 20))
		return 1;
	if (!contents_too)
		return 0;
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
	unsigned long max_size, delta_size, base_size, src_copied, literal_added;
	unsigned long delta_limit;
	int score;

	/* We deal only with regular files.  Symlink renames are handled
	 * only when they are exact matches --- in other words, no edits
	 * after renaming.
	 */
	if (!S_ISREG(src->mode) || !S_ISREG(dst->mode))
		return 0;

	max_size = ((src->size > dst->size) ? src->size : dst->size);
	base_size = ((src->size < dst->size) ? src->size : dst->size);
	delta_size = max_size - base_size;

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
	if (diffcore_count_changes(src->data, src->size,
				   dst->data, dst->size,
				   &src->cnt_data, &dst->cnt_data,
				   delta_limit,
				   &src_copied, &literal_added))
		return 0;

	/* How similar are they?
	 * what percentage of material in dst are from source?
	 */
	if (!dst->size)
		score = 0; /* should not happen */
	else
		score = src_copied * MAX_SCORE / max_size;
	return score;
}

static void record_rename_pair(int dst_index, int src_index, int score)
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

	dp = diff_queue(NULL, one, two);
	dp->renamed_pair = 1;
	if (!strcmp(src->path, dst->path))
		dp->score = rename_src[src_index].score;
	else
		dp->score = score;
	dp->source_stays = rename_src[src_index].src_path_left;
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

static int compute_stays(struct diff_queue_struct *q,
			 struct diff_filespec *one)
{
	int i;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (strcmp(one->path, p->two->path))
			continue;
		if (DIFF_PAIR_RENAME(p)) {
			return 0; /* something else is renamed into this */
		}
	}
	return 1;
}

void diffcore_rename(struct diff_options *options)
{
	int detect_rename = options->detect_rename;
	int minimum_score = options->rename_score;
	int rename_limit = options->rename_limit;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	struct diff_score *mx;
	int i, j, rename_count, contents_too;
	int num_create, num_src, dst_cnt;

	if (!minimum_score)
		minimum_score = DEFAULT_RENAME_SCORE;
	rename_count = 0;

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
			register_rename_src(p->one, stays, p->score);
		}
		else if (detect_rename == DIFF_DETECT_COPY)
			register_rename_src(p->one, 1, p->score);
	}
	if (rename_dst_nr == 0 || rename_src_nr == 0 ||
	    (0 < rename_limit && rename_limit < rename_dst_nr))
		goto cleanup; /* nothing to do */

	/* We really want to cull the candidates list early
	 * with cheap tests in order to avoid doing deltas.
	 * The first round matches up the up-to-date entries,
	 * and then during the second round we try to match
	 * cache-dirty entries as well.
	 */
	for (contents_too = 0; contents_too < 2; contents_too++) {
		for (i = 0; i < rename_dst_nr; i++) {
			struct diff_filespec *two = rename_dst[i].two;
			if (rename_dst[i].pair)
				continue; /* dealt with an earlier round */
			for (j = 0; j < rename_src_nr; j++) {
				struct diff_filespec *one = rename_src[j].one;
				if (!is_exact_match(one, two, contents_too))
					continue;
				record_rename_pair(i, j, MAX_SCORE);
				rename_count++;
				break; /* we are done with this entry */
			}
		}
	}

	/* Have we run out the created file pool?  If so we can avoid
	 * doing the delta matrix altogether.
	 */
	if (rename_count == rename_dst_nr)
		goto cleanup;

	if (minimum_score == MAX_SCORE)
		goto cleanup;

	num_create = (rename_dst_nr - rename_count);
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
		/* We do not need the text anymore */
		diff_free_filespec_data(two);
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
		record_rename_pair(mx[i].dst, mx[i].src, mx[i].score);
		rename_count++;
	}
	free(mx);

 cleanup:
	/* At this point, we have found some renames and copies and they
	 * are recorded in rename_dst.  The original list is still in *q.
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
			 * (2) this is not a broken delete, and rename_dst
			 *     does not have a rename/copy to move p->one->path
			 *     out of existence.
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
				for (j = 0; j < rename_dst_nr; j++) {
					if (!rename_dst[j].pair)
						continue;
					if (strcmp(rename_dst[j].pair->
						   one->path,
						   p->one->path))
						continue;
					break;
				}
				if (j < rename_dst_nr)
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

	free(q->queue);
	*q = outq;
	diff_debug_queue("done collapsing", q);

	/* We need to see which rename source really stays here;
	 * earlier we only checked if the path is left in the result,
	 * but even if a path remains in the result, if that is coming
	 * from copying something else on top of it, then the original
	 * source is lost and does not stay.
	 */
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (DIFF_PAIR_RENAME(p) && p->source_stays) {
			/* If one appears as the target of a rename-copy,
			 * then mark p->source_stays = 0; otherwise
			 * leave it as is.
			 */
			p->source_stays = compute_stays(q, p->one);
		}
	}

	for (i = 0; i < rename_dst_nr; i++) {
		diff_free_filespec_data(rename_dst[i].two);
		free(rename_dst[i].two);
	}

	free(rename_dst);
	rename_dst = NULL;
	rename_dst_nr = rename_dst_alloc = 0;
	free(rename_src);
	rename_src = NULL;
	rename_src_nr = rename_src_alloc = 0;
	return;
}
