/*
 *
 * Copyright (C) 2005 Junio C Hamano
 */
#include "cache.h"
#include "diff.h"
#include "diffcore.h"
#include "object-store.h"
#include "hashmap.h"
#include "progress.h"
#include "promisor-remote.h"
#include "strmap.h"

/* Table of rename/copy destinations */

static struct diff_rename_dst {
	struct diff_filepair *p;
	struct diff_filespec *filespec_to_free;
	int is_rename; /* false -> just a create; true -> rename or copy */
} *rename_dst;
static int rename_dst_nr, rename_dst_alloc;
/* Mapping from break source pathname to break destination index */
static struct strintmap *break_idx = NULL;

static struct diff_rename_dst *locate_rename_dst(struct diff_filepair *p)
{
	/* Lookup by p->ONE->path */
	int idx = break_idx ? strintmap_get(break_idx, p->one->path) : -1;
	return (idx == -1) ? NULL : &rename_dst[idx];
}

/*
 * Returns 0 on success, -1 if we found a duplicate.
 */
static int add_rename_dst(struct diff_filepair *p)
{
	ALLOC_GROW(rename_dst, rename_dst_nr + 1, rename_dst_alloc);
	rename_dst[rename_dst_nr].p = p;
	rename_dst[rename_dst_nr].filespec_to_free = NULL;
	rename_dst[rename_dst_nr].is_rename = 0;
	rename_dst_nr++;
	return 0;
}

/* Table of rename/copy src files */
static struct diff_rename_src {
	struct diff_filepair *p;
	unsigned short score; /* to remember the break score */
} *rename_src;
static int rename_src_nr, rename_src_alloc;

static void register_rename_src(struct diff_filepair *p)
{
	if (p->broken_pair) {
		if (!break_idx) {
			break_idx = xmalloc(sizeof(*break_idx));
			strintmap_init(break_idx, -1);
		}
		strintmap_set(break_idx, p->one->path, rename_dst_nr);
	}

	ALLOC_GROW(rename_src, rename_src_nr + 1, rename_src_alloc);
	rename_src[rename_src_nr].p = p;
	rename_src[rename_src_nr].score = p->score;
	rename_src_nr++;
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

struct prefetch_options {
	struct repository *repo;
	int skip_unmodified;
};
static void prefetch(void *prefetch_options)
{
	struct prefetch_options *options = prefetch_options;
	int i;
	struct oid_array to_fetch = OID_ARRAY_INIT;

	for (i = 0; i < rename_dst_nr; i++) {
		if (rename_dst[i].p->renamed_pair)
			/*
			 * The loop in diffcore_rename() will not need these
			 * blobs, so skip prefetching.
			 */
			continue; /* already found exact match */
		diff_add_if_missing(options->repo, &to_fetch,
				    rename_dst[i].p->two);
	}
	for (i = 0; i < rename_src_nr; i++) {
		if (options->skip_unmodified &&
		    diff_unmodified_pair(rename_src[i].p))
			/*
			 * The loop in diffcore_rename() will not need these
			 * blobs, so skip prefetching.
			 */
			continue;
		diff_add_if_missing(options->repo, &to_fetch,
				    rename_src[i].p->one);
	}
	promisor_remote_get_direct(options->repo, to_fetch.oid, to_fetch.nr);
	oid_array_clear(&to_fetch);
}

static int estimate_similarity(struct repository *r,
			       struct diff_filespec *src,
			       struct diff_filespec *dst,
			       int minimum_score,
			       int skip_unmodified)
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
	struct diff_populate_filespec_options dpf_options = {
		.check_size_only = 1
	};
	struct prefetch_options prefetch_options = {r, skip_unmodified};

	if (r == the_repository && has_promisor_remote()) {
		dpf_options.missing_object_cb = prefetch;
		dpf_options.missing_object_data = &prefetch_options;
	}

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
	    diff_populate_filespec(r, src, &dpf_options))
		return 0;
	if (!dst->cnt_data &&
	    diff_populate_filespec(r, dst, &dpf_options))
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

	dpf_options.check_size_only = 0;

	if (!src->cnt_data && diff_populate_filespec(r, src, &dpf_options))
		return 0;
	if (!dst->cnt_data && diff_populate_filespec(r, dst, &dpf_options))
		return 0;

	if (diffcore_count_changes(r, src, dst,
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
	struct diff_filepair *src = rename_src[src_index].p;
	struct diff_filepair *dst = rename_dst[dst_index].p;

	if (dst->renamed_pair)
		die("internal error: dst already matched.");

	src->one->rename_used++;
	src->one->count++;

	rename_dst[dst_index].filespec_to_free = dst->one;
	rename_dst[dst_index].is_rename = 1;

	dst->one = src->one;
	dst->renamed_pair = 1;
	if (!strcmp(dst->one->path, dst->two->path))
		dst->score = rename_src[src_index].score;
	else
		dst->score = score;
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

static unsigned int hash_filespec(struct repository *r,
				  struct diff_filespec *filespec)
{
	if (!filespec->oid_valid) {
		if (diff_populate_filespec(r, filespec, NULL))
			return 0;
		hash_object_file(r->hash_algo, filespec->data, filespec->size,
				 "blob", &filespec->oid);
	}
	return oidhash(&filespec->oid);
}

static int find_identical_files(struct hashmap *srcs,
				int dst_index,
				struct diff_options *options)
{
	int renames = 0;
	struct diff_filespec *target = rename_dst[dst_index].p->two;
	struct file_similarity *p, *best = NULL;
	int i = 100, best_score = -1;
	unsigned int hash = hash_filespec(options->repo, target);

	/*
	 * Find the best source match for specified destination.
	 */
	p = hashmap_get_entry_from_hash(srcs, hash, NULL,
					struct file_similarity, entry);
	hashmap_for_each_entry_from(srcs, p, entry) {
		int score;
		struct diff_filespec *source = p->filespec;

		/* False hash collision? */
		if (!oideq(&source->oid, &target->oid))
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

static void insert_file_table(struct repository *r,
			      struct hashmap *table, int index,
			      struct diff_filespec *filespec)
{
	struct file_similarity *entry = xmalloc(sizeof(*entry));

	entry->index = index;
	entry->filespec = filespec;

	hashmap_entry_init(&entry->entry, hash_filespec(r, filespec));
	hashmap_add(table, &entry->entry);
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
	hashmap_init(&file_table, NULL, NULL, rename_src_nr);
	for (i = rename_src_nr-1; i >= 0; i--)
		insert_file_table(options->repo,
				  &file_table, i,
				  rename_src[i].p->one);

	/* Walk the destinations and find best source match */
	for (i = 0; i < rename_dst_nr; i++)
		renames += find_identical_files(&file_table, i, options);

	/* Free the hash data structure and entries */
	hashmap_clear_and_free(&file_table, struct file_similarity, entry);

	return renames;
}

static const char *get_basename(const char *filename)
{
	/*
	 * gitbasename() has to worry about special drives, multiple
	 * directory separator characters, trailing slashes, NULL or
	 * empty strings, etc.  We only work on filenames as stored in
	 * git, and thus get to ignore all those complications.
	 */
	const char *base = strrchr(filename, '/');
	return base ? base + 1 : filename;
}

static int find_basename_matches(struct diff_options *options,
				 int minimum_score)
{
	/*
	 * When I checked in early 2020, over 76% of file renames in linux
	 * just moved files to a different directory but kept the same
	 * basename.  gcc did that with over 64% of renames, gecko did it
	 * with over 79%, and WebKit did it with over 89%.
	 *
	 * Therefore we can bypass the normal exhaustive NxM matrix
	 * comparison of similarities between all potential rename sources
	 * and destinations by instead using file basename as a hint (i.e.
	 * the portion of the filename after the last '/'), checking for
	 * similarity between files with the same basename, and if we find
	 * a pair that are sufficiently similar, record the rename pair and
	 * exclude those two from the NxM matrix.
	 *
	 * This *might* cause us to find a less than optimal pairing (if
	 * there is another file that we are even more similar to but has a
	 * different basename).  Given the huge performance advantage
	 * basename matching provides, and given the frequency with which
	 * people use the same basename in real world projects, that's a
	 * trade-off we are willing to accept when doing just rename
	 * detection.
	 *
	 * If someone wants copy detection that implies they are willing to
	 * spend more cycles to find similarities between files, so it may
	 * be less likely that this heuristic is wanted.  If someone is
	 * doing break detection, that means they do not want filename
	 * similarity to imply any form of content similiarity, and thus
	 * this heuristic would definitely be incompatible.
	 */

	int i, renames = 0;
	struct strintmap sources;
	struct strintmap dests;
	struct hashmap_iter iter;
	struct strmap_entry *entry;

	/*
	 * The prefeteching stuff wants to know if it can skip prefetching
	 * blobs that are unmodified...and will then do a little extra work
	 * to verify that the oids are indeed different before prefetching.
	 * Unmodified blobs are only relevant when doing copy detection;
	 * when limiting to rename detection, diffcore_rename[_extended]()
	 * will never be called with unmodified source paths fed to us, so
	 * the extra work necessary to check if rename_src entries are
	 * unmodified would be a small waste.
	 */
	int skip_unmodified = 0;

	/*
	 * Create maps of basename -> fullname(s) for remaining sources and
	 * dests.
	 */
	strintmap_init_with_options(&sources, -1, NULL, 0);
	strintmap_init_with_options(&dests, -1, NULL, 0);
	for (i = 0; i < rename_src_nr; ++i) {
		char *filename = rename_src[i].p->one->path;
		const char *base;

		/* exact renames removed in remove_unneeded_paths_from_src() */
		assert(!rename_src[i].p->one->rename_used);

		/* Record index within rename_src (i) if basename is unique */
		base = get_basename(filename);
		if (strintmap_contains(&sources, base))
			strintmap_set(&sources, base, -1);
		else
			strintmap_set(&sources, base, i);
	}
	for (i = 0; i < rename_dst_nr; ++i) {
		char *filename = rename_dst[i].p->two->path;
		const char *base;

		if (rename_dst[i].is_rename)
			continue; /* involved in exact match already. */

		/* Record index within rename_dst (i) if basename is unique */
		base = get_basename(filename);
		if (strintmap_contains(&dests, base))
			strintmap_set(&dests, base, -1);
		else
			strintmap_set(&dests, base, i);
	}

	/* Now look for basename matchups and do similarity estimation */
	strintmap_for_each_entry(&sources, &iter, entry) {
		const char *base = entry->key;
		intptr_t src_index = (intptr_t)entry->value;
		intptr_t dst_index;
		if (src_index == -1)
			continue;

		if (0 <= (dst_index = strintmap_get(&dests, base))) {
			struct diff_filespec *one, *two;
			int score;

			/* Estimate the similarity */
			one = rename_src[src_index].p->one;
			two = rename_dst[dst_index].p->two;
			score = estimate_similarity(options->repo, one, two,
						    minimum_score, skip_unmodified);

			/* If sufficiently similar, record as rename pair */
			if (score < minimum_score)
				continue;
			record_rename_pair(dst_index, src_index, score);
			renames++;

			/*
			 * Found a rename so don't need text anymore; if we
			 * didn't find a rename, the filespec_blob would get
			 * re-used when doing the matrix of comparisons.
			 */
			diff_free_filespec_blob(one);
			diff_free_filespec_blob(two);
		}
	}

	strintmap_clear(&sources);
	strintmap_clear(&dests);

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
static int too_many_rename_candidates(int num_destinations, int num_sources,
				      struct diff_options *options)
{
	int rename_limit = options->rename_limit;
	int i, limited_sources;

	options->needed_rename_limit = 0;

	/*
	 * This basically does a test for the rename matrix not
	 * growing larger than a "rename_limit" square matrix, ie:
	 *
	 *    num_destinations * num_sources > rename_limit * rename_limit
	 *
	 * We use st_mult() to check overflow conditions; in the
	 * exceptional circumstance that size_t isn't large enough to hold
	 * the multiplication, the system won't be able to allocate enough
	 * memory for the matrix anyway.
	 */
	if (rename_limit <= 0)
		rename_limit = 32767;
	if (st_mult(num_destinations, num_sources)
	    <= st_mult(rename_limit, rename_limit))
		return 0;

	options->needed_rename_limit =
		num_sources > num_destinations ? num_sources : num_destinations;

	/* Are we running under -C -C? */
	if (!options->flags.find_copies_harder)
		return 1;

	/* Would we bust the limit if we were running under -C? */
	for (limited_sources = i = 0; i < num_sources; i++) {
		if (diff_unmodified_pair(rename_src[i].p))
			continue;
		limited_sources++;
	}
	if (st_mult(num_destinations, limited_sources)
	    <= st_mult(rename_limit, rename_limit))
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
		if (dst->is_rename)
			continue; /* already done, either exact or fuzzy. */
		if (!copies && rename_src[mx[i].src].p->one->rename_used)
			continue;
		record_rename_pair(mx[i].dst, mx[i].src, mx[i].score);
		count++;
	}
	return count;
}

static void remove_unneeded_paths_from_src(int detecting_copies)
{
	int i, new_num_src;

	if (detecting_copies)
		return; /* nothing to remove */
	if (break_idx)
		return; /* culling incompatible with break detection */

	/*
	 * Note on reasons why we cull unneeded sources but not destinations:
	 *   1) Pairings are stored in rename_dst (not rename_src), which we
	 *      need to keep around.  So, we just can't cull rename_dst even
	 *      if we wanted to.  But doing so wouldn't help because...
	 *
	 *   2) There is a matrix pairwise comparison that follows the
	 *      "Performing inexact rename detection" progress message.
	 *      Iterating over the destinations is done in the outer loop,
	 *      hence we only iterate over each of those once and we can
	 *      easily skip the outer loop early if the destination isn't
	 *      relevant.  That's only one check per destination path to
	 *      skip.
	 *
	 *      By contrast, the sources are iterated in the inner loop; if
	 *      we check whether a source can be skipped, then we'll be
	 *      checking it N separate times, once for each destination.
	 *      We don't want to have to iterate over known-not-needed
	 *      sources N times each, so avoid that by removing the sources
	 *      from rename_src here.
	 */
	for (i = 0, new_num_src = 0; i < rename_src_nr; i++) {
		/*
		 * renames are stored in rename_dst, so if a rename has
		 * already been detected using this source, we can just
		 * remove the source knowing rename_dst has its info.
		 */
		if (rename_src[i].p->one->rename_used)
			continue;

		if (new_num_src < i)
			memcpy(&rename_src[new_num_src], &rename_src[i],
			       sizeof(struct diff_rename_src));
		new_num_src++;
	}

	rename_src_nr = new_num_src;
}

void diffcore_rename(struct diff_options *options)
{
	int detect_rename = options->detect_rename;
	int minimum_score = options->rename_score;
	struct diff_queue_struct *q = &diff_queued_diff;
	struct diff_queue_struct outq;
	struct diff_score *mx;
	int i, j, rename_count, skip_unmodified = 0;
	int num_destinations, dst_cnt;
	int num_sources, want_copies;
	struct progress *progress = NULL;

	trace2_region_enter("diff", "setup", options->repo);
	want_copies = (detect_rename == DIFF_DETECT_COPY);
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
			else if (!options->flags.rename_empty &&
				 is_empty_blob_oid(&p->two->oid))
				continue;
			else if (add_rename_dst(p) < 0) {
				warning("skipping rename detection, detected"
					" duplicate destination '%s'",
					p->two->path);
				goto cleanup;
			}
		}
		else if (!options->flags.rename_empty &&
			 is_empty_blob_oid(&p->one->oid))
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
		else if (want_copies) {
			/*
			 * Increment the "rename_used" score by
			 * one, to indicate ourselves as a user.
			 */
			p->one->rename_used++;
			register_rename_src(p);
		}
	}
	trace2_region_leave("diff", "setup", options->repo);
	if (rename_dst_nr == 0 || rename_src_nr == 0)
		goto cleanup; /* nothing to do */

	trace2_region_enter("diff", "exact renames", options->repo);
	/*
	 * We really want to cull the candidates list early
	 * with cheap tests in order to avoid doing deltas.
	 */
	rename_count = find_exact_renames(options);
	trace2_region_leave("diff", "exact renames", options->repo);

	/* Did we only want exact renames? */
	if (minimum_score == MAX_SCORE)
		goto cleanup;

	num_sources = rename_src_nr;

	if (want_copies || break_idx) {
		/*
		 * Cull sources:
		 *   - remove ones corresponding to exact renames
		 */
		trace2_region_enter("diff", "cull after exact", options->repo);
		remove_unneeded_paths_from_src(want_copies);
		trace2_region_leave("diff", "cull after exact", options->repo);
	} else {
		/* Determine minimum score to match basenames */
		double factor = 0.5;
		char *basename_factor = getenv("GIT_BASENAME_FACTOR");
		int min_basename_score;

		if (basename_factor)
			factor = strtol(basename_factor, NULL, 10)/100.0;
		assert(factor >= 0.0 && factor <= 1.0);
		min_basename_score = minimum_score +
			(int)(factor * (MAX_SCORE - minimum_score));

		/*
		 * Cull sources:
		 *   - remove ones involved in renames (found via exact match)
		 */
		trace2_region_enter("diff", "cull after exact", options->repo);
		remove_unneeded_paths_from_src(want_copies);
		trace2_region_leave("diff", "cull after exact", options->repo);

		/* Utilize file basenames to quickly find renames. */
		trace2_region_enter("diff", "basename matches", options->repo);
		rename_count += find_basename_matches(options,
						      min_basename_score);
		trace2_region_leave("diff", "basename matches", options->repo);

		/*
		 * Cull sources, again:
		 *   - remove ones involved in renames (found via basenames)
		 */
		trace2_region_enter("diff", "cull basename", options->repo);
		remove_unneeded_paths_from_src(want_copies);
		trace2_region_leave("diff", "cull basename", options->repo);
	}

	/* Calculate how many rename destinations are left */
	num_destinations = (rename_dst_nr - rename_count);
	num_sources = rename_src_nr; /* rename_src_nr reflects lower number */

	/* All done? */
	if (!num_destinations || !num_sources)
		goto cleanup;

	switch (too_many_rename_candidates(num_destinations, num_sources,
					   options)) {
	case 1:
		goto cleanup;
	case 2:
		options->degraded_cc_to_c = 1;
		skip_unmodified = 1;
		break;
	default:
		break;
	}

	trace2_region_enter("diff", "inexact renames", options->repo);
	if (options->show_rename_progress) {
		progress = start_delayed_progress(
				_("Performing inexact rename detection"),
				(uint64_t)num_destinations * (uint64_t)num_sources);
	}

	mx = xcalloc(st_mult(NUM_CANDIDATE_PER_DST, num_destinations),
		     sizeof(*mx));
	for (dst_cnt = i = 0; i < rename_dst_nr; i++) {
		struct diff_filespec *two = rename_dst[i].p->two;
		struct diff_score *m;

		if (rename_dst[i].is_rename)
			continue; /* exact or basename match already handled */

		m = &mx[dst_cnt * NUM_CANDIDATE_PER_DST];
		for (j = 0; j < NUM_CANDIDATE_PER_DST; j++)
			m[j].dst = -1;

		for (j = 0; j < rename_src_nr; j++) {
			struct diff_filespec *one = rename_src[j].p->one;
			struct diff_score this_src;

			assert(!one->rename_used || want_copies || break_idx);

			if (skip_unmodified &&
			    diff_unmodified_pair(rename_src[j].p))
				continue;

			this_src.score = estimate_similarity(options->repo,
							     one, two,
							     minimum_score,
							     skip_unmodified);
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
		display_progress(progress,
				 (uint64_t)dst_cnt * (uint64_t)num_sources);
	}
	stop_progress(&progress);

	/* cost matrix sorted by most to least similar pair */
	STABLE_QSORT(mx, dst_cnt * NUM_CANDIDATE_PER_DST, score_compare);

	rename_count += find_renames(mx, dst_cnt, minimum_score, 0);
	if (want_copies)
		rename_count += find_renames(mx, dst_cnt, minimum_score, 1);
	free(mx);
	trace2_region_leave("diff", "inexact renames", options->repo);

 cleanup:
	/* At this point, we have found some renames and copies and they
	 * are recorded in rename_dst.  The original list is still in *q.
	 */
	trace2_region_enter("diff", "write back to queue", options->repo);
	DIFF_QUEUE_CLEAR(&outq);
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		struct diff_filepair *pair_to_free = NULL;

		if (DIFF_PAIR_UNMERGED(p)) {
			diff_q(&outq, p);
		}
		else if (!DIFF_FILE_VALID(p->one) && DIFF_FILE_VALID(p->two)) {
			/* Creation */
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
				struct diff_rename_dst *dst = locate_rename_dst(p);
				if (!dst)
					BUG("tracking failed somehow; failed to find associated dst for broken pair");
				if (dst->is_rename)
					/* counterpart is now rename/copy */
					pair_to_free = p;
			}
			else {
				if (p->one->rename_used)
					/* this path remains */
					pair_to_free = p;
			}

			if (!pair_to_free)
				diff_q(&outq, p);
		}
		else if (!diff_unmodified_pair(p))
			/* all the usual ones need to be kept */
			diff_q(&outq, p);
		else
			/* no need to keep unmodified pairs; FIXME: remove earlier? */
			pair_to_free = p;

		if (pair_to_free)
			diff_free_filepair(pair_to_free);
	}
	diff_debug_queue("done copying original", &outq);

	free(q->queue);
	*q = outq;
	diff_debug_queue("done collapsing", q);

	for (i = 0; i < rename_dst_nr; i++)
		if (rename_dst[i].filespec_to_free)
			free_filespec(rename_dst[i].filespec_to_free);

	FREE_AND_NULL(rename_dst);
	rename_dst_nr = rename_dst_alloc = 0;
	FREE_AND_NULL(rename_src);
	rename_src_nr = rename_src_alloc = 0;
	if (break_idx) {
		strintmap_clear(break_idx);
		FREE_AND_NULL(break_idx);
	}
	trace2_region_leave("diff", "write back to queue", options->repo);
	return;
}
