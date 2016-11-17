/*
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "hashmap.h"
#include "progress.h"

/* Table of rename/copy destinations */

static struct diff_rename_dst {
	struct diff_filespec *two;
	struct diff_filepair *pair;
} *rename_dst;
static int rename_dst_nr, rename_dst_alloc;

static int find_rename_dst(struct diff_filespec *two)
{
	int first, last;

	first = 0;
	last = rename_dst_nr;
	while (last > first) {
		int next = (last + first) >> 1;
		struct diff_rename_dst *dst = &(rename_dst[next]);
		int cmp = strcmp(two->path, dst->two->path);
		if (!cmp)
			return next;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	return -first - 1;
}

static struct diff_rename_dst *locate_rename_dst(struct diff_filespec *two)
{
	int ofs = find_rename_dst(two);
	return ofs < 0 ? NULL : &rename_dst[ofs];
}

/*
 * Returns 0 on success, -1 if we found a duplicate.
 */
static int add_rename_dst(struct diff_filespec *two)
{
	int first = find_rename_dst(two);

	if (first >= 0)
		return -1;
	first = -first - 1;

	/* insert to make it at "first" */
	ALLOC_GROW(rename_dst, rename_dst_nr + 1, rename_dst_alloc);
	rename_dst_nr++;
	if (first < rename_dst_nr)
		memmove(rename_dst + first + 1, rename_dst + first,
			(rename_dst_nr - first - 1) * sizeof(*rename_dst));
	rename_dst[first].two = alloc_filespec(two->path);
	fill_filespec(rename_dst[first].two, two->oid.hash, two->oid_valid,
		      two->mode);
	rename_dst[first].pair = NULL;
	return 0;
}

/* Table of rename/copy src files */
static struct diff_rename_src {
	struct diff_filepair *p;
	unsigned short score; /* to remember the break score */
} *rename_src;
static int rename_src_nr, rename_src_alloc;

static struct diff_rename_src *register_rename_src(struct diff_filepair *p)
{
	int first, last;
	struct diff_filespec *one = p->one;
	unsigned short score = p->score;

	first = 0;
	last = rename_src_nr;
	while (last > first) {
		int next = (last + first) >> 1;
		struct diff_rename_src *src = &(rename_src[next]);
		int cmp = strcmp(one->path, src->p->one->path);
		if (!cmp)
			return src;
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}

	/* insert to make it at "first" */
	ALLOC_GROW(rename_src, rename_src_nr + 1, rename_src_alloc);
	rename_src_nr++;
	if (first < rename_src_nr)
		memmove(rename_src + first + 1, rename_src + first,
			(rename_src_nr - first - 1) * sizeof(*rename_src));
	rename_src[first].p = p;
	rename_src[first].score = score;
	return &(rename_src[first]);
}

static int basename_same(struct diff_filespec *src, struct diff_filespec *dst)
{
	int src_len = strlen(src->path), dst_len = strlen(dst->path);
	while (src_len && dst_len) {
		char c1 = src->path[--src_len];
		char c2 = dst->path[--dst_len];
		if (c1 != c2)
			return 0;
		if (c1 == '/')
			return 1;
	}
	return (!src_len || src->path[src_len - 1] == '/') &&
		(!dst_len || dst->path[dst_len - 1] == '/');
}

struct diff_score {
	int src; /* index in rename_src */
	int dst; /* index in rename_dst */
	unsigned short score;
	short name_score;
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
	int score;

	/* We deal only with regular files.  Symlink renames are handled
	 * only when they are exact matches --- in other words, no edits
	 * after renaming.
	 */
	if (!S_ISREG(src->mode) || !S_ISREG(dst->mode))
		return 0;

	/*
	 * Need to check that source and destination sizes are
	 * filled in before comparing them.
	 *
	 * If we already have "cnt_data" filled in, we know it's
	 * all good (avoid checking the size for zero, as that
	 * is a possible size - we really should have a flag to
	 * say whether the size is valid or not!)
	 */
	if (!src->cnt_data &&
	    diff_populate_filespec(src, CHECK_SIZE_ONLY))
		return 0;
	if (!dst->cnt_data &&
	    diff_populate_filespec(dst, CHECK_SIZE_ONLY))
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
	if (max_size * (MAX_SCORE-minimum_score) < delta_size * MAX_SCORE)
		return 0;

	if (!src->cnt_data && diff_populate_filespec(src, 0))
		return 0;
	if (!dst->cnt_data && diff_populate_filespec(dst, 0))
		return 0;

	if (diffcore_count_changes(src, dst,
				   &src->cnt_data, &dst->cnt_data,
				   &src_copied, &literal_added))
		return 0;

	/* How similar are they?
	 * what percentage of material in dst are from source?
	 */
	if (!dst->size)
		score = 0; /* should not happen */
	else
		score = (int)(src_copied * MAX_SCORE / max_size);
	return score;
}

static void record_rename_pair(int dst_index, int src_index, int score)
{
	struct diff_filespec *src, *dst;
	struct diff_filepair *dp;

	if (rename_dst[dst_index].pair)
		die("internal error: dst already matched.");

	src = rename_src[src_index].p->one;
	src->rename_used++;
	src->count++;

	dst = rename_dst[dst_index].two;
	dst->count++;

	dp = diff_queue(NULL, src, dst);
	dp->renamed_pair = 1;
	if (!strcmp(src->path, dst->path))
		dp->score = rename_src[src_index].score;
	else
		dp->score = score;
	rename_dst[dst_index].pair = dp;
}

/*
 * We sort the rename similarity matrix with the score, in descending
 * order (the most similar first).
 */
static int score_compare(const void *a_, const void *b_)
{
	const struct diff_score *a = a_, *b = b_;

	/* sink the unused ones to the bottom */
	if (a->dst < 0)
		return (0 <= b->dst);
	else if (b->dst < 0)
		return -1;

	if (a->score == b->score)
		return b->name_score - a->name_score;

	return b->score - a->score;
}

struct file_similarity {
	struct hashmap_entry entry;
	int index;
	struct diff_filespec *filespec;
};

static unsigned int hash_filespec(struct diff_filespec *filespec)
{
	if (!filespec->oid_valid) {
		if (diff_populate_filespec(filespec, 0))
			return 0;
		hash_sha1_file(filespec->data, filespec->size, "blob",
			       filespec->oid.hash);
	}
	return sha1hash(filespec->oid.hash);
}

static int find_identical_files(struct hashmap *srcs,
				int dst_index,
				struct diff_options *options)
{
	int renames = 0;

	struct diff_filespec *target = rename_dst[dst_index].two;
	struct file_similarity *p, *best = NULL;
	int i = 100, best_score = -1;

	/*
	 * Find the best source match for specified destination.
	 */
	p = hashmap_get_from_hash(srcs, hash_filespec(target), NULL);
	for (; p; p = hashmap_get_next(srcs, p)) {
		int score;
		struct diff_filespec *source = p->filespec;

		/* False hash collision? */
		if (oidcmp(&source->oid, &target->oid))
			continue;
		/* Non-regular files? If so, the modes must match! */
		if (!S_ISREG(source->mode) || !S_ISREG(target->mode)) {
			if (source->mode != target->mode)
				continue;
		}
		/* Give higher scores to sources that haven't been used already */
		score = !source->rename_used;
		if (source->rename_used && options->detect_rename != DIFF_DETECT_COPY)
			continue;
		score += basename_same(source, target);
		if (score > best_score) {
			best = p;
			best_score = score;
			if (score == 2)
				break;
		}

		/* Too many identical alternatives? Pick one */
		if (!--i)
			break;
	}
	if (best) {
		record_rename_pair(dst_index, best->index, MAX_SCORE);
		renames++;
	}
	return renames;
}

static void insert_file_table(struct hashmap *table, int index, struct diff_filespec *filespec)
{
	struct file_similarity *entry = xmalloc(sizeof(*entry));

	entry->index = index;
	entry->filespec = filespec;

	hashmap_entry_init(entry, hash_filespec(filespec));
	hashmap_add(table, entry);
}

/*
 * Find exact renames first.
 *
 * The first round matches up the up-to-date entries,
 * and then during the second round we try to match
 * cache-dirty entries as well.
 */
static int find_exact_renames(struct diff_options *options)
{
	int i, renames = 0;
	struct hashmap file_table;

	/* Add all sources to the hash table in reverse order, because
	 * later on they will be retrieved in LIFO order.
	 */
	hashmap_init(&file_table, NULL, rename_src_nr);
	for (i = rename_src_nr-1; i >= 0; i--)
		insert_file_table(&file_table, i, rename_src[i].p->one);

	/* Walk the destinations and find best source match */
	for (i = 0; i < rename_dst_nr; i++)
		renames += find_identical_files(&file_table, i, options);

	/* Free the hash data structure and entries */
	hashmap_free(&file_table, 1);

	return renames;
}

#define NUM_CANDIDATE_PER_DST 4
static void record_if_better(struct diff_score m[], struct diff_score *o)
{
	int i, worst;

	/* find the worst one */
	worst = 0;
	for (i = 1; i < NUM_CANDIDATE_PER_DST; i++)
		if (score_compare(&m[i], &m[worst]) > 0)
			worst = i;

	/* is it better than the worst one? */
	if (score_compare(&m[worst], o) > 0)
		m[worst] = *o;
}

/*
 * Returns:
 * 0 if we are under the limit;
 * 1 if we need to disable inexact rename detection;
 * 2 if we would be under the limit if we were given -C instead of -C -C.
 */
static int too_many_rename_candidates(int num_create,
				      struct diff_options *options)
{
	int rename_limit = options->rename_limit;
	int num_src = rename_src_nr;
	int i;

	options->needed_rename_limit = 0;

	/*
	 * This basically does a test for the rename matrix not
	 * growing larger than a "rename_limit" square matrix, ie:
	 *
	 *    num_create * num_src > rename_limit * rename_limit
	 *
	 * but handles the potential overflow case specially (and we
	 * assume at least 32-bit integers)
	 */
	if (rename_limit <= 0 || rename_limit > 32767)
		rename_limit = 32767;
	if ((num_create <= rename_limit || num_src <= rename_limit) &&
	    (num_create * num_src <= rename_limit * rename_limit))
		return 0;

	options->needed_rename_limit =
		num_src > num_create ? num_src : num_create;

	/* Are we running under -C -C? */
	if (!DIFF_OPT_TST(options, FIND_COPIES_HARDER))
		return 1;

	/* Would we bust the limit if we were running under -C? */
	for (num_src = i = 0; i < rename_src_nr; i++) {
		if (diff_unmodified_pair(rename_src[i].p))
			continue;
		num_src++;
	}
	if ((num_create <= rename_limit || num_src <= rename_limit) &&
	    (num_create * num_src <= rename_limit * rename_limit))
		return 2;
	return 1;
}

static int find_renames(struct diff_score *mx, int dst_cnt, int minimum_score, int copies)
{
	int count = 0, i;

	for (i = 0; i < dst_cnt * NUM_CANDIDATE_PER_DST; i++) {
		struct diff_rename_dst *dst;

		if ((mx[i].dst < 0) ||
		    (mx[i].score < minimum_score))
			break; /* there is no more usable pair. */
		dst = &rename_dst[mx[i].dst];
		if (dst->pair)
			continue; /* already done, either exact or fuzzy. */
		if (!copies && rename_src[mx[i].src].p->one->rename_used)
			continue;
		record_rename_pair(mx[i].dst, mx[i].src, mx[i].score);
		count++;
	}
	return count;
}

void diffcore_rename(struct diff_options *options)
{
	int detect_rename = options->detect_rename;
	int minimum_score = options->rename_score;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	struct diff_score *mx;
	int i, j, rename_count, skip_unmodified = 0;
	int num_create, dst_cnt;
	struct progress *progress = NULL;

	if (!minimum_score)
		minimum_score = DEFAULT_RENAME_SCORE;

	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		if (!DIFF_FILE_VALID(p->one)) {
			if (!DIFF_FILE_VALID(p->two))
				continue; /* unmerged */
			else if (options->single_follow &&
				 strcmp(options->single_follow, p->two->path))
				continue; /* not interested */
			else if (!DIFF_OPT_TST(options, RENAME_EMPTY) &&
				 is_empty_blob_sha1(p->two->oid.hash))
				continue;
			else if (add_rename_dst(p->two) < 0) {
				warning("skipping rename detection, detected"
					" duplicate destination '%s'",
					p->two->path);
				goto cleanup;
			}
		}
		else if (!DIFF_OPT_TST(options, RENAME_EMPTY) &&
			 is_empty_blob_sha1(p->one->oid.hash))
			continue;
		else if (!DIFF_PAIR_UNMERGED(p) && !DIFF_FILE_VALID(p->two)) {
			/*
			 * If the source is a broken "delete", and
			 * they did not really want to get broken,
			 * that means the source actually stays.
			 * So we increment the "rename_used" score
			 * by one, to indicate ourselves as a user
			 */
			if (p->broken_pair && !p->score)
				p->one->rename_used++;
			register_rename_src(p);
		}
		else if (detect_rename == DIFF_DETECT_COPY) {
			/*
			 * Increment the "rename_used" score by
			 * one, to indicate ourselves as a user.
			 */
			p->one->rename_used++;
			register_rename_src(p);
		}
	}
	if (rename_dst_nr == 0 || rename_src_nr == 0)
		goto cleanup; /* nothing to do */

	/*
	 * We really want to cull the candidates list early
	 * with cheap tests in order to avoid doing deltas.
	 */
	rename_count = find_exact_renames(options);

	/* Did we only want exact renames? */
	if (minimum_score == MAX_SCORE)
		goto cleanup;

	/*
	 * Calculate how many renames are left (but all the source
	 * files still remain as options for rename/copies!)
	 */
	num_create = (rename_dst_nr - rename_count);

	/* All done? */
	if (!num_create)
		goto cleanup;

	switch (too_many_rename_candidates(num_create, options)) {
	case 1:
		goto cleanup;
	case 2:
		options->degraded_cc_to_c = 1;
		skip_unmodified = 1;
		break;
	default:
		break;
	}

	if (options->show_rename_progress) {
		progress = start_progress_delay(
				_("Performing inexact rename detection"),
				rename_dst_nr * rename_src_nr, 50, 1);
	}

	mx = xcalloc(st_mult(NUM_CANDIDATE_PER_DST, num_create), sizeof(*mx));
	for (dst_cnt = i = 0; i < rename_dst_nr; i++) {
		struct diff_filespec *two = rename_dst[i].two;
		struct diff_score *m;

		if (rename_dst[i].pair)
			continue; /* dealt with exact match already. */

		m = &mx[dst_cnt * NUM_CANDIDATE_PER_DST];
		for (j = 0; j < NUM_CANDIDATE_PER_DST; j++)
			m[j].dst = -1;

		for (j = 0; j < rename_src_nr; j++) {
			struct diff_filespec *one = rename_src[j].p->one;
			struct diff_score this_src;

			if (skip_unmodified &&
			    diff_unmodified_pair(rename_src[j].p))
				continue;

			this_src.score = estimate_similarity(one, two,
							     minimum_score);
			this_src.name_score = basename_same(one, two);
			this_src.dst = i;
			this_src.src = j;
			record_if_better(m, &this_src);
			/*
			 * Once we run estimate_similarity,
			 * We do not need the text anymore.
			 */
			diff_free_filespec_blob(one);
			diff_free_filespec_blob(two);
		}
		dst_cnt++;
		display_progress(progress, (i+1)*rename_src_nr);
	}
	stop_progress(&progress);

	/* cost matrix sorted by most to least similar pair */
	QSORT(mx, dst_cnt * NUM_CANDIDATE_PER_DST, score_compare);

	rename_count += find_renames(mx, dst_cnt, minimum_score, 0);
	if (detect_rename == DIFF_DETECT_COPY)
		rename_count += find_renames(mx, dst_cnt, minimum_score, 1);
	free(mx);

 cleanup:
	/* At this point, we have found some renames and copies and they
	 * are recorded in rename_dst.  The original list is still in *q.
	 */
	DIFF_QUEUE_CLEAR(&outq);
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		struct diff_filepair *pair_to_free = NULL;

		if (DIFF_PAIR_UNMERGED(p)) {
			diff_q(&outq, p);
		}
		else if (!DIFF_FILE_VALID(p->one) && DIFF_FILE_VALID(p->two)) {
			/*
			 * Creation
			 *
			 * We would output this create record if it has
			 * not been turned into a rename/copy already.
			 */
			struct diff_rename_dst *dst = locate_rename_dst(p->two);
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
				struct diff_rename_dst *dst = locate_rename_dst(p->one);
				if (dst && dst->pair)
					/* counterpart is now rename/copy */
					pair_to_free = p;
			}
			else {
				if (p->one->rename_used)
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

	for (i = 0; i < rename_dst_nr; i++)
		free_filespec(rename_dst[i].two);

	free(rename_dst);
	rename_dst = NULL;
	rename_dst_nr = rename_dst_alloc = 0;
	free(rename_src);
	rename_src = NULL;
	rename_src_nr = rename_src_alloc = 0;
	return;
}
