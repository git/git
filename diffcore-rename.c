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
			strintmap_init_with_options(break_idx, -1, NULL, 0);
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

struct inexact_prefetch_options {
	struct repository *repo;
	int skip_unmodified;
};
static void inexact_prefetch(void *prefetch_options)
{
	struct inexact_prefetch_options *options = prefetch_options;
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
			       struct diff_populate_filespec_options *dpf_opt)
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
	dpf_opt->check_size_only = 1;

	if (!src->cnt_data &&
	    diff_populate_filespec(r, src, dpf_opt))
		return 0;
	if (!dst->cnt_data &&
	    diff_populate_filespec(r, dst, dpf_opt))
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

	dpf_opt->check_size_only = 0;

	if (!src->cnt_data && diff_populate_filespec(r, src, dpf_opt))
		return 0;
	if (!dst->cnt_data && diff_populate_filespec(r, dst, dpf_opt))
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
			      struct mem_pool *pool,
			      struct hashmap *table, int index,
			      struct diff_filespec *filespec)
{
	struct file_similarity *entry = mem_pool_alloc(pool, sizeof(*entry));

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
static int find_exact_renames(struct diff_options *options,
			      struct mem_pool *pool)
{
	int i, renames = 0;
	struct hashmap file_table;

	/* Add all sources to the hash table in reverse order, because
	 * later on they will be retrieved in LIFO order.
	 */
	hashmap_init(&file_table, NULL, NULL, rename_src_nr);
	for (i = rename_src_nr-1; i >= 0; i--)
		insert_file_table(options->repo, pool,
				  &file_table, i,
				  rename_src[i].p->one);

	/* Walk the destinations and find best source match */
	for (i = 0; i < rename_dst_nr; i++)
		renames += find_identical_files(&file_table, i, options);

	/* Free the hash data structure (entries will be freed with the pool) */
	hashmap_clear(&file_table);

	return renames;
}

struct dir_rename_info {
	struct strintmap idx_map;
	struct strmap dir_rename_guess;
	struct strmap *dir_rename_count;
	struct strintmap *relevant_source_dirs;
	unsigned setup;
};

static char *get_dirname(const char *filename)
{
	char *slash = strrchr(filename, '/');
	return slash ? xstrndup(filename, slash - filename) : xstrdup("");
}

static void dirname_munge(char *filename)
{
	char *slash = strrchr(filename, '/');
	if (!slash)
		slash = filename;
	*slash = '\0';
}

static const char *get_highest_rename_path(struct strintmap *counts)
{
	int highest_count = 0;
	const char *highest_destination_dir = NULL;
	struct hashmap_iter iter;
	struct strmap_entry *entry;

	strintmap_for_each_entry(counts, &iter, entry) {
		const char *destination_dir = entry->key;
		intptr_t count = (intptr_t)entry->value;
		if (count > highest_count) {
			highest_count = count;
			highest_destination_dir = destination_dir;
		}
	}
	return highest_destination_dir;
}

static char *UNKNOWN_DIR = "/";  /* placeholder -- short, illegal directory */

static int dir_rename_already_determinable(struct strintmap *counts)
{
	struct hashmap_iter iter;
	struct strmap_entry *entry;
	int first = 0, second = 0, unknown = 0;
	strintmap_for_each_entry(counts, &iter, entry) {
		const char *destination_dir = entry->key;
		intptr_t count = (intptr_t)entry->value;
		if (!strcmp(destination_dir, UNKNOWN_DIR)) {
			unknown = count;
		} else if (count >= first) {
			second = first;
			first = count;
		} else if (count >= second) {
			second = count;
		}
	}
	return first > second + unknown;
}

static void increment_count(struct dir_rename_info *info,
			    char *old_dir,
			    char *new_dir)
{
	struct strintmap *counts;
	struct strmap_entry *e;

	/* Get the {new_dirs -> counts} mapping using old_dir */
	e = strmap_get_entry(info->dir_rename_count, old_dir);
	if (e) {
		counts = e->value;
	} else {
		counts = xmalloc(sizeof(*counts));
		strintmap_init_with_options(counts, 0, NULL, 1);
		strmap_put(info->dir_rename_count, old_dir, counts);
	}

	/* Increment the count for new_dir */
	strintmap_incr(counts, new_dir, 1);
}

static void update_dir_rename_counts(struct dir_rename_info *info,
				     struct strintmap *dirs_removed,
				     const char *oldname,
				     const char *newname)
{
	char *old_dir;
	char *new_dir;
	const char new_dir_first_char = newname[0];
	int first_time_in_loop = 1;

	if (!info->setup)
		/*
		 * info->setup is 0 here in two cases: (1) all auxiliary
		 * vars (like dirs_removed) were NULL so
		 * initialize_dir_rename_info() returned early, or (2)
		 * either break detection or copy detection are active so
		 * that we never called initialize_dir_rename_info().  In
		 * the former case, we don't have enough info to know if
		 * directories were renamed (because dirs_removed lets us
		 * know about a necessary prerequisite, namely if they were
		 * removed), and in the latter, we don't care about
		 * directory renames or find_basename_matches.
		 *
		 * This matters because both basename and inexact matching
		 * will also call update_dir_rename_counts().  In either of
		 * the above two cases info->dir_rename_counts will not
		 * have been properly initialized which prevents us from
		 * updating it, but in these two cases we don't care about
		 * dir_rename_counts anyway, so we can just exit early.
		 */
		return;


	old_dir = xstrdup(oldname);
	new_dir = xstrdup(newname);

	while (1) {
		int drd_flag = NOT_RELEVANT;

		/* Get old_dir, skip if its directory isn't relevant. */
		dirname_munge(old_dir);
		if (info->relevant_source_dirs &&
		    !strintmap_contains(info->relevant_source_dirs, old_dir))
			break;

		/* Get new_dir */
		dirname_munge(new_dir);

		/*
		 * When renaming
		 *   "a/b/c/d/e/foo.c" -> "a/b/some/thing/else/e/foo.c"
		 * then this suggests that both
		 *   a/b/c/d/e/ => a/b/some/thing/else/e/
		 *   a/b/c/d/   => a/b/some/thing/else/
		 * so we want to increment counters for both.  We do NOT,
		 * however, also want to suggest that there was the following
		 * rename:
		 *   a/b/c/ => a/b/some/thing/
		 * so we need to quit at that point.
		 *
		 * Note the when first_time_in_loop, we only strip off the
		 * basename, and we don't care if that's different.
		 */
		if (!first_time_in_loop) {
			char *old_sub_dir = strchr(old_dir, '\0')+1;
			char *new_sub_dir = strchr(new_dir, '\0')+1;
			if (!*new_dir) {
				/*
				 * Special case when renaming to root directory,
				 * i.e. when new_dir == "".  In this case, we had
				 * something like
				 *    a/b/subdir => subdir
				 * and so dirname_munge() sets things up so that
				 *    old_dir = "a/b\0subdir\0"
				 *    new_dir = "\0ubdir\0"
				 * We didn't have a '/' to overwrite a '\0' onto
				 * in new_dir, so we have to compare differently.
				 */
				if (new_dir_first_char != old_sub_dir[0] ||
				    strcmp(old_sub_dir+1, new_sub_dir))
					break;
			} else {
				if (strcmp(old_sub_dir, new_sub_dir))
					break;
			}
		}

		/*
		 * Above we suggested that we'd keep recording renames for
		 * all ancestor directories where the trailing directories
		 * matched, i.e. for
		 *   "a/b/c/d/e/foo.c" -> "a/b/some/thing/else/e/foo.c"
		 * we'd increment rename counts for each of
		 *   a/b/c/d/e/ => a/b/some/thing/else/e/
		 *   a/b/c/d/   => a/b/some/thing/else/
		 * However, we only need the rename counts for directories
		 * in dirs_removed whose value is RELEVANT_FOR_SELF.
		 * However, we add one special case of also recording it for
		 * first_time_in_loop because find_basename_matches() can
		 * use that as a hint to find a good pairing.
		 */
		if (dirs_removed)
			drd_flag = strintmap_get(dirs_removed, old_dir);
		if (drd_flag == RELEVANT_FOR_SELF || first_time_in_loop)
			increment_count(info, old_dir, new_dir);

		first_time_in_loop = 0;
		if (drd_flag == NOT_RELEVANT)
			break;
		/* If we hit toplevel directory ("") for old or new dir, quit */
		if (!*old_dir || !*new_dir)
			break;
	}

	/* Free resources we don't need anymore */
	free(old_dir);
	free(new_dir);
}

static void initialize_dir_rename_info(struct dir_rename_info *info,
				       struct strintmap *relevant_sources,
				       struct strintmap *dirs_removed,
				       struct strmap *dir_rename_count,
				       struct strmap *cached_pairs)
{
	struct hashmap_iter iter;
	struct strmap_entry *entry;
	int i;

	if (!dirs_removed && !relevant_sources) {
		info->setup = 0;
		return;
	}
	info->setup = 1;

	info->dir_rename_count = dir_rename_count;
	if (!info->dir_rename_count) {
		info->dir_rename_count = xmalloc(sizeof(*dir_rename_count));
		strmap_init(info->dir_rename_count);
	}
	strintmap_init_with_options(&info->idx_map, -1, NULL, 0);
	strmap_init_with_options(&info->dir_rename_guess, NULL, 0);

	/* Setup info->relevant_source_dirs */
	info->relevant_source_dirs = NULL;
	if (dirs_removed || !relevant_sources) {
		info->relevant_source_dirs = dirs_removed; /* might be NULL */
	} else {
		info->relevant_source_dirs = xmalloc(sizeof(struct strintmap));
		strintmap_init(info->relevant_source_dirs, 0 /* unused */);
		strintmap_for_each_entry(relevant_sources, &iter, entry) {
			char *dirname = get_dirname(entry->key);
			if (!dirs_removed ||
			    strintmap_contains(dirs_removed, dirname))
				strintmap_set(info->relevant_source_dirs,
					      dirname, 0 /* value irrelevant */);
			free(dirname);
		}
	}

	/*
	 * Loop setting up both info->idx_map, and doing setup of
	 * info->dir_rename_count.
	 */
	for (i = 0; i < rename_dst_nr; ++i) {
		/*
		 * For non-renamed files, make idx_map contain mapping of
		 *   filename -> index (index within rename_dst, that is)
		 */
		if (!rename_dst[i].is_rename) {
			char *filename = rename_dst[i].p->two->path;
			strintmap_set(&info->idx_map, filename, i);
			continue;
		}

		/*
		 * For everything else (i.e. renamed files), make
		 * dir_rename_count contain a map of a map:
		 *   old_directory -> {new_directory -> count}
		 * In other words, for every pair look at the directories for
		 * the old filename and the new filename and count how many
		 * times that pairing occurs.
		 */
		update_dir_rename_counts(info, dirs_removed,
					 rename_dst[i].p->one->path,
					 rename_dst[i].p->two->path);
	}

	/* Add cached_pairs to counts */
	strmap_for_each_entry(cached_pairs, &iter, entry) {
		const char *old_name = entry->key;
		const char *new_name = entry->value;
		if (!new_name)
			/* known delete; ignore it */
			continue;

		update_dir_rename_counts(info, dirs_removed, old_name, new_name);
	}

	/*
	 * Now we collapse
	 *    dir_rename_count: old_directory -> {new_directory -> count}
	 * down to
	 *    dir_rename_guess: old_directory -> best_new_directory
	 * where best_new_directory is the one with the highest count.
	 */
	strmap_for_each_entry(info->dir_rename_count, &iter, entry) {
		/* entry->key is source_dir */
		struct strintmap *counts = entry->value;
		char *best_newdir;

		best_newdir = xstrdup(get_highest_rename_path(counts));
		strmap_put(&info->dir_rename_guess, entry->key,
			   best_newdir);
	}
}

void partial_clear_dir_rename_count(struct strmap *dir_rename_count)
{
	struct hashmap_iter iter;
	struct strmap_entry *entry;

	strmap_for_each_entry(dir_rename_count, &iter, entry) {
		struct strintmap *counts = entry->value;
		strintmap_clear(counts);
	}
	strmap_partial_clear(dir_rename_count, 1);
}

static void cleanup_dir_rename_info(struct dir_rename_info *info,
				    struct strintmap *dirs_removed,
				    int keep_dir_rename_count)
{
	struct hashmap_iter iter;
	struct strmap_entry *entry;
	struct string_list to_remove = STRING_LIST_INIT_NODUP;
	int i;

	if (!info->setup)
		return;

	/* idx_map */
	strintmap_clear(&info->idx_map);

	/* dir_rename_guess */
	strmap_clear(&info->dir_rename_guess, 1);

	/* relevant_source_dirs */
	if (info->relevant_source_dirs &&
	    info->relevant_source_dirs != dirs_removed) {
		strintmap_clear(info->relevant_source_dirs);
		FREE_AND_NULL(info->relevant_source_dirs);
	}

	/* dir_rename_count */
	if (!keep_dir_rename_count) {
		partial_clear_dir_rename_count(info->dir_rename_count);
		strmap_clear(info->dir_rename_count, 1);
		FREE_AND_NULL(info->dir_rename_count);
		return;
	}

	/*
	 * Although dir_rename_count was passed in
	 * diffcore_rename_extended() and we want to keep it around and
	 * return it to that caller, we first want to remove any counts in
	 * the maps associated with UNKNOWN_DIR entries and any data
	 * associated with directories that weren't renamed.
	 */
	strmap_for_each_entry(info->dir_rename_count, &iter, entry) {
		const char *source_dir = entry->key;
		struct strintmap *counts = entry->value;

		if (!strintmap_get(dirs_removed, source_dir)) {
			string_list_append(&to_remove, source_dir);
			strintmap_clear(counts);
			continue;
		}

		if (strintmap_contains(counts, UNKNOWN_DIR))
			strintmap_remove(counts, UNKNOWN_DIR);
	}
	for (i = 0; i < to_remove.nr; ++i)
		strmap_remove(info->dir_rename_count,
			      to_remove.items[i].string, 1);
	string_list_clear(&to_remove, 0);
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

static int idx_possible_rename(char *filename, struct dir_rename_info *info)
{
	/*
	 * Our comparison of files with the same basename (see
	 * find_basename_matches() below), is only helpful when after exact
	 * rename detection we have exactly one file with a given basename
	 * among the rename sources and also only exactly one file with
	 * that basename among the rename destinations.  When we have
	 * multiple files with the same basename in either set, we do not
	 * know which to compare against.  However, there are some
	 * filenames that occur in large numbers (particularly
	 * build-related filenames such as 'Makefile', '.gitignore', or
	 * 'build.gradle' that potentially exist within every single
	 * subdirectory), and for performance we want to be able to quickly
	 * find renames for these files too.
	 *
	 * The reason basename comparisons are a useful heuristic was that it
	 * is common for people to move files across directories while keeping
	 * their filename the same.  If we had a way of determining or even
	 * making a good educated guess about which directory these non-unique
	 * basename files had moved the file to, we could check it.
	 * Luckily...
	 *
	 * When an entire directory is in fact renamed, we have two factors
	 * helping us out:
	 *   (a) the original directory disappeared giving us a hint
	 *       about when we can apply an extra heuristic.
	 *   (a) we often have several files within that directory and
	 *       subdirectories that are renamed without changes
	 * So, rules for a heuristic:
	 *   (0) If there basename matches are non-unique (the condition under
	 *       which this function is called) AND
	 *   (1) the directory in which the file was found has disappeared
	 *       (i.e. dirs_removed is non-NULL and has a relevant entry) THEN
	 *   (2) use exact renames of files within the directory to determine
	 *       where the directory is likely to have been renamed to.  IF
	 *       there is at least one exact rename from within that
	 *       directory, we can proceed.
	 *   (3) If there are multiple places the directory could have been
	 *       renamed to based on exact renames, ignore all but one of them.
	 *       Just use the destination with the most renames going to it.
	 *   (4) Check if applying that directory rename to the original file
	 *       would result in a destination filename that is in the
	 *       potential rename set.  If so, return the index of the
	 *       destination file (the index within rename_dst).
	 *   (5) Compare the original file and returned destination for
	 *       similarity, and if they are sufficiently similar, record the
	 *       rename.
	 *
	 * This function, idx_possible_rename(), is only responsible for (4).
	 * The conditions/steps in (1)-(3) are handled via setting up
	 * dir_rename_count and dir_rename_guess in
	 * initialize_dir_rename_info().  Steps (0) and (5) are handled by
	 * the caller of this function.
	 */
	char *old_dir, *new_dir;
	struct strbuf new_path = STRBUF_INIT;
	int idx;

	if (!info->setup)
		return -1;

	old_dir = get_dirname(filename);
	new_dir = strmap_get(&info->dir_rename_guess, old_dir);
	free(old_dir);
	if (!new_dir)
		return -1;

	strbuf_addstr(&new_path, new_dir);
	strbuf_addch(&new_path, '/');
	strbuf_addstr(&new_path, get_basename(filename));

	idx = strintmap_get(&info->idx_map, new_path.buf);
	strbuf_release(&new_path);
	return idx;
}

struct basename_prefetch_options {
	struct repository *repo;
	struct strintmap *relevant_sources;
	struct strintmap *sources;
	struct strintmap *dests;
	struct dir_rename_info *info;
};
static void basename_prefetch(void *prefetch_options)
{
	struct basename_prefetch_options *options = prefetch_options;
	struct strintmap *relevant_sources = options->relevant_sources;
	struct strintmap *sources = options->sources;
	struct strintmap *dests = options->dests;
	struct dir_rename_info *info = options->info;
	int i;
	struct oid_array to_fetch = OID_ARRAY_INIT;

	/*
	 * TODO: The following loops mirror the code/logic from
	 * find_basename_matches(), though not quite exactly.  Maybe
	 * abstract the iteration logic out somehow?
	 */
	for (i = 0; i < rename_src_nr; ++i) {
		char *filename = rename_src[i].p->one->path;
		const char *base = NULL;
		intptr_t src_index;
		intptr_t dst_index;

		/* Skip irrelevant sources */
		if (relevant_sources &&
		    !strintmap_contains(relevant_sources, filename))
			continue;

		/*
		 * If the basename is unique among remaining sources, then
		 * src_index will equal 'i' and we can attempt to match it
		 * to a unique basename in the destinations.  Otherwise,
		 * use directory rename heuristics, if possible.
		 */
		base = get_basename(filename);
		src_index = strintmap_get(sources, base);
		assert(src_index == -1 || src_index == i);

		if (strintmap_contains(dests, base)) {
			struct diff_filespec *one, *two;

			/* Find a matching destination, if possible */
			dst_index = strintmap_get(dests, base);
			if (src_index == -1 || dst_index == -1) {
				src_index = i;
				dst_index = idx_possible_rename(filename, info);
			}
			if (dst_index == -1)
				continue;

			/* Ignore this dest if already used in a rename */
			if (rename_dst[dst_index].is_rename)
				continue; /* already used previously */

			one = rename_src[src_index].p->one;
			two = rename_dst[dst_index].p->two;

			/* Add the pairs */
			diff_add_if_missing(options->repo, &to_fetch, two);
			diff_add_if_missing(options->repo, &to_fetch, one);
		}
	}

	promisor_remote_get_direct(options->repo, to_fetch.oid, to_fetch.nr);
	oid_array_clear(&to_fetch);
}

static int find_basename_matches(struct diff_options *options,
				 int minimum_score,
				 struct dir_rename_info *info,
				 struct strintmap *relevant_sources,
				 struct strintmap *dirs_removed)
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
	struct diff_populate_filespec_options dpf_options = {
		.check_binary = 0,
		.missing_object_cb = NULL,
		.missing_object_data = NULL
	};
	struct basename_prefetch_options prefetch_options = {
		.repo = options->repo,
		.relevant_sources = relevant_sources,
		.sources = &sources,
		.dests = &dests,
		.info = info
	};

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

	if (options->repo == the_repository && has_promisor_remote()) {
		dpf_options.missing_object_cb = basename_prefetch;
		dpf_options.missing_object_data = &prefetch_options;
	}

	/* Now look for basename matchups and do similarity estimation */
	for (i = 0; i < rename_src_nr; ++i) {
		char *filename = rename_src[i].p->one->path;
		const char *base = NULL;
		intptr_t src_index;
		intptr_t dst_index;

		/* Skip irrelevant sources */
		if (relevant_sources &&
		    !strintmap_contains(relevant_sources, filename))
			continue;

		/*
		 * If the basename is unique among remaining sources, then
		 * src_index will equal 'i' and we can attempt to match it
		 * to a unique basename in the destinations.  Otherwise,
		 * use directory rename heuristics, if possible.
		 */
		base = get_basename(filename);
		src_index = strintmap_get(&sources, base);
		assert(src_index == -1 || src_index == i);

		if (strintmap_contains(&dests, base)) {
			struct diff_filespec *one, *two;
			int score;

			/* Find a matching destination, if possible */
			dst_index = strintmap_get(&dests, base);
			if (src_index == -1 || dst_index == -1) {
				src_index = i;
				dst_index = idx_possible_rename(filename, info);
			}
			if (dst_index == -1)
				continue;

			/* Ignore this dest if already used in a rename */
			if (rename_dst[dst_index].is_rename)
				continue; /* already used previously */

			/* Estimate the similarity */
			one = rename_src[src_index].p->one;
			two = rename_dst[dst_index].p->two;
			score = estimate_similarity(options->repo, one, two,
						    minimum_score, &dpf_options);

			/* If sufficiently similar, record as rename pair */
			if (score < minimum_score)
				continue;
			record_rename_pair(dst_index, src_index, score);
			renames++;
			update_dir_rename_counts(info, dirs_removed,
						 one->path, two->path);

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
		return 0; /* treat as unlimited */
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

static int find_renames(struct diff_score *mx,
			int dst_cnt,
			int minimum_score,
			int copies,
			struct dir_rename_info *info,
			struct strintmap *dirs_removed)
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
		update_dir_rename_counts(info, dirs_removed,
					 rename_src[mx[i].src].p->one->path,
					 rename_dst[mx[i].dst].p->two->path);
	}
	return count;
}

static void remove_unneeded_paths_from_src(int detecting_copies,
					   struct strintmap *interesting)
{
	int i, new_num_src;

	if (detecting_copies && !interesting)
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
		struct diff_filespec *one = rename_src[i].p->one;

		/*
		 * renames are stored in rename_dst, so if a rename has
		 * already been detected using this source, we can just
		 * remove the source knowing rename_dst has its info.
		 */
		if (!detecting_copies && one->rename_used)
			continue;

		/* If we don't care about the source path, skip it */
		if (interesting && !strintmap_contains(interesting, one->path))
			continue;

		if (new_num_src < i)
			memcpy(&rename_src[new_num_src], &rename_src[i],
			       sizeof(struct diff_rename_src));
		new_num_src++;
	}

	rename_src_nr = new_num_src;
}

static void handle_early_known_dir_renames(struct dir_rename_info *info,
					   struct strintmap *relevant_sources,
					   struct strintmap *dirs_removed)
{
	/*
	 * Directory renames are determined via an aggregate of all renames
	 * under them and using a "majority wins" rule.  The fact that
	 * "majority wins", though, means we don't need all the renames
	 * under the given directory, we only need enough to ensure we have
	 * a majority.
	 */

	int i, new_num_src;
	struct hashmap_iter iter;
	struct strmap_entry *entry;

	if (!dirs_removed || !relevant_sources)
		return; /* nothing to cull */
	if (break_idx)
		return; /* culling incompatbile with break detection */

	/*
	 * Supplement dir_rename_count with number of potential renames,
	 * marking all potential rename sources as mapping to UNKNOWN_DIR.
	 */
	for (i = 0; i < rename_src_nr; i++) {
		char *old_dir;
		struct diff_filespec *one = rename_src[i].p->one;

		/*
		 * sources that are part of a rename will have already been
		 * removed by a prior call to remove_unneeded_paths_from_src()
		 */
		assert(!one->rename_used);

		old_dir = get_dirname(one->path);
		while (*old_dir != '\0' &&
		       NOT_RELEVANT != strintmap_get(dirs_removed, old_dir)) {
			char *freeme = old_dir;

			increment_count(info, old_dir, UNKNOWN_DIR);
			old_dir = get_dirname(old_dir);

			/* Free resources we don't need anymore */
			free(freeme);
		}
		/*
		 * old_dir and new_dir free'd in increment_count, but
		 * get_dirname() gives us a new pointer we need to free for
		 * old_dir.  Also, if the loop runs 0 times we need old_dir
		 * to be freed.
		 */
		free(old_dir);
	}

	/*
	 * For any directory which we need a potential rename detected for
	 * (i.e. those marked as RELEVANT_FOR_SELF in dirs_removed), check
	 * whether we have enough renames to satisfy the "majority rules"
	 * requirement such that detecting any more renames of files under
	 * it won't change the result.  For any such directory, mark that
	 * we no longer need to detect a rename for it.  However, since we
	 * might need to still detect renames for an ancestor of that
	 * directory, use RELEVANT_FOR_ANCESTOR.
	 */
	strmap_for_each_entry(info->dir_rename_count, &iter, entry) {
		/* entry->key is source_dir */
		struct strintmap *counts = entry->value;

		if (strintmap_get(dirs_removed, entry->key) ==
		    RELEVANT_FOR_SELF &&
		    dir_rename_already_determinable(counts)) {
			strintmap_set(dirs_removed, entry->key,
				      RELEVANT_FOR_ANCESTOR);
		}
	}

	for (i = 0, new_num_src = 0; i < rename_src_nr; i++) {
		struct diff_filespec *one = rename_src[i].p->one;
		int val;

		val = strintmap_get(relevant_sources, one->path);

		/*
		 * sources that were not found in relevant_sources should
		 * have already been removed by a prior call to
		 * remove_unneeded_paths_from_src()
		 */
		assert(val != -1);

		if (val == RELEVANT_LOCATION) {
			int removable = 1;
			char *dir = get_dirname(one->path);
			while (1) {
				char *freeme = dir;
				int res = strintmap_get(dirs_removed, dir);

				/* Quit if not found or irrelevant */
				if (res == NOT_RELEVANT)
					break;
				/* If RELEVANT_FOR_SELF, can't remove */
				if (res == RELEVANT_FOR_SELF) {
					removable = 0;
					break;
				}
				/* Else continue searching upwards */
				assert(res == RELEVANT_FOR_ANCESTOR);
				dir = get_dirname(dir);
				free(freeme);
			}
			free(dir);
			if (removable) {
				strintmap_set(relevant_sources, one->path,
					      RELEVANT_NO_MORE);
				continue;
			}
		}

		if (new_num_src < i)
			memcpy(&rename_src[new_num_src], &rename_src[i],
			       sizeof(struct diff_rename_src));
		new_num_src++;
	}

	rename_src_nr = new_num_src;
}

static void free_filespec_data(struct diff_filespec *spec)
{
	if (!--spec->count)
		diff_free_filespec_data(spec);
}

static void pool_free_filespec(struct mem_pool *pool,
			       struct diff_filespec *spec)
{
	if (!pool) {
		free_filespec(spec);
		return;
	}

	/*
	 * Similar to free_filespec(), but only frees the data.  The spec
	 * itself was allocated in the pool and should not be individually
	 * freed.
	 */
	free_filespec_data(spec);
}

void pool_diff_free_filepair(struct mem_pool *pool,
			     struct diff_filepair *p)
{
	if (!pool) {
		diff_free_filepair(p);
		return;
	}

	/*
	 * Similar to diff_free_filepair() but only frees the data from the
	 * filespecs; not the filespecs or the filepair which were
	 * allocated from the pool.
	 */
	free_filespec_data(p->one);
	free_filespec_data(p->two);
}

void diffcore_rename_extended(struct diff_options *options,
			      struct mem_pool *pool,
			      struct strintmap *relevant_sources,
			      struct strintmap *dirs_removed,
			      struct strmap *dir_rename_count,
			      struct strmap *cached_pairs)
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
	struct mem_pool local_pool;
	struct dir_rename_info info;
	struct diff_populate_filespec_options dpf_options = {
		.check_binary = 0,
		.missing_object_cb = NULL,
		.missing_object_data = NULL
	};
	struct inexact_prefetch_options prefetch_options = {
		.repo = options->repo
	};

	trace2_region_enter("diff", "setup", options->repo);
	info.setup = 0;
	assert(!dir_rename_count || strmap_empty(dir_rename_count));
	want_copies = (detect_rename == DIFF_DETECT_COPY);
	if (dirs_removed && (break_idx || want_copies))
		BUG("dirs_removed incompatible with break/copy detection");
	if (break_idx && relevant_sources)
		BUG("break detection incompatible with source specification");
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
	mem_pool_init(&local_pool, 32*1024);
	/*
	 * We really want to cull the candidates list early
	 * with cheap tests in order to avoid doing deltas.
	 */
	rename_count = find_exact_renames(options, &local_pool);
	/*
	 * Discard local_pool immediately instead of at "cleanup:" in order
	 * to reduce maximum memory usage; inexact rename detection uses up
	 * a fair amount of memory, and mem_pools can too.
	 */
	mem_pool_discard(&local_pool, 0);
	trace2_region_leave("diff", "exact renames", options->repo);

	/* Did we only want exact renames? */
	if (minimum_score == MAX_SCORE)
		goto cleanup;

	num_sources = rename_src_nr;

	if (want_copies || break_idx) {
		/*
		 * Cull sources:
		 *   - remove ones corresponding to exact renames
		 *   - remove ones not found in relevant_sources
		 */
		trace2_region_enter("diff", "cull after exact", options->repo);
		remove_unneeded_paths_from_src(want_copies, relevant_sources);
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
		remove_unneeded_paths_from_src(want_copies, NULL);
		trace2_region_leave("diff", "cull after exact", options->repo);

		/* Preparation for basename-driven matching. */
		trace2_region_enter("diff", "dir rename setup", options->repo);
		initialize_dir_rename_info(&info, relevant_sources,
					   dirs_removed, dir_rename_count,
					   cached_pairs);
		trace2_region_leave("diff", "dir rename setup", options->repo);

		/* Utilize file basenames to quickly find renames. */
		trace2_region_enter("diff", "basename matches", options->repo);
		rename_count += find_basename_matches(options,
						      min_basename_score,
						      &info,
						      relevant_sources,
						      dirs_removed);
		trace2_region_leave("diff", "basename matches", options->repo);

		/*
		 * Cull sources, again:
		 *   - remove ones involved in renames (found via basenames)
		 *   - remove ones not found in relevant_sources
		 * and
		 *   - remove ones in relevant_sources which are needed only
		 *     for directory renames IF no ancestory directory
		 *     actually needs to know any more individual path
		 *     renames under them
		 */
		trace2_region_enter("diff", "cull basename", options->repo);
		remove_unneeded_paths_from_src(want_copies, relevant_sources);
		handle_early_known_dir_renames(&info, relevant_sources,
					       dirs_removed);
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

	/* Finish setting up dpf_options */
	prefetch_options.skip_unmodified = skip_unmodified;
	if (options->repo == the_repository && has_promisor_remote()) {
		dpf_options.missing_object_cb = inexact_prefetch;
		dpf_options.missing_object_data = &prefetch_options;
	}

	CALLOC_ARRAY(mx, st_mult(NUM_CANDIDATE_PER_DST, num_destinations));
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
							     &dpf_options);
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

	rename_count += find_renames(mx, dst_cnt, minimum_score, 0,
				     &info, dirs_removed);
	if (want_copies)
		rename_count += find_renames(mx, dst_cnt, minimum_score, 1,
					     &info, dirs_removed);
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
			/* no need to keep unmodified pairs */
			pair_to_free = p;

		if (pair_to_free)
			pool_diff_free_filepair(pool, pair_to_free);
	}
	diff_debug_queue("done copying original", &outq);

	free(q->queue);
	*q = outq;
	diff_debug_queue("done collapsing", q);

	for (i = 0; i < rename_dst_nr; i++)
		if (rename_dst[i].filespec_to_free)
			pool_free_filespec(pool, rename_dst[i].filespec_to_free);

	cleanup_dir_rename_info(&info, dirs_removed, dir_rename_count != NULL);
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

void diffcore_rename(struct diff_options *options)
{
	diffcore_rename_extended(options, NULL, NULL, NULL, NULL, NULL);
}
