/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "delta.h"

struct diff_rename_pool {
	struct diff_filespec **s;
	int nr, alloc;
};

static void diff_rename_pool_clear(struct diff_rename_pool *pool)
{
	pool->s = NULL; pool->nr = pool->alloc = 0;
}

static void diff_rename_pool_add(struct diff_rename_pool *pool,
				 struct diff_filespec *s)
{
	if (S_ISDIR(s->mode))
		return;  /* rename/copy patch for tree does not make sense. */

	if (pool->alloc <= pool->nr) {
		pool->alloc = alloc_nr(pool->alloc);
		pool->s = xrealloc(pool->s,
				   sizeof(*(pool->s)) * pool->alloc);
	}
	pool->s[pool->nr] = s;
	pool->nr++;
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
	struct diff_filespec *src;
	struct diff_filespec *dst;
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
	unsigned long delta_size;
	int score;

	delta_size = ((src->size < dst->size) ?
		      (dst->size - src->size) : (src->size - dst->size));

	/* We would not consider rename followed by more than
	 * minimum_score/MAX_SCORE edits; that is, delta_size must be smaller
	 * than (src->size + dst->size)/2 * minimum_score/MAX_SCORE,
	 * which means...
	 */

	if ((src->size+dst->size)*minimum_score < delta_size*MAX_SCORE*2)
		return 0;

	delta = diff_delta(src->data, src->size,
			   dst->data, dst->size,
			   &delta_size);
	free(delta);

	/* This "delta" is really xdiff with adler32 and all the
	 * overheads but it is a quick and dirty approximation.
	 *
	 * Now we will give some score to it.  100% edit gets
	 * 0 points and 0% edit gets MAX_SCORE points.  That is, every
	 * 1/MAX_SCORE edit gets 1 point penalty.  The amount of penalty is:
	 *
	 * (delta_size * 2 / (src->size + dst->size)) * MAX_SCORE
	 *
	 */
	score = MAX_SCORE-(MAX_SCORE*2*delta_size/(src->size+dst->size));
	if (score < 0) return 0;
	if (MAX_SCORE < score) return MAX_SCORE;
	return score;
}

static void record_rename_pair(struct diff_queue_struct *outq,
			       struct diff_filespec *src,
			       struct diff_filespec *dst,
			       int rank,
			       int score)
{
	/* The rank is used to sort the final output, because there
	 * are certain dependencies.
	 *
	 *  - rank #0 depends on deleted ones.
	 *  - rank #1 depends on kept files before they are modified.
	 *  - rank #2 depends on kept files after they are modified;
	 *    currently not used.
	 *
	 * Therefore, the final output order should be:
	 *
	 *  1. rank #0 rename/copy diffs.
	 *  2. deletions in the original.
	 *  3. rank #1 rename/copy diffs.
	 *  4. additions and modifications in the original.
	 *  5. rank #2 rename/copy diffs; currently not used.
	 *
	 * To achieve this sort order, we give xform_work the number
	 * above.
	 */
	struct diff_filepair *dp = diff_queue(outq, src, dst);
	dp->xfrm_work = (rank * 2 + 1) | (score<<RENAME_SCORE_SHIFT);
	dst->xfrm_flags |= RENAME_DST_MATCHED;
}

#if 0
static void debug_filespec(struct diff_filespec *s, int x, const char *one)
{
	fprintf(stderr, "queue[%d] %s (%s) %s %06o %s\n",
		x, one,
		s->path,
		s->file_valid ? "valid" : "invalid",
		s->mode,
		s->sha1_valid ? sha1_to_hex(s->sha1) : "");
	fprintf(stderr, "queue[%d] %s size %lu flags %d\n",
		x, one,
		s->size, s->xfrm_flags);
}

static void debug_filepair(const struct diff_filepair *p, int i)
{
	debug_filespec(p->one, i, "one");
	debug_filespec(p->two, i, "two");
	fprintf(stderr, "pair flags %d, orig order %d, score %d\n",
		(p->xfrm_work & ((1<<RENAME_SCORE_SHIFT) - 1)),
		p->orig_order,
		(p->xfrm_work >> RENAME_SCORE_SHIFT));
}

static void debug_queue(const char *msg, struct diff_queue_struct *q)
{
	int i;
	if (msg)
		fprintf(stderr, "%s\n", msg);
	fprintf(stderr, "q->nr = %d\n", q->nr);
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		debug_filepair(p, i);
	}
}
#else
#define debug_queue(a,b) do { ; /*nothing*/ } while(0)
#endif

/*
 * We sort the outstanding diff entries according to the rank (see
 * comment at the beginning of record_rename_pair) and tiebreak with
 * the order in the original input.
 */
static int rank_compare(const void *a_, const void *b_)
{
	const struct diff_filepair *a = *(const struct diff_filepair **)a_;
	const struct diff_filepair *b = *(const struct diff_filepair **)b_;
	int a_rank = a->xfrm_work & ((1<<RENAME_SCORE_SHIFT) - 1);
	int b_rank = b->xfrm_work & ((1<<RENAME_SCORE_SHIFT) - 1);

	if (a_rank != b_rank)
		return a_rank - b_rank;
	return a->orig_order - b->orig_order;
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

static int needs_to_stay(struct diff_queue_struct *q, int i,
			 struct diff_filespec *it)
{
	/* If it will be used in later entry (either stay or used
	 * as the source of rename/copy), we need to copy, not rename.
	 */
	while (i < q->nr) {
		struct diff_filepair *p = q->queue[i++];
		if (!p->two->file_valid)
			continue; /* removed is fine */
		if (strcmp(p->one->path, it->path))
			continue; /* not relevant */

		/* p has its src set to *it and it is not a delete;
		 * it will be used for in-place change or rename/copy,
		 * so we cannot rename it out.
		 */
		return 1;
	}
	return 0;
}

void diff_detect_rename(struct diff_queue_struct *q,
			int detect_rename,
			int minimum_score)
{
	struct diff_queue_struct outq;
	struct diff_rename_pool created, deleted, stay;
	struct diff_rename_pool *(srcs[2]);
	struct diff_score *mx;
	int h, i, j;
	int num_create, num_src, dst_cnt, src_cnt;

	outq.queue = NULL;
	outq.nr = outq.alloc = 0;

	diff_rename_pool_clear(&created);
	diff_rename_pool_clear(&deleted);
	diff_rename_pool_clear(&stay);

	srcs[0] = &deleted;
	srcs[1] = &stay;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!p->one->file_valid)
			if (!p->two->file_valid)
				continue; /* ignore nonsense */
			else
				diff_rename_pool_add(&created, p->two);
		else if (!p->two->file_valid)
			diff_rename_pool_add(&deleted, p->one);
		else if (1 < detect_rename) /* find copy, too */
			diff_rename_pool_add(&stay, p->one);
	}
	if (created.nr == 0)
		goto cleanup; /* nothing to do */

	/* We really want to cull the candidates list early
	 * with cheap tests in order to avoid doing deltas.
	 *
	 * With the current callers, we should not have already
	 * matched entries at this point, but it is nonetheless
	 * checked for sanity.
	 */
	for (i = 0; i < created.nr; i++) {
		if (created.s[i]->xfrm_flags & RENAME_DST_MATCHED)
			continue; /* we have matched exactly already */
		for (h = 0; h < sizeof(srcs)/sizeof(srcs[0]); h++) {
			struct diff_rename_pool *p = srcs[h];
			for (j = 0; j < p->nr; j++) {
				if (!is_exact_match(p->s[j], created.s[i]))
					continue;
				record_rename_pair(&outq,
						   p->s[j], created.s[i], h,
						   MAX_SCORE);
				break; /* we are done with this entry */
			}
		}
	}
	debug_queue("done detecting exact", &outq);

	/* Have we run out the created file pool?  If so we can avoid
	 * doing the delta matrix altogether.
	 */
	if (outq.nr == created.nr)
		goto flush_rest;

	num_create = (created.nr - outq.nr);
	num_src = deleted.nr + stay.nr;
	mx = xmalloc(sizeof(*mx) * num_create * num_src);
	for (dst_cnt = i = 0; i < created.nr; i++) {
		int base = dst_cnt * num_src;
		if (created.s[i]->xfrm_flags & RENAME_DST_MATCHED)
			continue; /* dealt with exact match already. */
		for (src_cnt = h = 0; h < sizeof(srcs)/sizeof(srcs[0]); h++) {
			struct diff_rename_pool *p = srcs[h];
			for (j = 0; j < p->nr; j++, src_cnt++) {
				struct diff_score *m = &mx[base + src_cnt];
				m->src = p->s[j];
				m->dst = created.s[i];
				m->score = estimate_similarity(m->src, m->dst,
							       minimum_score);
				m->rank = h;
			}
		}
		dst_cnt++;
	}
	/* cost matrix sorted by most to least similar pair */
	qsort(mx, num_create * num_src, sizeof(*mx), score_compare);
	for (i = 0; i < num_create * num_src; i++) {
		if (mx[i].dst->xfrm_flags & RENAME_DST_MATCHED)
			continue; /* alreayd done, either exact or fuzzy. */
		if (mx[i].score < minimum_score)
			continue;
		record_rename_pair(&outq,
				  mx[i].src, mx[i].dst, mx[i].rank,
				  mx[i].score);
	}
	free(mx);
	debug_queue("done detecting fuzzy", &outq);

 flush_rest:
	/* At this point, we have found some renames and copies and they
	 * are kept in outq.  The original list is still in *q.
	 *
	 * Scan the original list and move them into the outq; we will sort
	 * outq and swap it into the queue supplied to pass that to
	 * downstream, so we assign the sort keys in this loop.
	 *
	 * See comments at the top of record_rename_pair for numbers used
	 * to assign xfrm_work.
	 *
	 * Note that we have not annotated the diff_filepair with any comment
	 * so there is nothing other than p to free.
	 */
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *dp, *p = q->queue[i];
		if (!p->one->file_valid) {
			if (p->two->file_valid) {
				/* creation */
				dp = diff_queue(&outq, p->one, p->two);
				dp->xfrm_work = 4;
			}
			/* otherwise it is a nonsense; just ignore it */
		}
		else if (!p->two->file_valid) {
			/* deletion */
			dp = diff_queue(&outq, p->one, p->two);
			dp->xfrm_work = 2;
		}
		else {
			/* modification, or stay as is */
			dp = diff_queue(&outq, p->one, p->two);
			dp->xfrm_work = 4;
		}
		free(p);
	}
	debug_queue("done copying original", &outq);

	/* Sort outq */
	qsort(outq.queue, outq.nr, sizeof(outq.queue[0]), rank_compare);

	debug_queue("done sorting", &outq);

	free(q->queue);
	q->nr = q->alloc = 0;
	q->queue = NULL;

	/* Copy it out to q, removing duplicates. */
	for (i = 0; i < outq.nr; i++) {
		struct diff_filepair *p = outq.queue[i];
		if (!p->one->file_valid) {
			/* created */
			if (p->two->xfrm_flags & RENAME_DST_MATCHED)
				; /* rename/copy created it already */
			else
				diff_queue(q, p->one, p->two);
		}
		else if (!p->two->file_valid) {
			/* deleted */
			if (p->one->xfrm_flags & RENAME_SRC_GONE)
				; /* rename/copy deleted it already */
			else
				diff_queue(q, p->one, p->two);
		}
		else if (strcmp(p->one->path, p->two->path)) {
			/* rename or copy */
			struct diff_filepair *dp =
				diff_queue(q, p->one, p->two);
			int msglen = (strlen(p->one->path) +
				      strlen(p->two->path) + 100);
			int score = (p->xfrm_work >> RENAME_SCORE_SHIFT);
			dp->xfrm_msg = xmalloc(msglen);

			/* if we have a later entry that is a rename/copy
			 * that depends on p->one, then we copy here.
			 * otherwise we rename it.
			 */
			if (needs_to_stay(&outq, i+1, p->one)) {
				/* copy it */
				sprintf(dp->xfrm_msg,
					"similarity index %d%%\n"
					"copy from %s\n"
					"copy to %s\n",
					(int)(0.5 + score * 100 / MAX_SCORE),
					p->one->path, p->two->path);
			}
			else {
				/* rename it, and mark it as gone. */
				p->one->xfrm_flags |= RENAME_SRC_GONE;
				sprintf(dp->xfrm_msg,
					"similarity index %d%%\n"
					"rename old %s\n"
					"rename new %s\n",
					(int)(0.5 + score * 100 / MAX_SCORE),
					p->one->path, p->two->path);
			}
		}
		else
			/* otherwise it is a modified (or stayed) entry */
			diff_queue(q, p->one, p->two);
		free(p);
	}

	free(outq.queue);
	debug_queue("done collapsing", q);

 cleanup:
	free(created.s);
	free(deleted.s);
	free(stay.s);
	return;
}
