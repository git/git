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
		return;  /* no trees, please */

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

static void record_rename_pair(struct diff_queue_struct *outq,
			       struct diff_filespec *src,
			       struct diff_filespec *dst,
			       int rank,
			       int score)
{
	/*
	 * These ranks are used to sort the final output, because there
	 * are certain dependencies:
	 *
	 *  1. rename/copy that depends on deleted ones.
	 *  2. deletions in the original.
	 *  3. rename/copy that depends on the pre-edit image of kept files.
	 *  4. additions, modifications and no-modifications in the original.
	 *  5. rename/copy that depends on the post-edit image of kept files
	 *     (note that we currently do not detect such rename/copy).
	 *
	 * The downstream diffcore transformers are free to reorder
	 * the entries as long as they keep file pairs that has the
	 * same p->one->path in earlier rename_rank to appear before
	 * later ones.
	 *
	 * To the final output routine, and in the diff-raw format
	 * output, a rename/copy that is based on a path that has a
	 * later entry that shares the same p->one->path and is not a
	 * deletion is a copy.  Otherwise it is a rename.
	 */

	struct diff_filepair *dp = diff_queue(outq, src, dst);
	dp->rename_rank = rank * 2 + 1;
	dp->score = score;
	dst->xfrm_flags |= RENAME_DST_MATCHED;
}

#if 0
static void debug_filespec(struct diff_filespec *s, int x, const char *one)
{
	fprintf(stderr, "queue[%d] %s (%s) %s %06o %s\n",
		x, one,
		s->path,
		DIFF_FILE_VALID(s) ? "valid" : "invalid",
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
	fprintf(stderr, "pair rank %d, orig order %d, score %d\n",
		p->rename_rank, p->orig_order, p->score);
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
	int a_rank = a->rename_rank;
	int b_rank = b->rename_rank;

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
	struct diff_queue_struct outq;
	struct diff_rename_pool created, deleted, stay;
	struct diff_rename_pool *(srcs[2]);
	struct diff_score *mx;
	int h, i, j;
	int num_create, num_src, dst_cnt, src_cnt;

	if (!minimum_score)
		minimum_score = DEFAULT_MINIMUM_SCORE;
	outq.queue = NULL;
	outq.nr = outq.alloc = 0;

	diff_rename_pool_clear(&created);
	diff_rename_pool_clear(&deleted);
	diff_rename_pool_clear(&stay);

	srcs[0] = &deleted;
	srcs[1] = &stay;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!DIFF_FILE_VALID(p->one))
			if (!DIFF_FILE_VALID(p->two))
				continue; /* unmerged */
			else
				diff_rename_pool_add(&created, p->two);
		else if (!DIFF_FILE_VALID(p->two))
			diff_rename_pool_add(&deleted, p->one);
		else if (1 < detect_rename) /* find copy, too */
			diff_rename_pool_add(&stay, p->one);
	}
	if (created.nr == 0)
		goto cleanup; /* nothing to do */

	/* We really want to cull the candidates list early
	 * with cheap tests in order to avoid doing deltas.
	 */
	for (i = 0; i < created.nr; i++) {
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
			break; /* there is not any more diffs applicable. */
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
	 * to assign rename_rank.
	 */
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *dp, *p = q->queue[i];
		if (!DIFF_FILE_VALID(p->one)) {
			/* creation or unmerged entries */
			dp = diff_queue(&outq, p->one, p->two);
			dp->rename_rank = 4;
		}
		else if (!DIFF_FILE_VALID(p->two)) {
			/* deletion */
			dp = diff_queue(&outq, p->one, p->two);
			dp->rename_rank = 2;
		}
		else {
			/* modification, or stay as is */
			dp = diff_queue(&outq, p->one, p->two);
			dp->rename_rank = 4;
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
		if (!DIFF_FILE_VALID(p->one)) {
			/* created or unmerged */
			if (p->two->xfrm_flags & RENAME_DST_MATCHED)
				; /* rename/copy created it already */
			else
				diff_queue(q, p->one, p->two);
		}
		else if (!DIFF_FILE_VALID(p->two)) {
			/* deleted */
			diff_queue(q, p->one, p->two);
		}
		else if (strcmp(p->one->path, p->two->path)) {
			/* rename or copy */
			struct diff_filepair *dp =
				diff_queue(q, p->one, p->two);
			dp->score = p->score;
		}
		else
			/* otherwise it is a modified (or "stay") entry */
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
