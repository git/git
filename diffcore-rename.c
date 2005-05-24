/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"

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

static struct diff_rename_src {
	struct diff_filespec *one;
	unsigned src_used : 1;
} *rename_src;
static int rename_src_nr, rename_src_alloc;

static struct diff_rename_src *locate_rename_src(struct diff_filespec *one,
						 int insert_ok)
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
	/* not found */
	if (!insert_ok)
		return NULL;
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
	rename_src[first].src_used = 0;
	return &(rename_src[first]);
}

static int is_exact_match(struct diff_filespec *src, struct diff_filespec *dst)
{
	if (src->sha1_valid && dst->sha1_valid &&
	    !memcmp(src->sha1, dst->sha1, 20))
		return 1;
	if (diff_populate_filespec(src) || diff_populate_filespec(dst))
		/* this is an error but will be caught downstream */
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
	int rank;
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
	 * the score as an integer between 0 and 10000, except
	 * where they match exactly it is considered better than anything
	 * else.
	 */
	void *delta;
	unsigned long delta_size, base_size;
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
	 * Note that base_size == 0 case is handled here already
	 * and the final score computation below would not have a
	 * divide-by-zero issue.
	 */
	if (base_size * (MAX_SCORE-minimum_score) < delta_size * MAX_SCORE)
		return 0;

	delta = diff_delta(src->data, src->size,
			   dst->data, dst->size,
			   &delta_size);
	/*
	 * We currently punt here, but we may later end up parsing the
	 * delta to really assess the extent of damage.  A big consecutive
	 * remove would produce small delta_size that affects quite a
	 * big portion of the file.
	 */
	free(delta);

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

	rename_src[src_index].src_used = 1;
	rename_dst[dst_index].pair = dp;
}

/*
 * We sort the rename similarity matrix with the score, in descending
 * order (more similar first).
 */
static int score_compare(const void *a_, const void *b_)
{
	const struct diff_score *a = a_, *b = b_;
	return b->score - a->score;
}

int diff_scoreopt_parse(const char *opt)
{
	int diglen, num, scale, i;
	if (opt[0] != '-' || (opt[1] != 'M' && opt[1] != 'C'))
		return -1; /* that is not a -M nor -C option */
	diglen = strspn(opt+2, "0123456789");
	if (diglen == 0 || strlen(opt+2) != diglen)
		return 0; /* use default */
	sscanf(opt+2, "%d", &num);
	for (i = 0, scale = 1; i < diglen; i++)
		scale *= 10;

	/* user says num divided by scale and we say internally that
	 * is MAX_SCORE * num / scale.
	 */
	return MAX_SCORE * num / scale;
}

void diffcore_rename(int detect_rename, int minimum_score)
{
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct renq, outq;
	struct diff_score *mx;
	int i, j;
	int num_create, num_src, dst_cnt;

	if (!minimum_score)
		minimum_score = DEFAULT_MINIMUM_SCORE;
	renq.queue = NULL;
	renq.nr = renq.alloc = 0;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!DIFF_FILE_VALID(p->one))
			if (!DIFF_FILE_VALID(p->two))
				continue; /* unmerged */
			else
				locate_rename_dst(p->two, 1);
		else if (!DIFF_FILE_VALID(p->two))
			locate_rename_src(p->one, 1);
		else if (1 < detect_rename) /* find copy, too */
			locate_rename_src(p->one, 1);
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
		goto flush_rest;

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
			break; /* there is not any more diffs applicable. */
		record_rename_pair(&renq, mx[i].dst, mx[i].src, mx[i].score);
	}
	free(mx);
	diff_debug_queue("done detecting fuzzy", &renq);

 flush_rest:
	/* At this point, we have found some renames and copies and they
	 * are kept in renq.  The original list is still in *q.
	 *
	 * Scan the original list and move them into the outq; we will sort
	 * outq and swap it into the queue supplied to pass that to
	 * downstream, so we assign the sort keys in this loop.
	 *
	 * See comments at the top of record_rename_pair for numbers used
	 * to assign rename_rank.
	 */
	outq.queue = NULL;
	outq.nr = outq.alloc = 0;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		struct diff_rename_src *src = locate_rename_src(p->one, 0);
		struct diff_rename_dst *dst = locate_rename_dst(p->two, 0);
		struct diff_filepair *pair_to_free = NULL;

		if (dst) {
			/* creation */
			if (dst->pair) {
				/* renq has rename/copy already to produce
				 * this file, so we do not emit the creation
				 * record in the output.
				 */
				diff_q(&outq, dst->pair);
				pair_to_free = p;
			}
			else
				/* no matching rename/copy source, so record
				 * this as a creation.
				 */
				diff_q(&outq, p);
		}
		else if (!diff_unmodified_pair(p))
			/* all the other cases need to be recorded as is */
			diff_q(&outq, p);
		else {
			/* unmodified pair needs to be recorded only if
			 * it is used as the source of rename/copy
			 */
			if (src && src->src_used)
				diff_q(&outq, p);
			else
				pair_to_free = p;
		}
		if (pair_to_free) {
			diff_free_filespec_data(pair_to_free->one);
			diff_free_filespec_data(pair_to_free->two);
			free(pair_to_free);
		}
	}
	diff_debug_queue("done copying original", &outq);

	free(renq.queue);
	free(q->queue);
	*q = outq;
	diff_debug_queue("done collapsing", q);

 cleanup:
	free(rename_dst);
	rename_dst = NULL;
	rename_dst_nr = rename_dst_alloc = 0;
	free(rename_src);
	rename_src = NULL;
	rename_src_nr = rename_src_alloc = 0;
	return;
}
