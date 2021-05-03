/*
 * "Ostensibly Recursive's Twin" merge strategy, or "ort" for short.  Meant
 * as a drop-in replacement for the "recursive" merge strategy, allowing one
 * to replace
 *
 *   git merge [-s recursive]
 *
 * with
 *
 *   git merge -s ort
 *
 * Note: git's parser allows the space between '-s' and its argument to be
 * missing.  (Should I have backronymed "ham", "alsa", "kip", "nap, "alvo",
 * "cale", "peedy", or "ins" instead of "ort"?)
 */

#include "cache.h"
#include "merge-ort.h"

#include "alloc.h"
#include "attr.h"
#include "blob.h"
#include "cache-tree.h"
#include "commit.h"
#include "commit-reach.h"
#include "diff.h"
#include "diffcore.h"
#include "dir.h"
#include "entry.h"
#include "ll-merge.h"
#include "object-store.h"
#include "revision.h"
#include "strmap.h"
#include "submodule.h"
#include "tree.h"
#include "unpack-trees.h"
#include "xdiff-interface.h"

/*
 * We have many arrays of size 3.  Whenever we have such an array, the
 * indices refer to one of the sides of the three-way merge.  This is so
 * pervasive that the constants 0, 1, and 2 are used in many places in the
 * code (especially in arithmetic operations to find the other side's index
 * or to compute a relevant mask), but sometimes these enum names are used
 * to aid code clarity.
 *
 * See also 'filemask' and 'dirmask' in struct conflict_info; the "ith side"
 * referred to there is one of these three sides.
 */
enum merge_side {
	MERGE_BASE = 0,
	MERGE_SIDE1 = 1,
	MERGE_SIDE2 = 2
};

struct traversal_callback_data {
	unsigned long mask;
	unsigned long dirmask;
	struct name_entry names[3];
};

struct rename_info {
	/*
	 * All variables that are arrays of size 3 correspond to data tracked
	 * for the sides in enum merge_side.  Index 0 is almost always unused
	 * because we often only need to track information for MERGE_SIDE1 and
	 * MERGE_SIDE2 (MERGE_BASE can't have rename information since renames
	 * are determined relative to what changed since the MERGE_BASE).
	 */

	/*
	 * pairs: pairing of filenames from diffcore_rename()
	 */
	struct diff_queue_struct pairs[3];

	/*
	 * dirs_removed: directories removed on a given side of history.
	 *
	 * The keys of dirs_removed[side] are the directories that were removed
	 * on the given side of history.  The value of the strintmap for each
	 * directory is a value from enum dir_rename_relevance.
	 */
	struct strintmap dirs_removed[3];

	/*
	 * dir_rename_count: tracking where parts of a directory were renamed to
	 *
	 * When files in a directory are renamed, they may not all go to the
	 * same location.  Each strmap here tracks:
	 *      old_dir => {new_dir => int}
	 * That is, dir_rename_count[side] is a strmap to a strintmap.
	 */
	struct strmap dir_rename_count[3];

	/*
	 * dir_renames: computed directory renames
	 *
	 * This is a map of old_dir => new_dir and is derived in part from
	 * dir_rename_count.
	 */
	struct strmap dir_renames[3];

	/*
	 * relevant_sources: deleted paths wanted in rename detection, and why
	 *
	 * relevant_sources is a set of deleted paths on each side of
	 * history for which we need rename detection.  If a path is deleted
	 * on one side of history, we need to detect if it is part of a
	 * rename if either
	 *    * the file is modified/deleted on the other side of history
	 *    * we need to detect renames for an ancestor directory
	 * If neither of those are true, we can skip rename detection for
	 * that path.  The reason is stored as a value from enum
	 * file_rename_relevance, as the reason can inform the algorithm in
	 * diffcore_rename_extended().
	 */
	struct strintmap relevant_sources[3];

	/*
	 * dir_rename_mask:
	 *   0: optimization removing unmodified potential rename source okay
	 *   2 or 4: optimization okay, but must check for files added to dir
	 *   7: optimization forbidden; need rename source in case of dir rename
	 */
	unsigned dir_rename_mask:3;

	/*
	 * callback_data_*: supporting data structures for alternate traversal
	 *
	 * We sometimes need to be able to traverse through all the files
	 * in a given tree before all immediate subdirectories within that
	 * tree.  Since traverse_trees() doesn't do that naturally, we have
	 * a traverse_trees_wrapper() that stores any immediate
	 * subdirectories while traversing files, then traverses the
	 * immediate subdirectories later.  These callback_data* variables
	 * store the information for the subdirectories so that we can do
	 * that traversal order.
	 */
	struct traversal_callback_data *callback_data;
	int callback_data_nr, callback_data_alloc;
	char *callback_data_traverse_path;

	/*
	 * needed_limit: value needed for inexact rename detection to run
	 *
	 * If the current rename limit wasn't high enough for inexact
	 * rename detection to run, this records the limit needed.  Otherwise,
	 * this value remains 0.
	 */
	int needed_limit;
};

struct merge_options_internal {
	/*
	 * paths: primary data structure in all of merge ort.
	 *
	 * The keys of paths:
	 *   * are full relative paths from the toplevel of the repository
	 *     (e.g. "drivers/firmware/raspberrypi.c").
	 *   * store all relevant paths in the repo, both directories and
	 *     files (e.g. drivers, drivers/firmware would also be included)
	 *   * these keys serve to intern all the path strings, which allows
	 *     us to do pointer comparison on directory names instead of
	 *     strcmp; we just have to be careful to use the interned strings.
	 *     (Technically paths_to_free may track some strings that were
	 *      removed from froms paths.)
	 *
	 * The values of paths:
	 *   * either a pointer to a merged_info, or a conflict_info struct
	 *   * merged_info contains all relevant information for a
	 *     non-conflicted entry.
	 *   * conflict_info contains a merged_info, plus any additional
	 *     information about a conflict such as the higher orders stages
	 *     involved and the names of the paths those came from (handy
	 *     once renames get involved).
	 *   * a path may start "conflicted" (i.e. point to a conflict_info)
	 *     and then a later step (e.g. three-way content merge) determines
	 *     it can be cleanly merged, at which point it'll be marked clean
	 *     and the algorithm will ignore any data outside the contained
	 *     merged_info for that entry
	 *   * If an entry remains conflicted, the merged_info portion of a
	 *     conflict_info will later be filled with whatever version of
	 *     the file should be placed in the working directory (e.g. an
	 *     as-merged-as-possible variation that contains conflict markers).
	 */
	struct strmap paths;

	/*
	 * conflicted: a subset of keys->values from "paths"
	 *
	 * conflicted is basically an optimization between process_entries()
	 * and record_conflicted_index_entries(); the latter could loop over
	 * ALL the entries in paths AGAIN and look for the ones that are
	 * still conflicted, but since process_entries() has to loop over
	 * all of them, it saves the ones it couldn't resolve in this strmap
	 * so that record_conflicted_index_entries() can iterate just the
	 * relevant entries.
	 */
	struct strmap conflicted;

	/*
	 * paths_to_free: additional list of strings to free
	 *
	 * If keys are removed from "paths", they are added to paths_to_free
	 * to ensure they are later freed.  We avoid free'ing immediately since
	 * other places (e.g. conflict_info.pathnames[]) may still be
	 * referencing these paths.
	 */
	struct string_list paths_to_free;

	/*
	 * output: special messages and conflict notices for various paths
	 *
	 * This is a map of pathnames (a subset of the keys in "paths" above)
	 * to strbufs.  It gathers various warning/conflict/notice messages
	 * for later processing.
	 */
	struct strmap output;

	/*
	 * renames: various data relating to rename detection
	 */
	struct rename_info renames;

	/*
	 * attr_index: hacky minimal index used for renormalization
	 *
	 * renormalization code _requires_ an index, though it only needs to
	 * find a .gitattributes file within the index.  So, when
	 * renormalization is important, we create a special index with just
	 * that one file.
	 */
	struct index_state attr_index;

	/*
	 * current_dir_name, toplevel_dir: temporary vars
	 *
	 * These are used in collect_merge_info_callback(), and will set the
	 * various merged_info.directory_name for the various paths we get;
	 * see documentation for that variable and the requirements placed on
	 * that field.
	 */
	const char *current_dir_name;
	const char *toplevel_dir;

	/* call_depth: recursion level counter for merging merge bases */
	int call_depth;
};

struct version_info {
	struct object_id oid;
	unsigned short mode;
};

struct merged_info {
	/* if is_null, ignore result.  otherwise result has oid & mode */
	struct version_info result;
	unsigned is_null:1;

	/*
	 * clean: whether the path in question is cleanly merged.
	 *
	 * see conflict_info.merged for more details.
	 */
	unsigned clean:1;

	/*
	 * basename_offset: offset of basename of path.
	 *
	 * perf optimization to avoid recomputing offset of final '/'
	 * character in pathname (0 if no '/' in pathname).
	 */
	size_t basename_offset;

	 /*
	  * directory_name: containing directory name.
	  *
	  * Note that we assume directory_name is constructed such that
	  *    strcmp(dir1_name, dir2_name) == 0 iff dir1_name == dir2_name,
	  * i.e. string equality is equivalent to pointer equality.  For this
	  * to hold, we have to be careful setting directory_name.
	  */
	const char *directory_name;
};

struct conflict_info {
	/*
	 * merged: the version of the path that will be written to working tree
	 *
	 * WARNING: It is critical to check merged.clean and ensure it is 0
	 * before reading any conflict_info fields outside of merged.
	 * Allocated merge_info structs will always have clean set to 1.
	 * Allocated conflict_info structs will have merged.clean set to 0
	 * initially.  The merged.clean field is how we know if it is safe
	 * to access other parts of conflict_info besides merged; if a
	 * conflict_info's merged.clean is changed to 1, the rest of the
	 * algorithm is not allowed to look at anything outside of the
	 * merged member anymore.
	 */
	struct merged_info merged;

	/* oids & modes from each of the three trees for this path */
	struct version_info stages[3];

	/* pathnames for each stage; may differ due to rename detection */
	const char *pathnames[3];

	/* Whether this path is/was involved in a directory/file conflict */
	unsigned df_conflict:1;

	/*
	 * Whether this path is/was involved in a non-content conflict other
	 * than a directory/file conflict (e.g. rename/rename, rename/delete,
	 * file location based on possible directory rename).
	 */
	unsigned path_conflict:1;

	/*
	 * For filemask and dirmask, the ith bit corresponds to whether the
	 * ith entry is a file (filemask) or a directory (dirmask).  Thus,
	 * filemask & dirmask is always zero, and filemask | dirmask is at
	 * most 7 but can be less when a path does not appear as either a
	 * file or a directory on at least one side of history.
	 *
	 * Note that these masks are related to enum merge_side, as the ith
	 * entry corresponds to side i.
	 *
	 * These values come from a traverse_trees() call; more info may be
	 * found looking at tree-walk.h's struct traverse_info,
	 * particularly the documentation above the "fn" member (note that
	 * filemask = mask & ~dirmask from that documentation).
	 */
	unsigned filemask:3;
	unsigned dirmask:3;

	/*
	 * Optimization to track which stages match, to avoid the need to
	 * recompute it in multiple steps. Either 0 or at least 2 bits are
	 * set; if at least 2 bits are set, their corresponding stages match.
	 */
	unsigned match_mask:3;
};

/*** Function Grouping: various utility functions ***/

/*
 * For the next three macros, see warning for conflict_info.merged.
 *
 * In each of the below, mi is a struct merged_info*, and ci was defined
 * as a struct conflict_info* (but we need to verify ci isn't actually
 * pointed at a struct merged_info*).
 *
 * INITIALIZE_CI: Assign ci to mi but only if it's safe; set to NULL otherwise.
 * VERIFY_CI: Ensure that something we assigned to a conflict_info* is one.
 * ASSIGN_AND_VERIFY_CI: Similar to VERIFY_CI but do assignment first.
 */
#define INITIALIZE_CI(ci, mi) do {                                           \
	(ci) = (!(mi) || (mi)->clean) ? NULL : (struct conflict_info *)(mi); \
} while (0)
#define VERIFY_CI(ci) assert(ci && !ci->merged.clean);
#define ASSIGN_AND_VERIFY_CI(ci, mi) do {    \
	(ci) = (struct conflict_info *)(mi);  \
	assert((ci) && !(mi)->clean);        \
} while (0)

static void free_strmap_strings(struct strmap *map)
{
	struct hashmap_iter iter;
	struct strmap_entry *entry;

	strmap_for_each_entry(map, &iter, entry) {
		free((char*)entry->key);
	}
}

static void clear_or_reinit_internal_opts(struct merge_options_internal *opti,
					  int reinitialize)
{
	struct rename_info *renames = &opti->renames;
	int i;
	void (*strmap_func)(struct strmap *, int) =
		reinitialize ? strmap_partial_clear : strmap_clear;
	void (*strintmap_func)(struct strintmap *) =
		reinitialize ? strintmap_partial_clear : strintmap_clear;

	/*
	 * We marked opti->paths with strdup_strings = 0, so that we
	 * wouldn't have to make another copy of the fullpath created by
	 * make_traverse_path from setup_path_info().  But, now that we've
	 * used it and have no other references to these strings, it is time
	 * to deallocate them.
	 */
	free_strmap_strings(&opti->paths);
	strmap_func(&opti->paths, 1);

	/*
	 * All keys and values in opti->conflicted are a subset of those in
	 * opti->paths.  We don't want to deallocate anything twice, so we
	 * don't free the keys and we pass 0 for free_values.
	 */
	strmap_func(&opti->conflicted, 0);

	/*
	 * opti->paths_to_free is similar to opti->paths; we created it with
	 * strdup_strings = 0 to avoid making _another_ copy of the fullpath
	 * but now that we've used it and have no other references to these
	 * strings, it is time to deallocate them.  We do so by temporarily
	 * setting strdup_strings to 1.
	 */
	opti->paths_to_free.strdup_strings = 1;
	string_list_clear(&opti->paths_to_free, 0);
	opti->paths_to_free.strdup_strings = 0;

	if (opti->attr_index.cache_nr) /* true iff opt->renormalize */
		discard_index(&opti->attr_index);

	/* Free memory used by various renames maps */
	for (i = MERGE_SIDE1; i <= MERGE_SIDE2; ++i) {
		strintmap_func(&renames->dirs_removed[i]);

		partial_clear_dir_rename_count(&renames->dir_rename_count[i]);
		if (!reinitialize)
			strmap_clear(&renames->dir_rename_count[i], 1);

		strmap_func(&renames->dir_renames[i], 0);

		strintmap_func(&renames->relevant_sources[i]);
	}

	if (!reinitialize) {
		struct hashmap_iter iter;
		struct strmap_entry *e;

		/* Release and free each strbuf found in output */
		strmap_for_each_entry(&opti->output, &iter, e) {
			struct strbuf *sb = e->value;
			strbuf_release(sb);
			/*
			 * While strictly speaking we don't need to free(sb)
			 * here because we could pass free_values=1 when
			 * calling strmap_clear() on opti->output, that would
			 * require strmap_clear to do another
			 * strmap_for_each_entry() loop, so we just free it
			 * while we're iterating anyway.
			 */
			free(sb);
		}
		strmap_clear(&opti->output, 0);
	}

	renames->dir_rename_mask = 0;

	/* Clean out callback_data as well. */
	FREE_AND_NULL(renames->callback_data);
	renames->callback_data_nr = renames->callback_data_alloc = 0;
}

static int err(struct merge_options *opt, const char *err, ...)
{
	va_list params;
	struct strbuf sb = STRBUF_INIT;

	strbuf_addstr(&sb, "error: ");
	va_start(params, err);
	strbuf_vaddf(&sb, err, params);
	va_end(params);

	error("%s", sb.buf);
	strbuf_release(&sb);

	return -1;
}

static void format_commit(struct strbuf *sb,
			  int indent,
			  struct commit *commit)
{
	struct merge_remote_desc *desc;
	struct pretty_print_context ctx = {0};
	ctx.abbrev = DEFAULT_ABBREV;

	strbuf_addchars(sb, ' ', indent);
	desc = merge_remote_util(commit);
	if (desc) {
		strbuf_addf(sb, "virtual %s\n", desc->name);
		return;
	}

	format_commit_message(commit, "%h %s", sb, &ctx);
	strbuf_addch(sb, '\n');
}

__attribute__((format (printf, 4, 5)))
static void path_msg(struct merge_options *opt,
		     const char *path,
		     int omittable_hint, /* skippable under --remerge-diff */
		     const char *fmt, ...)
{
	va_list ap;
	struct strbuf *sb = strmap_get(&opt->priv->output, path);
	if (!sb) {
		sb = xmalloc(sizeof(*sb));
		strbuf_init(sb, 0);
		strmap_put(&opt->priv->output, path, sb);
	}

	va_start(ap, fmt);
	strbuf_vaddf(sb, fmt, ap);
	va_end(ap);

	strbuf_addch(sb, '\n');
}

/* add a string to a strbuf, but converting "/" to "_" */
static void add_flattened_path(struct strbuf *out, const char *s)
{
	size_t i = out->len;
	strbuf_addstr(out, s);
	for (; i < out->len; i++)
		if (out->buf[i] == '/')
			out->buf[i] = '_';
}

static char *unique_path(struct strmap *existing_paths,
			 const char *path,
			 const char *branch)
{
	struct strbuf newpath = STRBUF_INIT;
	int suffix = 0;
	size_t base_len;

	strbuf_addf(&newpath, "%s~", path);
	add_flattened_path(&newpath, branch);

	base_len = newpath.len;
	while (strmap_contains(existing_paths, newpath.buf)) {
		strbuf_setlen(&newpath, base_len);
		strbuf_addf(&newpath, "_%d", suffix++);
	}

	return strbuf_detach(&newpath, NULL);
}

/*** Function Grouping: functions related to collect_merge_info() ***/

static int traverse_trees_wrapper_callback(int n,
					   unsigned long mask,
					   unsigned long dirmask,
					   struct name_entry *names,
					   struct traverse_info *info)
{
	struct merge_options *opt = info->data;
	struct rename_info *renames = &opt->priv->renames;
	unsigned filemask = mask & ~dirmask;

	assert(n==3);

	if (!renames->callback_data_traverse_path)
		renames->callback_data_traverse_path = xstrdup(info->traverse_path);

	if (filemask && filemask == renames->dir_rename_mask)
		renames->dir_rename_mask = 0x07;

	ALLOC_GROW(renames->callback_data, renames->callback_data_nr + 1,
		   renames->callback_data_alloc);
	renames->callback_data[renames->callback_data_nr].mask = mask;
	renames->callback_data[renames->callback_data_nr].dirmask = dirmask;
	COPY_ARRAY(renames->callback_data[renames->callback_data_nr].names,
		   names, 3);
	renames->callback_data_nr++;

	return mask;
}

/*
 * Much like traverse_trees(), BUT:
 *   - read all the tree entries FIRST, saving them
 *   - note that the above step provides an opportunity to compute necessary
 *     additional details before the "real" traversal
 *   - loop through the saved entries and call the original callback on them
 */
static int traverse_trees_wrapper(struct index_state *istate,
				  int n,
				  struct tree_desc *t,
				  struct traverse_info *info)
{
	int ret, i, old_offset;
	traverse_callback_t old_fn;
	char *old_callback_data_traverse_path;
	struct merge_options *opt = info->data;
	struct rename_info *renames = &opt->priv->renames;

	assert(renames->dir_rename_mask == 2 || renames->dir_rename_mask == 4);

	old_callback_data_traverse_path = renames->callback_data_traverse_path;
	old_fn = info->fn;
	old_offset = renames->callback_data_nr;

	renames->callback_data_traverse_path = NULL;
	info->fn = traverse_trees_wrapper_callback;
	ret = traverse_trees(istate, n, t, info);
	if (ret < 0)
		return ret;

	info->traverse_path = renames->callback_data_traverse_path;
	info->fn = old_fn;
	for (i = old_offset; i < renames->callback_data_nr; ++i) {
		info->fn(n,
			 renames->callback_data[i].mask,
			 renames->callback_data[i].dirmask,
			 renames->callback_data[i].names,
			 info);
	}

	renames->callback_data_nr = old_offset;
	free(renames->callback_data_traverse_path);
	renames->callback_data_traverse_path = old_callback_data_traverse_path;
	info->traverse_path = NULL;
	return 0;
}

static void setup_path_info(struct merge_options *opt,
			    struct string_list_item *result,
			    const char *current_dir_name,
			    int current_dir_name_len,
			    char *fullpath, /* we'll take over ownership */
			    struct name_entry *names,
			    struct name_entry *merged_version,
			    unsigned is_null,     /* boolean */
			    unsigned df_conflict, /* boolean */
			    unsigned filemask,
			    unsigned dirmask,
			    int resolved          /* boolean */)
{
	/* result->util is void*, so mi is a convenience typed variable */
	struct merged_info *mi;

	assert(!is_null || resolved);
	assert(!df_conflict || !resolved); /* df_conflict implies !resolved */
	assert(resolved == (merged_version != NULL));

	mi = xcalloc(1, resolved ? sizeof(struct merged_info) :
				   sizeof(struct conflict_info));
	mi->directory_name = current_dir_name;
	mi->basename_offset = current_dir_name_len;
	mi->clean = !!resolved;
	if (resolved) {
		mi->result.mode = merged_version->mode;
		oidcpy(&mi->result.oid, &merged_version->oid);
		mi->is_null = !!is_null;
	} else {
		int i;
		struct conflict_info *ci;

		ASSIGN_AND_VERIFY_CI(ci, mi);
		for (i = MERGE_BASE; i <= MERGE_SIDE2; i++) {
			ci->pathnames[i] = fullpath;
			ci->stages[i].mode = names[i].mode;
			oidcpy(&ci->stages[i].oid, &names[i].oid);
		}
		ci->filemask = filemask;
		ci->dirmask = dirmask;
		ci->df_conflict = !!df_conflict;
		if (dirmask)
			/*
			 * Assume is_null for now, but if we have entries
			 * under the directory then when it is complete in
			 * write_completed_directory() it'll update this.
			 * Also, for D/F conflicts, we have to handle the
			 * directory first, then clear this bit and process
			 * the file to see how it is handled -- that occurs
			 * near the top of process_entry().
			 */
			mi->is_null = 1;
	}
	strmap_put(&opt->priv->paths, fullpath, mi);
	result->string = fullpath;
	result->util = mi;
}

static void add_pair(struct merge_options *opt,
		     struct name_entry *names,
		     const char *pathname,
		     unsigned side,
		     unsigned is_add /* if false, is_delete */,
		     unsigned match_mask,
		     unsigned dir_rename_mask)
{
	struct diff_filespec *one, *two;
	struct rename_info *renames = &opt->priv->renames;
	int names_idx = is_add ? side : 0;

	if (!is_add) {
		unsigned content_relevant = (match_mask == 0);
		unsigned location_relevant = (dir_rename_mask == 0x07);

		if (content_relevant || location_relevant) {
			/* content_relevant trumps location_relevant */
			strintmap_set(&renames->relevant_sources[side], pathname,
				      content_relevant ? RELEVANT_CONTENT : RELEVANT_LOCATION);
		}
	}

	one = alloc_filespec(pathname);
	two = alloc_filespec(pathname);
	fill_filespec(is_add ? two : one,
		      &names[names_idx].oid, 1, names[names_idx].mode);
	diff_queue(&renames->pairs[side], one, two);
}

static void collect_rename_info(struct merge_options *opt,
				struct name_entry *names,
				const char *dirname,
				const char *fullname,
				unsigned filemask,
				unsigned dirmask,
				unsigned match_mask)
{
	struct rename_info *renames = &opt->priv->renames;
	unsigned side;

	/*
	 * Update dir_rename_mask (determines ignore-rename-source validity)
	 *
	 * dir_rename_mask helps us keep track of when directory rename
	 * detection may be relevant.  Basically, whenver a directory is
	 * removed on one side of history, and a file is added to that
	 * directory on the other side of history, directory rename
	 * detection is relevant (meaning we have to detect renames for all
	 * files within that directory to deduce where the directory
	 * moved).  Also, whenever a directory needs directory rename
	 * detection, due to the "majority rules" choice for where to move
	 * it (see t6423 testcase 1f), we also need to detect renames for
	 * all files within subdirectories of that directory as well.
	 *
	 * Here we haven't looked at files within the directory yet, we are
	 * just looking at the directory itself.  So, if we aren't yet in
	 * a case where a parent directory needed directory rename detection
	 * (i.e. dir_rename_mask != 0x07), and if the directory was removed
	 * on one side of history, record the mask of the other side of
	 * history in dir_rename_mask.
	 */
	if (renames->dir_rename_mask != 0x07 &&
	    (dirmask == 3 || dirmask == 5)) {
		/* simple sanity check */
		assert(renames->dir_rename_mask == 0 ||
		       renames->dir_rename_mask == (dirmask & ~1));
		/* update dir_rename_mask; have it record mask of new side */
		renames->dir_rename_mask = (dirmask & ~1);
	}

	/* Update dirs_removed, as needed */
	if (dirmask == 1 || dirmask == 3 || dirmask == 5) {
		/* absent_mask = 0x07 - dirmask; sides = absent_mask/2 */
		unsigned sides = (0x07 - dirmask)/2;
		unsigned relevance = (renames->dir_rename_mask == 0x07) ?
					RELEVANT_FOR_ANCESTOR : NOT_RELEVANT;
		/*
		 * Record relevance of this directory.  However, note that
		 * when collect_merge_info_callback() recurses into this
		 * directory and calls collect_rename_info() on paths
		 * within that directory, if we find a path that was added
		 * to this directory on the other side of history, we will
		 * upgrade this value to RELEVANT_FOR_SELF; see below.
		 */
		if (sides & 1)
			strintmap_set(&renames->dirs_removed[1], fullname,
				      relevance);
		if (sides & 2)
			strintmap_set(&renames->dirs_removed[2], fullname,
				      relevance);
	}

	/*
	 * Here's the block that potentially upgrades to RELEVANT_FOR_SELF.
	 * When we run across a file added to a directory.  In such a case,
	 * find the directory of the file and upgrade its relevance.
	 */
	if (renames->dir_rename_mask == 0x07 &&
	    (filemask == 2 || filemask == 4)) {
		/*
		 * Need directory rename for parent directory on other side
		 * of history from added file.  Thus
		 *    side = (~filemask & 0x06) >> 1
		 * or
		 *    side = 3 - (filemask/2).
		 */
		unsigned side = 3 - (filemask >> 1);
		strintmap_set(&renames->dirs_removed[side], dirname,
			      RELEVANT_FOR_SELF);
	}

	if (filemask == 0 || filemask == 7)
		return;

	for (side = MERGE_SIDE1; side <= MERGE_SIDE2; ++side) {
		unsigned side_mask = (1 << side);

		/* Check for deletion on side */
		if ((filemask & 1) && !(filemask & side_mask))
			add_pair(opt, names, fullname, side, 0 /* delete */,
				 match_mask & filemask,
				 renames->dir_rename_mask);

		/* Check for addition on side */
		if (!(filemask & 1) && (filemask & side_mask))
			add_pair(opt, names, fullname, side, 1 /* add */,
				 match_mask & filemask,
				 renames->dir_rename_mask);
	}
}

static int collect_merge_info_callback(int n,
				       unsigned long mask,
				       unsigned long dirmask,
				       struct name_entry *names,
				       struct traverse_info *info)
{
	/*
	 * n is 3.  Always.
	 * common ancestor (mbase) has mask 1, and stored in index 0 of names
	 * head of side 1  (side1) has mask 2, and stored in index 1 of names
	 * head of side 2  (side2) has mask 4, and stored in index 2 of names
	 */
	struct merge_options *opt = info->data;
	struct merge_options_internal *opti = opt->priv;
	struct rename_info *renames = &opt->priv->renames;
	struct string_list_item pi;  /* Path Info */
	struct conflict_info *ci; /* typed alias to pi.util (which is void*) */
	struct name_entry *p;
	size_t len;
	char *fullpath;
	const char *dirname = opti->current_dir_name;
	unsigned prev_dir_rename_mask = renames->dir_rename_mask;
	unsigned filemask = mask & ~dirmask;
	unsigned match_mask = 0; /* will be updated below */
	unsigned mbase_null = !(mask & 1);
	unsigned side1_null = !(mask & 2);
	unsigned side2_null = !(mask & 4);
	unsigned side1_matches_mbase = (!side1_null && !mbase_null &&
					names[0].mode == names[1].mode &&
					oideq(&names[0].oid, &names[1].oid));
	unsigned side2_matches_mbase = (!side2_null && !mbase_null &&
					names[0].mode == names[2].mode &&
					oideq(&names[0].oid, &names[2].oid));
	unsigned sides_match = (!side1_null && !side2_null &&
				names[1].mode == names[2].mode &&
				oideq(&names[1].oid, &names[2].oid));

	/*
	 * Note: When a path is a file on one side of history and a directory
	 * in another, we have a directory/file conflict.  In such cases, if
	 * the conflict doesn't resolve from renames and deletions, then we
	 * always leave directories where they are and move files out of the
	 * way.  Thus, while struct conflict_info has a df_conflict field to
	 * track such conflicts, we ignore that field for any directories at
	 * a path and only pay attention to it for files at the given path.
	 * The fact that we leave directories were they are also means that
	 * we do not need to worry about getting additional df_conflict
	 * information propagated from parent directories down to children
	 * (unlike, say traverse_trees_recursive() in unpack-trees.c, which
	 * sets a newinfo.df_conflicts field specifically to propagate it).
	 */
	unsigned df_conflict = (filemask != 0) && (dirmask != 0);

	/* n = 3 is a fundamental assumption. */
	if (n != 3)
		BUG("Called collect_merge_info_callback wrong");

	/*
	 * A bunch of sanity checks verifying that traverse_trees() calls
	 * us the way I expect.  Could just remove these at some point,
	 * though maybe they are helpful to future code readers.
	 */
	assert(mbase_null == is_null_oid(&names[0].oid));
	assert(side1_null == is_null_oid(&names[1].oid));
	assert(side2_null == is_null_oid(&names[2].oid));
	assert(!mbase_null || !side1_null || !side2_null);
	assert(mask > 0 && mask < 8);

	/* Determine match_mask */
	if (side1_matches_mbase)
		match_mask = (side2_matches_mbase ? 7 : 3);
	else if (side2_matches_mbase)
		match_mask = 5;
	else if (sides_match)
		match_mask = 6;

	/*
	 * Get the name of the relevant filepath, which we'll pass to
	 * setup_path_info() for tracking.
	 */
	p = names;
	while (!p->mode)
		p++;
	len = traverse_path_len(info, p->pathlen);

	/* +1 in both of the following lines to include the NUL byte */
	fullpath = xmalloc(len + 1);
	make_traverse_path(fullpath, len + 1, info, p->path, p->pathlen);

	/*
	 * If mbase, side1, and side2 all match, we can resolve early.  Even
	 * if these are trees, there will be no renames or anything
	 * underneath.
	 */
	if (side1_matches_mbase && side2_matches_mbase) {
		/* mbase, side1, & side2 all match; use mbase as resolution */
		setup_path_info(opt, &pi, dirname, info->pathlen, fullpath,
				names, names+0, mbase_null, 0,
				filemask, dirmask, 1);
		return mask;
	}

	/*
	 * Gather additional information used in rename detection.
	 */
	collect_rename_info(opt, names, dirname, fullpath,
			    filemask, dirmask, match_mask);

	/*
	 * Record information about the path so we can resolve later in
	 * process_entries.
	 */
	setup_path_info(opt, &pi, dirname, info->pathlen, fullpath,
			names, NULL, 0, df_conflict, filemask, dirmask, 0);

	ci = pi.util;
	VERIFY_CI(ci);
	ci->match_mask = match_mask;

	/* If dirmask, recurse into subdirectories */
	if (dirmask) {
		struct traverse_info newinfo;
		struct tree_desc t[3];
		void *buf[3] = {NULL, NULL, NULL};
		const char *original_dir_name;
		int i, ret;

		ci->match_mask &= filemask;
		newinfo = *info;
		newinfo.prev = info;
		newinfo.name = p->path;
		newinfo.namelen = p->pathlen;
		newinfo.pathlen = st_add3(newinfo.pathlen, p->pathlen, 1);
		/*
		 * If this directory we are about to recurse into cared about
		 * its parent directory (the current directory) having a D/F
		 * conflict, then we'd propagate the masks in this way:
		 *    newinfo.df_conflicts |= (mask & ~dirmask);
		 * But we don't worry about propagating D/F conflicts.  (See
		 * comment near setting of local df_conflict variable near
		 * the beginning of this function).
		 */

		for (i = MERGE_BASE; i <= MERGE_SIDE2; i++) {
			if (i == 1 && side1_matches_mbase)
				t[1] = t[0];
			else if (i == 2 && side2_matches_mbase)
				t[2] = t[0];
			else if (i == 2 && sides_match)
				t[2] = t[1];
			else {
				const struct object_id *oid = NULL;
				if (dirmask & 1)
					oid = &names[i].oid;
				buf[i] = fill_tree_descriptor(opt->repo,
							      t + i, oid);
			}
			dirmask >>= 1;
		}

		original_dir_name = opti->current_dir_name;
		opti->current_dir_name = pi.string;
		if (renames->dir_rename_mask == 0 ||
		    renames->dir_rename_mask == 0x07)
			ret = traverse_trees(NULL, 3, t, &newinfo);
		else
			ret = traverse_trees_wrapper(NULL, 3, t, &newinfo);
		opti->current_dir_name = original_dir_name;
		renames->dir_rename_mask = prev_dir_rename_mask;

		for (i = MERGE_BASE; i <= MERGE_SIDE2; i++)
			free(buf[i]);

		if (ret < 0)
			return -1;
	}

	return mask;
}

static int collect_merge_info(struct merge_options *opt,
			      struct tree *merge_base,
			      struct tree *side1,
			      struct tree *side2)
{
	int ret;
	struct tree_desc t[3];
	struct traverse_info info;

	opt->priv->toplevel_dir = "";
	opt->priv->current_dir_name = opt->priv->toplevel_dir;
	setup_traverse_info(&info, opt->priv->toplevel_dir);
	info.fn = collect_merge_info_callback;
	info.data = opt;
	info.show_all_errors = 1;

	parse_tree(merge_base);
	parse_tree(side1);
	parse_tree(side2);
	init_tree_desc(t + 0, merge_base->buffer, merge_base->size);
	init_tree_desc(t + 1, side1->buffer, side1->size);
	init_tree_desc(t + 2, side2->buffer, side2->size);

	trace2_region_enter("merge", "traverse_trees", opt->repo);
	ret = traverse_trees(NULL, 3, t, &info);
	trace2_region_leave("merge", "traverse_trees", opt->repo);

	return ret;
}

/*** Function Grouping: functions related to threeway content merges ***/

static int find_first_merges(struct repository *repo,
			     const char *path,
			     struct commit *a,
			     struct commit *b,
			     struct object_array *result)
{
	int i, j;
	struct object_array merges = OBJECT_ARRAY_INIT;
	struct commit *commit;
	int contains_another;

	char merged_revision[GIT_MAX_HEXSZ + 2];
	const char *rev_args[] = { "rev-list", "--merges", "--ancestry-path",
				   "--all", merged_revision, NULL };
	struct rev_info revs;
	struct setup_revision_opt rev_opts;

	memset(result, 0, sizeof(struct object_array));
	memset(&rev_opts, 0, sizeof(rev_opts));

	/* get all revisions that merge commit a */
	xsnprintf(merged_revision, sizeof(merged_revision), "^%s",
		  oid_to_hex(&a->object.oid));
	repo_init_revisions(repo, &revs, NULL);
	rev_opts.submodule = path;
	/* FIXME: can't handle linked worktrees in submodules yet */
	revs.single_worktree = path != NULL;
	setup_revisions(ARRAY_SIZE(rev_args)-1, rev_args, &revs, &rev_opts);

	/* save all revisions from the above list that contain b */
	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	while ((commit = get_revision(&revs)) != NULL) {
		struct object *o = &(commit->object);
		if (in_merge_bases(b, commit))
			add_object_array(o, NULL, &merges);
	}
	reset_revision_walk();

	/* Now we've got all merges that contain a and b. Prune all
	 * merges that contain another found merge and save them in
	 * result.
	 */
	for (i = 0; i < merges.nr; i++) {
		struct commit *m1 = (struct commit *) merges.objects[i].item;

		contains_another = 0;
		for (j = 0; j < merges.nr; j++) {
			struct commit *m2 = (struct commit *) merges.objects[j].item;
			if (i != j && in_merge_bases(m2, m1)) {
				contains_another = 1;
				break;
			}
		}

		if (!contains_another)
			add_object_array(merges.objects[i].item, NULL, result);
	}

	object_array_clear(&merges);
	return result->nr;
}

static int merge_submodule(struct merge_options *opt,
			   const char *path,
			   const struct object_id *o,
			   const struct object_id *a,
			   const struct object_id *b,
			   struct object_id *result)
{
	struct commit *commit_o, *commit_a, *commit_b;
	int parent_count;
	struct object_array merges;
	struct strbuf sb = STRBUF_INIT;

	int i;
	int search = !opt->priv->call_depth;

	/* store fallback answer in result in case we fail */
	oidcpy(result, opt->priv->call_depth ? o : a);

	/* we can not handle deletion conflicts */
	if (is_null_oid(o))
		return 0;
	if (is_null_oid(a))
		return 0;
	if (is_null_oid(b))
		return 0;

	if (add_submodule_odb(path)) {
		path_msg(opt, path, 0,
			 _("Failed to merge submodule %s (not checked out)"),
			 path);
		return 0;
	}

	if (!(commit_o = lookup_commit_reference(opt->repo, o)) ||
	    !(commit_a = lookup_commit_reference(opt->repo, a)) ||
	    !(commit_b = lookup_commit_reference(opt->repo, b))) {
		path_msg(opt, path, 0,
			 _("Failed to merge submodule %s (commits not present)"),
			 path);
		return 0;
	}

	/* check whether both changes are forward */
	if (!in_merge_bases(commit_o, commit_a) ||
	    !in_merge_bases(commit_o, commit_b)) {
		path_msg(opt, path, 0,
			 _("Failed to merge submodule %s "
			   "(commits don't follow merge-base)"),
			 path);
		return 0;
	}

	/* Case #1: a is contained in b or vice versa */
	if (in_merge_bases(commit_a, commit_b)) {
		oidcpy(result, b);
		path_msg(opt, path, 1,
			 _("Note: Fast-forwarding submodule %s to %s"),
			 path, oid_to_hex(b));
		return 1;
	}
	if (in_merge_bases(commit_b, commit_a)) {
		oidcpy(result, a);
		path_msg(opt, path, 1,
			 _("Note: Fast-forwarding submodule %s to %s"),
			 path, oid_to_hex(a));
		return 1;
	}

	/*
	 * Case #2: There are one or more merges that contain a and b in
	 * the submodule. If there is only one, then present it as a
	 * suggestion to the user, but leave it marked unmerged so the
	 * user needs to confirm the resolution.
	 */

	/* Skip the search if makes no sense to the calling context.  */
	if (!search)
		return 0;

	/* find commit which merges them */
	parent_count = find_first_merges(opt->repo, path, commit_a, commit_b,
					 &merges);
	switch (parent_count) {
	case 0:
		path_msg(opt, path, 0, _("Failed to merge submodule %s"), path);
		break;

	case 1:
		format_commit(&sb, 4,
			      (struct commit *)merges.objects[0].item);
		path_msg(opt, path, 0,
			 _("Failed to merge submodule %s, but a possible merge "
			   "resolution exists:\n%s\n"),
			 path, sb.buf);
		path_msg(opt, path, 1,
			 _("If this is correct simply add it to the index "
			   "for example\n"
			   "by using:\n\n"
			   "  git update-index --cacheinfo 160000 %s \"%s\"\n\n"
			   "which will accept this suggestion.\n"),
			 oid_to_hex(&merges.objects[0].item->oid), path);
		strbuf_release(&sb);
		break;
	default:
		for (i = 0; i < merges.nr; i++)
			format_commit(&sb, 4,
				      (struct commit *)merges.objects[i].item);
		path_msg(opt, path, 0,
			 _("Failed to merge submodule %s, but multiple "
			   "possible merges exist:\n%s"), path, sb.buf);
		strbuf_release(&sb);
	}

	object_array_clear(&merges);
	return 0;
}

static void initialize_attr_index(struct merge_options *opt)
{
	/*
	 * The renormalize_buffer() functions require attributes, and
	 * annoyingly those can only be read from the working tree or from
	 * an index_state.  merge-ort doesn't have an index_state, so we
	 * generate a fake one containing only attribute information.
	 */
	struct merged_info *mi;
	struct index_state *attr_index = &opt->priv->attr_index;
	struct cache_entry *ce;

	attr_index->initialized = 1;

	if (!opt->renormalize)
		return;

	mi = strmap_get(&opt->priv->paths, GITATTRIBUTES_FILE);
	if (!mi)
		return;

	if (mi->clean) {
		int len = strlen(GITATTRIBUTES_FILE);
		ce = make_empty_cache_entry(attr_index, len);
		ce->ce_mode = create_ce_mode(mi->result.mode);
		ce->ce_flags = create_ce_flags(0);
		ce->ce_namelen = len;
		oidcpy(&ce->oid, &mi->result.oid);
		memcpy(ce->name, GITATTRIBUTES_FILE, len);
		add_index_entry(attr_index, ce,
				ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE);
		get_stream_filter(attr_index, GITATTRIBUTES_FILE, &ce->oid);
	} else {
		int stage, len;
		struct conflict_info *ci;

		ASSIGN_AND_VERIFY_CI(ci, mi);
		for (stage = 0; stage < 3; stage++) {
			unsigned stage_mask = (1 << stage);

			if (!(ci->filemask & stage_mask))
				continue;
			len = strlen(GITATTRIBUTES_FILE);
			ce = make_empty_cache_entry(attr_index, len);
			ce->ce_mode = create_ce_mode(ci->stages[stage].mode);
			ce->ce_flags = create_ce_flags(stage);
			ce->ce_namelen = len;
			oidcpy(&ce->oid, &ci->stages[stage].oid);
			memcpy(ce->name, GITATTRIBUTES_FILE, len);
			add_index_entry(attr_index, ce,
					ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE);
			get_stream_filter(attr_index, GITATTRIBUTES_FILE,
					  &ce->oid);
		}
	}
}

static int merge_3way(struct merge_options *opt,
		      const char *path,
		      const struct object_id *o,
		      const struct object_id *a,
		      const struct object_id *b,
		      const char *pathnames[3],
		      const int extra_marker_size,
		      mmbuffer_t *result_buf)
{
	mmfile_t orig, src1, src2;
	struct ll_merge_options ll_opts = {0};
	char *base, *name1, *name2;
	int merge_status;

	if (!opt->priv->attr_index.initialized)
		initialize_attr_index(opt);

	ll_opts.renormalize = opt->renormalize;
	ll_opts.extra_marker_size = extra_marker_size;
	ll_opts.xdl_opts = opt->xdl_opts;

	if (opt->priv->call_depth) {
		ll_opts.virtual_ancestor = 1;
		ll_opts.variant = 0;
	} else {
		switch (opt->recursive_variant) {
		case MERGE_VARIANT_OURS:
			ll_opts.variant = XDL_MERGE_FAVOR_OURS;
			break;
		case MERGE_VARIANT_THEIRS:
			ll_opts.variant = XDL_MERGE_FAVOR_THEIRS;
			break;
		default:
			ll_opts.variant = 0;
			break;
		}
	}

	assert(pathnames[0] && pathnames[1] && pathnames[2] && opt->ancestor);
	if (pathnames[0] == pathnames[1] && pathnames[1] == pathnames[2]) {
		base  = mkpathdup("%s", opt->ancestor);
		name1 = mkpathdup("%s", opt->branch1);
		name2 = mkpathdup("%s", opt->branch2);
	} else {
		base  = mkpathdup("%s:%s", opt->ancestor, pathnames[0]);
		name1 = mkpathdup("%s:%s", opt->branch1,  pathnames[1]);
		name2 = mkpathdup("%s:%s", opt->branch2,  pathnames[2]);
	}

	read_mmblob(&orig, o);
	read_mmblob(&src1, a);
	read_mmblob(&src2, b);

	merge_status = ll_merge(result_buf, path, &orig, base,
				&src1, name1, &src2, name2,
				&opt->priv->attr_index, &ll_opts);

	free(base);
	free(name1);
	free(name2);
	free(orig.ptr);
	free(src1.ptr);
	free(src2.ptr);
	return merge_status;
}

static int handle_content_merge(struct merge_options *opt,
				const char *path,
				const struct version_info *o,
				const struct version_info *a,
				const struct version_info *b,
				const char *pathnames[3],
				const int extra_marker_size,
				struct version_info *result)
{
	/*
	 * path is the target location where we want to put the file, and
	 * is used to determine any normalization rules in ll_merge.
	 *
	 * The normal case is that path and all entries in pathnames are
	 * identical, though renames can affect which path we got one of
	 * the three blobs to merge on various sides of history.
	 *
	 * extra_marker_size is the amount to extend conflict markers in
	 * ll_merge; this is neeed if we have content merges of content
	 * merges, which happens for example with rename/rename(2to1) and
	 * rename/add conflicts.
	 */
	unsigned clean = 1;

	/*
	 * handle_content_merge() needs both files to be of the same type, i.e.
	 * both files OR both submodules OR both symlinks.  Conflicting types
	 * needs to be handled elsewhere.
	 */
	assert((S_IFMT & a->mode) == (S_IFMT & b->mode));

	/* Merge modes */
	if (a->mode == b->mode || a->mode == o->mode)
		result->mode = b->mode;
	else {
		/* must be the 100644/100755 case */
		assert(S_ISREG(a->mode));
		result->mode = a->mode;
		clean = (b->mode == o->mode);
		/*
		 * FIXME: If opt->priv->call_depth && !clean, then we really
		 * should not make result->mode match either a->mode or
		 * b->mode; that causes t6036 "check conflicting mode for
		 * regular file" to fail.  It would be best to use some other
		 * mode, but we'll confuse all kinds of stuff if we use one
		 * where S_ISREG(result->mode) isn't true, and if we use
		 * something like 0100666, then tree-walk.c's calls to
		 * canon_mode() will just normalize that to 100644 for us and
		 * thus not solve anything.
		 *
		 * Figure out if there's some kind of way we can work around
		 * this...
		 */
	}

	/*
	 * Trivial oid merge.
	 *
	 * Note: While one might assume that the next four lines would
	 * be unnecessary due to the fact that match_mask is often
	 * setup and already handled, renames don't always take care
	 * of that.
	 */
	if (oideq(&a->oid, &b->oid) || oideq(&a->oid, &o->oid))
		oidcpy(&result->oid, &b->oid);
	else if (oideq(&b->oid, &o->oid))
		oidcpy(&result->oid, &a->oid);

	/* Remaining rules depend on file vs. submodule vs. symlink. */
	else if (S_ISREG(a->mode)) {
		mmbuffer_t result_buf;
		int ret = 0, merge_status;
		int two_way;

		/*
		 * If 'o' is different type, treat it as null so we do a
		 * two-way merge.
		 */
		two_way = ((S_IFMT & o->mode) != (S_IFMT & a->mode));

		merge_status = merge_3way(opt, path,
					  two_way ? null_oid() : &o->oid,
					  &a->oid, &b->oid,
					  pathnames, extra_marker_size,
					  &result_buf);

		if ((merge_status < 0) || !result_buf.ptr)
			ret = err(opt, _("Failed to execute internal merge"));

		if (!ret &&
		    write_object_file(result_buf.ptr, result_buf.size,
				      blob_type, &result->oid))
			ret = err(opt, _("Unable to add %s to database"),
				  path);

		free(result_buf.ptr);
		if (ret)
			return -1;
		clean &= (merge_status == 0);
		path_msg(opt, path, 1, _("Auto-merging %s"), path);
	} else if (S_ISGITLINK(a->mode)) {
		int two_way = ((S_IFMT & o->mode) != (S_IFMT & a->mode));
		clean = merge_submodule(opt, pathnames[0],
					two_way ? null_oid() : &o->oid,
					&a->oid, &b->oid, &result->oid);
		if (opt->priv->call_depth && two_way && !clean) {
			result->mode = o->mode;
			oidcpy(&result->oid, &o->oid);
		}
	} else if (S_ISLNK(a->mode)) {
		if (opt->priv->call_depth) {
			clean = 0;
			result->mode = o->mode;
			oidcpy(&result->oid, &o->oid);
		} else {
			switch (opt->recursive_variant) {
			case MERGE_VARIANT_NORMAL:
				clean = 0;
				oidcpy(&result->oid, &a->oid);
				break;
			case MERGE_VARIANT_OURS:
				oidcpy(&result->oid, &a->oid);
				break;
			case MERGE_VARIANT_THEIRS:
				oidcpy(&result->oid, &b->oid);
				break;
			}
		}
	} else
		BUG("unsupported object type in the tree: %06o for %s",
		    a->mode, path);

	return clean;
}

/*** Function Grouping: functions related to detect_and_process_renames(), ***
 *** which are split into directory and regular rename detection sections. ***/

/*** Function Grouping: functions related to directory rename detection ***/

struct collision_info {
	struct string_list source_files;
	unsigned reported_already:1;
};

/*
 * Return a new string that replaces the beginning portion (which matches
 * rename_info->key), with rename_info->util.new_dir.  In perl-speak:
 *   new_path_name = (old_path =~ s/rename_info->key/rename_info->value/);
 * NOTE:
 *   Caller must ensure that old_path starts with rename_info->key + '/'.
 */
static char *apply_dir_rename(struct strmap_entry *rename_info,
			      const char *old_path)
{
	struct strbuf new_path = STRBUF_INIT;
	const char *old_dir = rename_info->key;
	const char *new_dir = rename_info->value;
	int oldlen, newlen, new_dir_len;

	oldlen = strlen(old_dir);
	if (*new_dir == '\0')
		/*
		 * If someone renamed/merged a subdirectory into the root
		 * directory (e.g. 'some/subdir' -> ''), then we want to
		 * avoid returning
		 *     '' + '/filename'
		 * as the rename; we need to make old_path + oldlen advance
		 * past the '/' character.
		 */
		oldlen++;
	new_dir_len = strlen(new_dir);
	newlen = new_dir_len + (strlen(old_path) - oldlen) + 1;
	strbuf_grow(&new_path, newlen);
	strbuf_add(&new_path, new_dir, new_dir_len);
	strbuf_addstr(&new_path, &old_path[oldlen]);

	return strbuf_detach(&new_path, NULL);
}

static int path_in_way(struct strmap *paths, const char *path, unsigned side_mask)
{
	struct merged_info *mi = strmap_get(paths, path);
	struct conflict_info *ci;
	if (!mi)
		return 0;
	INITIALIZE_CI(ci, mi);
	return mi->clean || (side_mask & (ci->filemask | ci->dirmask));
}

/*
 * See if there is a directory rename for path, and if there are any file
 * level conflicts on the given side for the renamed location.  If there is
 * a rename and there are no conflicts, return the new name.  Otherwise,
 * return NULL.
 */
static char *handle_path_level_conflicts(struct merge_options *opt,
					 const char *path,
					 unsigned side_index,
					 struct strmap_entry *rename_info,
					 struct strmap *collisions)
{
	char *new_path = NULL;
	struct collision_info *c_info;
	int clean = 1;
	struct strbuf collision_paths = STRBUF_INIT;

	/*
	 * entry has the mapping of old directory name to new directory name
	 * that we want to apply to path.
	 */
	new_path = apply_dir_rename(rename_info, path);
	if (!new_path)
		BUG("Failed to apply directory rename!");

	/*
	 * The caller needs to have ensured that it has pre-populated
	 * collisions with all paths that map to new_path.  Do a quick check
	 * to ensure that's the case.
	 */
	c_info = strmap_get(collisions, new_path);
	if (c_info == NULL)
		BUG("c_info is NULL");

	/*
	 * Check for one-sided add/add/.../add conflicts, i.e.
	 * where implicit renames from the other side doing
	 * directory rename(s) can affect this side of history
	 * to put multiple paths into the same location.  Warn
	 * and bail on directory renames for such paths.
	 */
	if (c_info->reported_already) {
		clean = 0;
	} else if (path_in_way(&opt->priv->paths, new_path, 1 << side_index)) {
		c_info->reported_already = 1;
		strbuf_add_separated_string_list(&collision_paths, ", ",
						 &c_info->source_files);
		path_msg(opt, new_path, 0,
			 _("CONFLICT (implicit dir rename): Existing file/dir "
			   "at %s in the way of implicit directory rename(s) "
			   "putting the following path(s) there: %s."),
		       new_path, collision_paths.buf);
		clean = 0;
	} else if (c_info->source_files.nr > 1) {
		c_info->reported_already = 1;
		strbuf_add_separated_string_list(&collision_paths, ", ",
						 &c_info->source_files);
		path_msg(opt, new_path, 0,
			 _("CONFLICT (implicit dir rename): Cannot map more "
			   "than one path to %s; implicit directory renames "
			   "tried to put these paths there: %s"),
		       new_path, collision_paths.buf);
		clean = 0;
	}

	/* Free memory we no longer need */
	strbuf_release(&collision_paths);
	if (!clean && new_path) {
		free(new_path);
		return NULL;
	}

	return new_path;
}

static void get_provisional_directory_renames(struct merge_options *opt,
					      unsigned side,
					      int *clean)
{
	struct hashmap_iter iter;
	struct strmap_entry *entry;
	struct rename_info *renames = &opt->priv->renames;

	/*
	 * Collapse
	 *    dir_rename_count: old_directory -> {new_directory -> count}
	 * down to
	 *    dir_renames: old_directory -> best_new_directory
	 * where best_new_directory is the one with the unique highest count.
	 */
	strmap_for_each_entry(&renames->dir_rename_count[side], &iter, entry) {
		const char *source_dir = entry->key;
		struct strintmap *counts = entry->value;
		struct hashmap_iter count_iter;
		struct strmap_entry *count_entry;
		int max = 0;
		int bad_max = 0;
		const char *best = NULL;

		strintmap_for_each_entry(counts, &count_iter, count_entry) {
			const char *target_dir = count_entry->key;
			intptr_t count = (intptr_t)count_entry->value;

			if (count == max)
				bad_max = max;
			else if (count > max) {
				max = count;
				best = target_dir;
			}
		}

		if (max == 0)
			continue;

		if (bad_max == max) {
			path_msg(opt, source_dir, 0,
			       _("CONFLICT (directory rename split): "
				 "Unclear where to rename %s to; it was "
				 "renamed to multiple other directories, with "
				 "no destination getting a majority of the "
				 "files."),
			       source_dir);
			*clean = 0;
		} else {
			strmap_put(&renames->dir_renames[side],
				   source_dir, (void*)best);
		}
	}
}

static void handle_directory_level_conflicts(struct merge_options *opt)
{
	struct hashmap_iter iter;
	struct strmap_entry *entry;
	struct string_list duplicated = STRING_LIST_INIT_NODUP;
	struct rename_info *renames = &opt->priv->renames;
	struct strmap *side1_dir_renames = &renames->dir_renames[MERGE_SIDE1];
	struct strmap *side2_dir_renames = &renames->dir_renames[MERGE_SIDE2];
	int i;

	strmap_for_each_entry(side1_dir_renames, &iter, entry) {
		if (strmap_contains(side2_dir_renames, entry->key))
			string_list_append(&duplicated, entry->key);
	}

	for (i = 0; i < duplicated.nr; i++) {
		strmap_remove(side1_dir_renames, duplicated.items[i].string, 0);
		strmap_remove(side2_dir_renames, duplicated.items[i].string, 0);
	}
	string_list_clear(&duplicated, 0);
}

static struct strmap_entry *check_dir_renamed(const char *path,
					      struct strmap *dir_renames)
{
	char *temp = xstrdup(path);
	char *end;
	struct strmap_entry *e = NULL;

	while ((end = strrchr(temp, '/'))) {
		*end = '\0';
		e = strmap_get_entry(dir_renames, temp);
		if (e)
			break;
	}
	free(temp);
	return e;
}

static void compute_collisions(struct strmap *collisions,
			       struct strmap *dir_renames,
			       struct diff_queue_struct *pairs)
{
	int i;

	strmap_init_with_options(collisions, NULL, 0);
	if (strmap_empty(dir_renames))
		return;

	/*
	 * Multiple files can be mapped to the same path due to directory
	 * renames done by the other side of history.  Since that other
	 * side of history could have merged multiple directories into one,
	 * if our side of history added the same file basename to each of
	 * those directories, then all N of them would get implicitly
	 * renamed by the directory rename detection into the same path,
	 * and we'd get an add/add/.../add conflict, and all those adds
	 * from *this* side of history.  This is not representable in the
	 * index, and users aren't going to easily be able to make sense of
	 * it.  So we need to provide a good warning about what's
	 * happening, and fall back to no-directory-rename detection
	 * behavior for those paths.
	 *
	 * See testcases 9e and all of section 5 from t6043 for examples.
	 */
	for (i = 0; i < pairs->nr; ++i) {
		struct strmap_entry *rename_info;
		struct collision_info *collision_info;
		char *new_path;
		struct diff_filepair *pair = pairs->queue[i];

		if (pair->status != 'A' && pair->status != 'R')
			continue;
		rename_info = check_dir_renamed(pair->two->path, dir_renames);
		if (!rename_info)
			continue;

		new_path = apply_dir_rename(rename_info, pair->two->path);
		assert(new_path);
		collision_info = strmap_get(collisions, new_path);
		if (collision_info) {
			free(new_path);
		} else {
			CALLOC_ARRAY(collision_info, 1);
			string_list_init(&collision_info->source_files, 0);
			strmap_put(collisions, new_path, collision_info);
		}
		string_list_insert(&collision_info->source_files,
				   pair->two->path);
	}
}

static char *check_for_directory_rename(struct merge_options *opt,
					const char *path,
					unsigned side_index,
					struct strmap *dir_renames,
					struct strmap *dir_rename_exclusions,
					struct strmap *collisions,
					int *clean_merge)
{
	char *new_path = NULL;
	struct strmap_entry *rename_info;
	struct strmap_entry *otherinfo = NULL;
	const char *new_dir;

	if (strmap_empty(dir_renames))
		return new_path;
	rename_info = check_dir_renamed(path, dir_renames);
	if (!rename_info)
		return new_path;
	/* old_dir = rename_info->key; */
	new_dir = rename_info->value;

	/*
	 * This next part is a little weird.  We do not want to do an
	 * implicit rename into a directory we renamed on our side, because
	 * that will result in a spurious rename/rename(1to2) conflict.  An
	 * example:
	 *   Base commit: dumbdir/afile, otherdir/bfile
	 *   Side 1:      smrtdir/afile, otherdir/bfile
	 *   Side 2:      dumbdir/afile, dumbdir/bfile
	 * Here, while working on Side 1, we could notice that otherdir was
	 * renamed/merged to dumbdir, and change the diff_filepair for
	 * otherdir/bfile into a rename into dumbdir/bfile.  However, Side
	 * 2 will notice the rename from dumbdir to smrtdir, and do the
	 * transitive rename to move it from dumbdir/bfile to
	 * smrtdir/bfile.  That gives us bfile in dumbdir vs being in
	 * smrtdir, a rename/rename(1to2) conflict.  We really just want
	 * the file to end up in smrtdir.  And the way to achieve that is
	 * to not let Side1 do the rename to dumbdir, since we know that is
	 * the source of one of our directory renames.
	 *
	 * That's why otherinfo and dir_rename_exclusions is here.
	 *
	 * As it turns out, this also prevents N-way transient rename
	 * confusion; See testcases 9c and 9d of t6043.
	 */
	otherinfo = strmap_get_entry(dir_rename_exclusions, new_dir);
	if (otherinfo) {
		path_msg(opt, rename_info->key, 1,
			 _("WARNING: Avoiding applying %s -> %s rename "
			   "to %s, because %s itself was renamed."),
			 rename_info->key, new_dir, path, new_dir);
		return NULL;
	}

	new_path = handle_path_level_conflicts(opt, path, side_index,
					       rename_info, collisions);
	*clean_merge &= (new_path != NULL);

	return new_path;
}

static void apply_directory_rename_modifications(struct merge_options *opt,
						 struct diff_filepair *pair,
						 char *new_path)
{
	/*
	 * The basic idea is to get the conflict_info from opt->priv->paths
	 * at old path, and insert it into new_path; basically just this:
	 *     ci = strmap_get(&opt->priv->paths, old_path);
	 *     strmap_remove(&opt->priv->paths, old_path, 0);
	 *     strmap_put(&opt->priv->paths, new_path, ci);
	 * However, there are some factors complicating this:
	 *     - opt->priv->paths may already have an entry at new_path
	 *     - Each ci tracks its containing directory, so we need to
	 *       update that
	 *     - If another ci has the same containing directory, then
	 *       the two char*'s MUST point to the same location.  See the
	 *       comment in struct merged_info.  strcmp equality is not
	 *       enough; we need pointer equality.
	 *     - opt->priv->paths must hold the parent directories of any
	 *       entries that are added.  So, if this directory rename
	 *       causes entirely new directories, we must recursively add
	 *       parent directories.
	 *     - For each parent directory added to opt->priv->paths, we
	 *       also need to get its parent directory stored in its
	 *       conflict_info->merged.directory_name with all the same
	 *       requirements about pointer equality.
	 */
	struct string_list dirs_to_insert = STRING_LIST_INIT_NODUP;
	struct conflict_info *ci, *new_ci;
	struct strmap_entry *entry;
	const char *branch_with_new_path, *branch_with_dir_rename;
	const char *old_path = pair->two->path;
	const char *parent_name;
	const char *cur_path;
	int i, len;

	entry = strmap_get_entry(&opt->priv->paths, old_path);
	old_path = entry->key;
	ci = entry->value;
	VERIFY_CI(ci);

	/* Find parent directories missing from opt->priv->paths */
	cur_path = new_path;
	while (1) {
		/* Find the parent directory of cur_path */
		char *last_slash = strrchr(cur_path, '/');
		if (last_slash) {
			parent_name = xstrndup(cur_path, last_slash - cur_path);
		} else {
			parent_name = opt->priv->toplevel_dir;
			break;
		}

		/* Look it up in opt->priv->paths */
		entry = strmap_get_entry(&opt->priv->paths, parent_name);
		if (entry) {
			free((char*)parent_name);
			parent_name = entry->key; /* reuse known pointer */
			break;
		}

		/* Record this is one of the directories we need to insert */
		string_list_append(&dirs_to_insert, parent_name);
		cur_path = parent_name;
	}

	/* Traverse dirs_to_insert and insert them into opt->priv->paths */
	for (i = dirs_to_insert.nr-1; i >= 0; --i) {
		struct conflict_info *dir_ci;
		char *cur_dir = dirs_to_insert.items[i].string;

		CALLOC_ARRAY(dir_ci, 1);

		dir_ci->merged.directory_name = parent_name;
		len = strlen(parent_name);
		/* len+1 because of trailing '/' character */
		dir_ci->merged.basename_offset = (len > 0 ? len+1 : len);
		dir_ci->dirmask = ci->filemask;
		strmap_put(&opt->priv->paths, cur_dir, dir_ci);

		parent_name = cur_dir;
	}

	/*
	 * We are removing old_path from opt->priv->paths.  old_path also will
	 * eventually need to be freed, but it may still be used by e.g.
	 * ci->pathnames.  So, store it in another string-list for now.
	 */
	string_list_append(&opt->priv->paths_to_free, old_path);

	assert(ci->filemask == 2 || ci->filemask == 4);
	assert(ci->dirmask == 0);
	strmap_remove(&opt->priv->paths, old_path, 0);

	branch_with_new_path   = (ci->filemask == 2) ? opt->branch1 : opt->branch2;
	branch_with_dir_rename = (ci->filemask == 2) ? opt->branch2 : opt->branch1;

	/* Now, finally update ci and stick it into opt->priv->paths */
	ci->merged.directory_name = parent_name;
	len = strlen(parent_name);
	ci->merged.basename_offset = (len > 0 ? len+1 : len);
	new_ci = strmap_get(&opt->priv->paths, new_path);
	if (!new_ci) {
		/* Place ci back into opt->priv->paths, but at new_path */
		strmap_put(&opt->priv->paths, new_path, ci);
	} else {
		int index;

		/* A few sanity checks */
		VERIFY_CI(new_ci);
		assert(ci->filemask == 2 || ci->filemask == 4);
		assert((new_ci->filemask & ci->filemask) == 0);
		assert(!new_ci->merged.clean);

		/* Copy stuff from ci into new_ci */
		new_ci->filemask |= ci->filemask;
		if (new_ci->dirmask)
			new_ci->df_conflict = 1;
		index = (ci->filemask >> 1);
		new_ci->pathnames[index] = ci->pathnames[index];
		new_ci->stages[index].mode = ci->stages[index].mode;
		oidcpy(&new_ci->stages[index].oid, &ci->stages[index].oid);

		free(ci);
		ci = new_ci;
	}

	if (opt->detect_directory_renames == MERGE_DIRECTORY_RENAMES_TRUE) {
		/* Notify user of updated path */
		if (pair->status == 'A')
			path_msg(opt, new_path, 1,
				 _("Path updated: %s added in %s inside a "
				   "directory that was renamed in %s; moving "
				   "it to %s."),
				 old_path, branch_with_new_path,
				 branch_with_dir_rename, new_path);
		else
			path_msg(opt, new_path, 1,
				 _("Path updated: %s renamed to %s in %s, "
				   "inside a directory that was renamed in %s; "
				   "moving it to %s."),
				 pair->one->path, old_path, branch_with_new_path,
				 branch_with_dir_rename, new_path);
	} else {
		/*
		 * opt->detect_directory_renames has the value
		 * MERGE_DIRECTORY_RENAMES_CONFLICT, so mark these as conflicts.
		 */
		ci->path_conflict = 1;
		if (pair->status == 'A')
			path_msg(opt, new_path, 0,
				 _("CONFLICT (file location): %s added in %s "
				   "inside a directory that was renamed in %s, "
				   "suggesting it should perhaps be moved to "
				   "%s."),
				 old_path, branch_with_new_path,
				 branch_with_dir_rename, new_path);
		else
			path_msg(opt, new_path, 0,
				 _("CONFLICT (file location): %s renamed to %s "
				   "in %s, inside a directory that was renamed "
				   "in %s, suggesting it should perhaps be "
				   "moved to %s."),
				 pair->one->path, old_path, branch_with_new_path,
				 branch_with_dir_rename, new_path);
	}

	/*
	 * Finally, record the new location.
	 */
	pair->two->path = new_path;
}

/*** Function Grouping: functions related to regular rename detection ***/

static int process_renames(struct merge_options *opt,
			   struct diff_queue_struct *renames)
{
	int clean_merge = 1, i;

	for (i = 0; i < renames->nr; ++i) {
		const char *oldpath = NULL, *newpath;
		struct diff_filepair *pair = renames->queue[i];
		struct conflict_info *oldinfo = NULL, *newinfo = NULL;
		struct strmap_entry *old_ent, *new_ent;
		unsigned int old_sidemask;
		int target_index, other_source_index;
		int source_deleted, collision, type_changed;
		const char *rename_branch = NULL, *delete_branch = NULL;

		old_ent = strmap_get_entry(&opt->priv->paths, pair->one->path);
		new_ent = strmap_get_entry(&opt->priv->paths, pair->two->path);
		if (old_ent) {
			oldpath = old_ent->key;
			oldinfo = old_ent->value;
		}
		newpath = pair->two->path;
		if (new_ent) {
			newpath = new_ent->key;
			newinfo = new_ent->value;
		}

		/*
		 * If pair->one->path isn't in opt->priv->paths, that means
		 * that either directory rename detection removed that
		 * path, or a parent directory of oldpath was resolved and
		 * we don't even need the rename; in either case, we can
		 * skip it.  If oldinfo->merged.clean, then the other side
		 * of history had no changes to oldpath and we don't need
		 * the rename and can skip it.
		 */
		if (!oldinfo || oldinfo->merged.clean)
			continue;

		/*
		 * diff_filepairs have copies of pathnames, thus we have to
		 * use standard 'strcmp()' (negated) instead of '=='.
		 */
		if (i + 1 < renames->nr &&
		    !strcmp(oldpath, renames->queue[i+1]->one->path)) {
			/* Handle rename/rename(1to2) or rename/rename(1to1) */
			const char *pathnames[3];
			struct version_info merged;
			struct conflict_info *base, *side1, *side2;
			unsigned was_binary_blob = 0;

			pathnames[0] = oldpath;
			pathnames[1] = newpath;
			pathnames[2] = renames->queue[i+1]->two->path;

			base = strmap_get(&opt->priv->paths, pathnames[0]);
			side1 = strmap_get(&opt->priv->paths, pathnames[1]);
			side2 = strmap_get(&opt->priv->paths, pathnames[2]);

			VERIFY_CI(base);
			VERIFY_CI(side1);
			VERIFY_CI(side2);

			if (!strcmp(pathnames[1], pathnames[2])) {
				/* Both sides renamed the same way */
				assert(side1 == side2);
				memcpy(&side1->stages[0], &base->stages[0],
				       sizeof(merged));
				side1->filemask |= (1 << MERGE_BASE);
				/* Mark base as resolved by removal */
				base->merged.is_null = 1;
				base->merged.clean = 1;

				/* We handled both renames, i.e. i+1 handled */
				i++;
				/* Move to next rename */
				continue;
			}

			/* This is a rename/rename(1to2) */
			clean_merge = handle_content_merge(opt,
							   pair->one->path,
							   &base->stages[0],
							   &side1->stages[1],
							   &side2->stages[2],
							   pathnames,
							   1 + 2 * opt->priv->call_depth,
							   &merged);
			if (!clean_merge &&
			    merged.mode == side1->stages[1].mode &&
			    oideq(&merged.oid, &side1->stages[1].oid))
				was_binary_blob = 1;
			memcpy(&side1->stages[1], &merged, sizeof(merged));
			if (was_binary_blob) {
				/*
				 * Getting here means we were attempting to
				 * merge a binary blob.
				 *
				 * Since we can't merge binaries,
				 * handle_content_merge() just takes one
				 * side.  But we don't want to copy the
				 * contents of one side to both paths.  We
				 * used the contents of side1 above for
				 * side1->stages, let's use the contents of
				 * side2 for side2->stages below.
				 */
				oidcpy(&merged.oid, &side2->stages[2].oid);
				merged.mode = side2->stages[2].mode;
			}
			memcpy(&side2->stages[2], &merged, sizeof(merged));

			side1->path_conflict = 1;
			side2->path_conflict = 1;
			/*
			 * TODO: For renames we normally remove the path at the
			 * old name.  It would thus seem consistent to do the
			 * same for rename/rename(1to2) cases, but we haven't
			 * done so traditionally and a number of the regression
			 * tests now encode an expectation that the file is
			 * left there at stage 1.  If we ever decide to change
			 * this, add the following two lines here:
			 *    base->merged.is_null = 1;
			 *    base->merged.clean = 1;
			 * and remove the setting of base->path_conflict to 1.
			 */
			base->path_conflict = 1;
			path_msg(opt, oldpath, 0,
				 _("CONFLICT (rename/rename): %s renamed to "
				   "%s in %s and to %s in %s."),
				 pathnames[0],
				 pathnames[1], opt->branch1,
				 pathnames[2], opt->branch2);

			i++; /* We handled both renames, i.e. i+1 handled */
			continue;
		}

		VERIFY_CI(oldinfo);
		VERIFY_CI(newinfo);
		target_index = pair->score; /* from collect_renames() */
		assert(target_index == 1 || target_index == 2);
		other_source_index = 3 - target_index;
		old_sidemask = (1 << other_source_index); /* 2 or 4 */
		source_deleted = (oldinfo->filemask == 1);
		collision = ((newinfo->filemask & old_sidemask) != 0);
		type_changed = !source_deleted &&
			(S_ISREG(oldinfo->stages[other_source_index].mode) !=
			 S_ISREG(newinfo->stages[target_index].mode));
		if (type_changed && collision) {
			/*
			 * special handling so later blocks can handle this...
			 *
			 * if type_changed && collision are both true, then this
			 * was really a double rename, but one side wasn't
			 * detected due to lack of break detection.  I.e.
			 * something like
			 *    orig: has normal file 'foo'
			 *    side1: renames 'foo' to 'bar', adds 'foo' symlink
			 *    side2: renames 'foo' to 'bar'
			 * In this case, the foo->bar rename on side1 won't be
			 * detected because the new symlink named 'foo' is
			 * there and we don't do break detection.  But we detect
			 * this here because we don't want to merge the content
			 * of the foo symlink with the foo->bar file, so we
			 * have some logic to handle this special case.  The
			 * easiest way to do that is make 'bar' on side1 not
			 * be considered a colliding file but the other part
			 * of a normal rename.  If the file is very different,
			 * well we're going to get content merge conflicts
			 * anyway so it doesn't hurt.  And if the colliding
			 * file also has a different type, that'll be handled
			 * by the content merge logic in process_entry() too.
			 *
			 * See also t6430, 'rename vs. rename/symlink'
			 */
			collision = 0;
		}
		if (source_deleted) {
			if (target_index == 1) {
				rename_branch = opt->branch1;
				delete_branch = opt->branch2;
			} else {
				rename_branch = opt->branch2;
				delete_branch = opt->branch1;
			}
		}

		assert(source_deleted || oldinfo->filemask & old_sidemask);

		/* Need to check for special types of rename conflicts... */
		if (collision && !source_deleted) {
			/* collision: rename/add or rename/rename(2to1) */
			const char *pathnames[3];
			struct version_info merged;

			struct conflict_info *base, *side1, *side2;
			unsigned clean;

			pathnames[0] = oldpath;
			pathnames[other_source_index] = oldpath;
			pathnames[target_index] = newpath;

			base = strmap_get(&opt->priv->paths, pathnames[0]);
			side1 = strmap_get(&opt->priv->paths, pathnames[1]);
			side2 = strmap_get(&opt->priv->paths, pathnames[2]);

			VERIFY_CI(base);
			VERIFY_CI(side1);
			VERIFY_CI(side2);

			clean = handle_content_merge(opt, pair->one->path,
						     &base->stages[0],
						     &side1->stages[1],
						     &side2->stages[2],
						     pathnames,
						     1 + 2 * opt->priv->call_depth,
						     &merged);

			memcpy(&newinfo->stages[target_index], &merged,
			       sizeof(merged));
			if (!clean) {
				path_msg(opt, newpath, 0,
					 _("CONFLICT (rename involved in "
					   "collision): rename of %s -> %s has "
					   "content conflicts AND collides "
					   "with another path; this may result "
					   "in nested conflict markers."),
					 oldpath, newpath);
			}
		} else if (collision && source_deleted) {
			/*
			 * rename/add/delete or rename/rename(2to1)/delete:
			 * since oldpath was deleted on the side that didn't
			 * do the rename, there's not much of a content merge
			 * we can do for the rename.  oldinfo->merged.is_null
			 * was already set, so we just leave things as-is so
			 * they look like an add/add conflict.
			 */

			newinfo->path_conflict = 1;
			path_msg(opt, newpath, 0,
				 _("CONFLICT (rename/delete): %s renamed "
				   "to %s in %s, but deleted in %s."),
				 oldpath, newpath, rename_branch, delete_branch);
		} else {
			/*
			 * a few different cases...start by copying the
			 * existing stage(s) from oldinfo over the newinfo
			 * and update the pathname(s).
			 */
			memcpy(&newinfo->stages[0], &oldinfo->stages[0],
			       sizeof(newinfo->stages[0]));
			newinfo->filemask |= (1 << MERGE_BASE);
			newinfo->pathnames[0] = oldpath;
			if (type_changed) {
				/* rename vs. typechange */
				/* Mark the original as resolved by removal */
				memcpy(&oldinfo->stages[0].oid, null_oid(),
				       sizeof(oldinfo->stages[0].oid));
				oldinfo->stages[0].mode = 0;
				oldinfo->filemask &= 0x06;
			} else if (source_deleted) {
				/* rename/delete */
				newinfo->path_conflict = 1;
				path_msg(opt, newpath, 0,
					 _("CONFLICT (rename/delete): %s renamed"
					   " to %s in %s, but deleted in %s."),
					 oldpath, newpath,
					 rename_branch, delete_branch);
			} else {
				/* normal rename */
				memcpy(&newinfo->stages[other_source_index],
				       &oldinfo->stages[other_source_index],
				       sizeof(newinfo->stages[0]));
				newinfo->filemask |= (1 << other_source_index);
				newinfo->pathnames[other_source_index] = oldpath;
			}
		}

		if (!type_changed) {
			/* Mark the original as resolved by removal */
			oldinfo->merged.is_null = 1;
			oldinfo->merged.clean = 1;
		}

	}

	return clean_merge;
}

static inline int possible_side_renames(struct rename_info *renames,
					unsigned side_index)
{
	return renames->pairs[side_index].nr > 0 &&
	       !strintmap_empty(&renames->relevant_sources[side_index]);
}

static inline int possible_renames(struct rename_info *renames)
{
	return possible_side_renames(renames, 1) ||
	       possible_side_renames(renames, 2);
}

static void resolve_diffpair_statuses(struct diff_queue_struct *q)
{
	/*
	 * A simplified version of diff_resolve_rename_copy(); would probably
	 * just use that function but it's static...
	 */
	int i;
	struct diff_filepair *p;

	for (i = 0; i < q->nr; ++i) {
		p = q->queue[i];
		p->status = 0; /* undecided */
		if (!DIFF_FILE_VALID(p->one))
			p->status = DIFF_STATUS_ADDED;
		else if (!DIFF_FILE_VALID(p->two))
			p->status = DIFF_STATUS_DELETED;
		else if (DIFF_PAIR_RENAME(p))
			p->status = DIFF_STATUS_RENAMED;
	}
}

static int compare_pairs(const void *a_, const void *b_)
{
	const struct diff_filepair *a = *((const struct diff_filepair **)a_);
	const struct diff_filepair *b = *((const struct diff_filepair **)b_);

	return strcmp(a->one->path, b->one->path);
}

/* Call diffcore_rename() to compute which files have changed on given side */
static void detect_regular_renames(struct merge_options *opt,
				   unsigned side_index)
{
	struct diff_options diff_opts;
	struct rename_info *renames = &opt->priv->renames;

	if (!possible_side_renames(renames, side_index)) {
		/*
		 * No rename detection needed for this side, but we still need
		 * to make sure 'adds' are marked correctly in case the other
		 * side had directory renames.
		 */
		resolve_diffpair_statuses(&renames->pairs[side_index]);
		return;
	}

	repo_diff_setup(opt->repo, &diff_opts);
	diff_opts.flags.recursive = 1;
	diff_opts.flags.rename_empty = 0;
	diff_opts.detect_rename = DIFF_DETECT_RENAME;
	diff_opts.rename_limit = opt->rename_limit;
	if (opt->rename_limit <= 0)
		diff_opts.rename_limit = 1000;
	diff_opts.rename_score = opt->rename_score;
	diff_opts.show_rename_progress = opt->show_rename_progress;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_setup_done(&diff_opts);

	diff_queued_diff = renames->pairs[side_index];
	trace2_region_enter("diff", "diffcore_rename", opt->repo);
	diffcore_rename_extended(&diff_opts,
				 &renames->relevant_sources[side_index],
				 &renames->dirs_removed[side_index],
				 &renames->dir_rename_count[side_index]);
	trace2_region_leave("diff", "diffcore_rename", opt->repo);
	resolve_diffpair_statuses(&diff_queued_diff);

	if (diff_opts.needed_rename_limit > renames->needed_limit)
		renames->needed_limit = diff_opts.needed_rename_limit;

	renames->pairs[side_index] = diff_queued_diff;

	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_queued_diff.nr = 0;
	diff_queued_diff.queue = NULL;
	diff_flush(&diff_opts);
}

/*
 * Get information of all renames which occurred in 'side_pairs', discarding
 * non-renames.
 */
static int collect_renames(struct merge_options *opt,
			   struct diff_queue_struct *result,
			   unsigned side_index,
			   struct strmap *dir_renames_for_side,
			   struct strmap *rename_exclusions)
{
	int i, clean = 1;
	struct strmap collisions;
	struct diff_queue_struct *side_pairs;
	struct hashmap_iter iter;
	struct strmap_entry *entry;
	struct rename_info *renames = &opt->priv->renames;

	side_pairs = &renames->pairs[side_index];
	compute_collisions(&collisions, dir_renames_for_side, side_pairs);

	for (i = 0; i < side_pairs->nr; ++i) {
		struct diff_filepair *p = side_pairs->queue[i];
		char *new_path; /* non-NULL only with directory renames */

		if (p->status != 'A' && p->status != 'R') {
			diff_free_filepair(p);
			continue;
		}

		new_path = check_for_directory_rename(opt, p->two->path,
						      side_index,
						      dir_renames_for_side,
						      rename_exclusions,
						      &collisions,
						      &clean);

		if (p->status != 'R' && !new_path) {
			diff_free_filepair(p);
			continue;
		}

		if (new_path)
			apply_directory_rename_modifications(opt, p, new_path);

		/*
		 * p->score comes back from diffcore_rename_extended() with
		 * the similarity of the renamed file.  The similarity is
		 * was used to determine that the two files were related
		 * and are a rename, which we have already used, but beyond
		 * that we have no use for the similarity.  So p->score is
		 * now irrelevant.  However, process_renames() will need to
		 * know which side of the merge this rename was associated
		 * with, so overwrite p->score with that value.
		 */
		p->score = side_index;
		result->queue[result->nr++] = p;
	}

	/* Free each value in the collisions map */
	strmap_for_each_entry(&collisions, &iter, entry) {
		struct collision_info *info = entry->value;
		string_list_clear(&info->source_files, 0);
	}
	/*
	 * In compute_collisions(), we set collisions.strdup_strings to 0
	 * so that we wouldn't have to make another copy of the new_path
	 * allocated by apply_dir_rename().  But now that we've used them
	 * and have no other references to these strings, it is time to
	 * deallocate them.
	 */
	free_strmap_strings(&collisions);
	strmap_clear(&collisions, 1);
	return clean;
}

static int detect_and_process_renames(struct merge_options *opt,
				      struct tree *merge_base,
				      struct tree *side1,
				      struct tree *side2)
{
	struct diff_queue_struct combined;
	struct rename_info *renames = &opt->priv->renames;
	int need_dir_renames, s, clean = 1;

	memset(&combined, 0, sizeof(combined));
	if (!possible_renames(renames))
		goto cleanup;

	trace2_region_enter("merge", "regular renames", opt->repo);
	detect_regular_renames(opt, MERGE_SIDE1);
	detect_regular_renames(opt, MERGE_SIDE2);
	trace2_region_leave("merge", "regular renames", opt->repo);

	trace2_region_enter("merge", "directory renames", opt->repo);
	need_dir_renames =
	  !opt->priv->call_depth &&
	  (opt->detect_directory_renames == MERGE_DIRECTORY_RENAMES_TRUE ||
	   opt->detect_directory_renames == MERGE_DIRECTORY_RENAMES_CONFLICT);

	if (need_dir_renames) {
		get_provisional_directory_renames(opt, MERGE_SIDE1, &clean);
		get_provisional_directory_renames(opt, MERGE_SIDE2, &clean);
		handle_directory_level_conflicts(opt);
	}

	ALLOC_GROW(combined.queue,
		   renames->pairs[1].nr + renames->pairs[2].nr,
		   combined.alloc);
	clean &= collect_renames(opt, &combined, MERGE_SIDE1,
				 &renames->dir_renames[2],
				 &renames->dir_renames[1]);
	clean &= collect_renames(opt, &combined, MERGE_SIDE2,
				 &renames->dir_renames[1],
				 &renames->dir_renames[2]);
	STABLE_QSORT(combined.queue, combined.nr, compare_pairs);
	trace2_region_leave("merge", "directory renames", opt->repo);

	trace2_region_enter("merge", "process renames", opt->repo);
	clean &= process_renames(opt, &combined);
	trace2_region_leave("merge", "process renames", opt->repo);

	goto simple_cleanup; /* collect_renames() handles some of cleanup */

cleanup:
	/*
	 * Free now unneeded filepairs, which would have been handled
	 * in collect_renames() normally but we skipped that code.
	 */
	for (s = MERGE_SIDE1; s <= MERGE_SIDE2; s++) {
		struct diff_queue_struct *side_pairs;
		int i;

		side_pairs = &renames->pairs[s];
		for (i = 0; i < side_pairs->nr; ++i) {
			struct diff_filepair *p = side_pairs->queue[i];
			diff_free_filepair(p);
		}
	}

simple_cleanup:
	/* Free memory for renames->pairs[] and combined */
	for (s = MERGE_SIDE1; s <= MERGE_SIDE2; s++) {
		free(renames->pairs[s].queue);
		DIFF_QUEUE_CLEAR(&renames->pairs[s]);
	}
	if (combined.nr) {
		int i;
		for (i = 0; i < combined.nr; i++)
			diff_free_filepair(combined.queue[i]);
		free(combined.queue);
	}

	return clean;
}

/*** Function Grouping: functions related to process_entries() ***/

static int string_list_df_name_compare(const char *one, const char *two)
{
	int onelen = strlen(one);
	int twolen = strlen(two);
	/*
	 * Here we only care that entries for D/F conflicts are
	 * adjacent, in particular with the file of the D/F conflict
	 * appearing before files below the corresponding directory.
	 * The order of the rest of the list is irrelevant for us.
	 *
	 * To achieve this, we sort with df_name_compare and provide
	 * the mode S_IFDIR so that D/F conflicts will sort correctly.
	 * We use the mode S_IFDIR for everything else for simplicity,
	 * since in other cases any changes in their order due to
	 * sorting cause no problems for us.
	 */
	int cmp = df_name_compare(one, onelen, S_IFDIR,
				  two, twolen, S_IFDIR);
	/*
	 * Now that 'foo' and 'foo/bar' compare equal, we have to make sure
	 * that 'foo' comes before 'foo/bar'.
	 */
	if (cmp)
		return cmp;
	return onelen - twolen;
}

static int read_oid_strbuf(struct merge_options *opt,
			   const struct object_id *oid,
			   struct strbuf *dst)
{
	void *buf;
	enum object_type type;
	unsigned long size;
	buf = read_object_file(oid, &type, &size);
	if (!buf)
		return err(opt, _("cannot read object %s"), oid_to_hex(oid));
	if (type != OBJ_BLOB) {
		free(buf);
		return err(opt, _("object %s is not a blob"), oid_to_hex(oid));
	}
	strbuf_attach(dst, buf, size, size + 1);
	return 0;
}

static int blob_unchanged(struct merge_options *opt,
			  const struct version_info *base,
			  const struct version_info *side,
			  const char *path)
{
	struct strbuf basebuf = STRBUF_INIT;
	struct strbuf sidebuf = STRBUF_INIT;
	int ret = 0; /* assume changed for safety */
	struct index_state *idx = &opt->priv->attr_index;

	if (!idx->initialized)
		initialize_attr_index(opt);

	if (base->mode != side->mode)
		return 0;
	if (oideq(&base->oid, &side->oid))
		return 1;

	if (read_oid_strbuf(opt, &base->oid, &basebuf) ||
	    read_oid_strbuf(opt, &side->oid, &sidebuf))
		goto error_return;
	/*
	 * Note: binary | is used so that both renormalizations are
	 * performed.  Comparison can be skipped if both files are
	 * unchanged since their sha1s have already been compared.
	 */
	if (renormalize_buffer(idx, path, basebuf.buf, basebuf.len, &basebuf) |
	    renormalize_buffer(idx, path, sidebuf.buf, sidebuf.len, &sidebuf))
		ret = (basebuf.len == sidebuf.len &&
		       !memcmp(basebuf.buf, sidebuf.buf, basebuf.len));

error_return:
	strbuf_release(&basebuf);
	strbuf_release(&sidebuf);
	return ret;
}

struct directory_versions {
	/*
	 * versions: list of (basename -> version_info)
	 *
	 * The basenames are in reverse lexicographic order of full pathnames,
	 * as processed in process_entries().  This puts all entries within
	 * a directory together, and covers the directory itself after
	 * everything within it, allowing us to write subtrees before needing
	 * to record information for the tree itself.
	 */
	struct string_list versions;

	/*
	 * offsets: list of (full relative path directories -> integer offsets)
	 *
	 * Since versions contains basenames from files in multiple different
	 * directories, we need to know which entries in versions correspond
	 * to which directories.  Values of e.g.
	 *     ""             0
	 *     src            2
	 *     src/moduleA    5
	 * Would mean that entries 0-1 of versions are files in the toplevel
	 * directory, entries 2-4 are files under src/, and the remaining
	 * entries starting at index 5 are files under src/moduleA/.
	 */
	struct string_list offsets;

	/*
	 * last_directory: directory that previously processed file found in
	 *
	 * last_directory starts NULL, but records the directory in which the
	 * previous file was found within.  As soon as
	 *    directory(current_file) != last_directory
	 * then we need to start updating accounting in versions & offsets.
	 * Note that last_directory is always the last path in "offsets" (or
	 * NULL if "offsets" is empty) so this exists just for quick access.
	 */
	const char *last_directory;

	/* last_directory_len: cached computation of strlen(last_directory) */
	unsigned last_directory_len;
};

static int tree_entry_order(const void *a_, const void *b_)
{
	const struct string_list_item *a = a_;
	const struct string_list_item *b = b_;

	const struct merged_info *ami = a->util;
	const struct merged_info *bmi = b->util;
	return base_name_compare(a->string, strlen(a->string), ami->result.mode,
				 b->string, strlen(b->string), bmi->result.mode);
}

static void write_tree(struct object_id *result_oid,
		       struct string_list *versions,
		       unsigned int offset,
		       size_t hash_size)
{
	size_t maxlen = 0, extra;
	unsigned int nr;
	struct strbuf buf = STRBUF_INIT;
	int i;

	assert(offset <= versions->nr);
	nr = versions->nr - offset;
	if (versions->nr)
		/* No need for STABLE_QSORT -- filenames must be unique */
		QSORT(versions->items + offset, nr, tree_entry_order);

	/* Pre-allocate some space in buf */
	extra = hash_size + 8; /* 8: 6 for mode, 1 for space, 1 for NUL char */
	for (i = 0; i < nr; i++) {
		maxlen += strlen(versions->items[offset+i].string) + extra;
	}
	strbuf_grow(&buf, maxlen);

	/* Write each entry out to buf */
	for (i = 0; i < nr; i++) {
		struct merged_info *mi = versions->items[offset+i].util;
		struct version_info *ri = &mi->result;
		strbuf_addf(&buf, "%o %s%c",
			    ri->mode,
			    versions->items[offset+i].string, '\0');
		strbuf_add(&buf, ri->oid.hash, hash_size);
	}

	/* Write this object file out, and record in result_oid */
	write_object_file(buf.buf, buf.len, tree_type, result_oid);
	strbuf_release(&buf);
}

static void record_entry_for_tree(struct directory_versions *dir_metadata,
				  const char *path,
				  struct merged_info *mi)
{
	const char *basename;

	if (mi->is_null)
		/* nothing to record */
		return;

	basename = path + mi->basename_offset;
	assert(strchr(basename, '/') == NULL);
	string_list_append(&dir_metadata->versions,
			   basename)->util = &mi->result;
}

static void write_completed_directory(struct merge_options *opt,
				      const char *new_directory_name,
				      struct directory_versions *info)
{
	const char *prev_dir;
	struct merged_info *dir_info = NULL;
	unsigned int offset;

	/*
	 * Some explanation of info->versions and info->offsets...
	 *
	 * process_entries() iterates over all relevant files AND
	 * directories in reverse lexicographic order, and calls this
	 * function.  Thus, an example of the paths that process_entries()
	 * could operate on (along with the directories for those paths
	 * being shown) is:
	 *
	 *     xtract.c             ""
	 *     tokens.txt           ""
	 *     src/moduleB/umm.c    src/moduleB
	 *     src/moduleB/stuff.h  src/moduleB
	 *     src/moduleB/baz.c    src/moduleB
	 *     src/moduleB          src
	 *     src/moduleA/foo.c    src/moduleA
	 *     src/moduleA/bar.c    src/moduleA
	 *     src/moduleA          src
	 *     src                  ""
	 *     Makefile             ""
	 *
	 * info->versions:
	 *
	 *     always contains the unprocessed entries and their
	 *     version_info information.  For example, after the first five
	 *     entries above, info->versions would be:
	 *
	 *     	   xtract.c     <xtract.c's version_info>
	 *     	   token.txt    <token.txt's version_info>
	 *     	   umm.c        <src/moduleB/umm.c's version_info>
	 *     	   stuff.h      <src/moduleB/stuff.h's version_info>
	 *     	   baz.c        <src/moduleB/baz.c's version_info>
	 *
	 *     Once a subdirectory is completed we remove the entries in
	 *     that subdirectory from info->versions, writing it as a tree
	 *     (write_tree()).  Thus, as soon as we get to src/moduleB,
	 *     info->versions would be updated to
	 *
	 *     	   xtract.c     <xtract.c's version_info>
	 *     	   token.txt    <token.txt's version_info>
	 *     	   moduleB      <src/moduleB's version_info>
	 *
	 * info->offsets:
	 *
	 *     helps us track which entries in info->versions correspond to
	 *     which directories.  When we are N directories deep (e.g. 4
	 *     for src/modA/submod/subdir/), we have up to N+1 unprocessed
	 *     directories (+1 because of toplevel dir).  Corresponding to
	 *     the info->versions example above, after processing five entries
	 *     info->offsets will be:
	 *
	 *     	   ""           0
	 *     	   src/moduleB  2
	 *
	 *     which is used to know that xtract.c & token.txt are from the
	 *     toplevel dirctory, while umm.c & stuff.h & baz.c are from the
	 *     src/moduleB directory.  Again, following the example above,
	 *     once we need to process src/moduleB, then info->offsets is
	 *     updated to
	 *
	 *     	   ""           0
	 *     	   src          2
	 *
	 *     which says that moduleB (and only moduleB so far) is in the
	 *     src directory.
	 *
	 *     One unique thing to note about info->offsets here is that
	 *     "src" was not added to info->offsets until there was a path
	 *     (a file OR directory) immediately below src/ that got
	 *     processed.
	 *
	 * Since process_entry() just appends new entries to info->versions,
	 * write_completed_directory() only needs to do work if the next path
	 * is in a directory that is different than the last directory found
	 * in info->offsets.
	 */

	/*
	 * If we are working with the same directory as the last entry, there
	 * is no work to do.  (See comments above the directory_name member of
	 * struct merged_info for why we can use pointer comparison instead of
	 * strcmp here.)
	 */
	if (new_directory_name == info->last_directory)
		return;

	/*
	 * If we are just starting (last_directory is NULL), or last_directory
	 * is a prefix of the current directory, then we can just update
	 * info->offsets to record the offset where we started this directory
	 * and update last_directory to have quick access to it.
	 */
	if (info->last_directory == NULL ||
	    !strncmp(new_directory_name, info->last_directory,
		     info->last_directory_len)) {
		uintptr_t offset = info->versions.nr;

		info->last_directory = new_directory_name;
		info->last_directory_len = strlen(info->last_directory);
		/*
		 * Record the offset into info->versions where we will
		 * start recording basenames of paths found within
		 * new_directory_name.
		 */
		string_list_append(&info->offsets,
				   info->last_directory)->util = (void*)offset;
		return;
	}

	/*
	 * The next entry that will be processed will be within
	 * new_directory_name.  Since at this point we know that
	 * new_directory_name is within a different directory than
	 * info->last_directory, we have all entries for info->last_directory
	 * in info->versions and we need to create a tree object for them.
	 */
	dir_info = strmap_get(&opt->priv->paths, info->last_directory);
	assert(dir_info);
	offset = (uintptr_t)info->offsets.items[info->offsets.nr-1].util;
	if (offset == info->versions.nr) {
		/*
		 * Actually, we don't need to create a tree object in this
		 * case.  Whenever all files within a directory disappear
		 * during the merge (e.g. unmodified on one side and
		 * deleted on the other, or files were renamed elsewhere),
		 * then we get here and the directory itself needs to be
		 * omitted from its parent tree as well.
		 */
		dir_info->is_null = 1;
	} else {
		/*
		 * Write out the tree to the git object directory, and also
		 * record the mode and oid in dir_info->result.
		 */
		dir_info->is_null = 0;
		dir_info->result.mode = S_IFDIR;
		write_tree(&dir_info->result.oid, &info->versions, offset,
			   opt->repo->hash_algo->rawsz);
	}

	/*
	 * We've now used several entries from info->versions and one entry
	 * from info->offsets, so we get rid of those values.
	 */
	info->offsets.nr--;
	info->versions.nr = offset;

	/*
	 * Now we've taken care of the completed directory, but we need to
	 * prepare things since future entries will be in
	 * new_directory_name.  (In particular, process_entry() will be
	 * appending new entries to info->versions.)  So, we need to make
	 * sure new_directory_name is the last entry in info->offsets.
	 */
	prev_dir = info->offsets.nr == 0 ? NULL :
		   info->offsets.items[info->offsets.nr-1].string;
	if (new_directory_name != prev_dir) {
		uintptr_t c = info->versions.nr;
		string_list_append(&info->offsets,
				   new_directory_name)->util = (void*)c;
	}

	/* And, of course, we need to update last_directory to match. */
	info->last_directory = new_directory_name;
	info->last_directory_len = strlen(info->last_directory);
}

/* Per entry merge function */
static void process_entry(struct merge_options *opt,
			  const char *path,
			  struct conflict_info *ci,
			  struct directory_versions *dir_metadata)
{
	int df_file_index = 0;

	VERIFY_CI(ci);
	assert(ci->filemask >= 0 && ci->filemask <= 7);
	/* ci->match_mask == 7 was handled in collect_merge_info_callback() */
	assert(ci->match_mask == 0 || ci->match_mask == 3 ||
	       ci->match_mask == 5 || ci->match_mask == 6);

	if (ci->dirmask) {
		record_entry_for_tree(dir_metadata, path, &ci->merged);
		if (ci->filemask == 0)
			/* nothing else to handle */
			return;
		assert(ci->df_conflict);
	}

	if (ci->df_conflict && ci->merged.result.mode == 0) {
		int i;

		/*
		 * directory no longer in the way, but we do have a file we
		 * need to place here so we need to clean away the "directory
		 * merges to nothing" result.
		 */
		ci->df_conflict = 0;
		assert(ci->filemask != 0);
		ci->merged.clean = 0;
		ci->merged.is_null = 0;
		/* and we want to zero out any directory-related entries */
		ci->match_mask = (ci->match_mask & ~ci->dirmask);
		ci->dirmask = 0;
		for (i = MERGE_BASE; i <= MERGE_SIDE2; i++) {
			if (ci->filemask & (1 << i))
				continue;
			ci->stages[i].mode = 0;
			oidcpy(&ci->stages[i].oid, null_oid());
		}
	} else if (ci->df_conflict && ci->merged.result.mode != 0) {
		/*
		 * This started out as a D/F conflict, and the entries in
		 * the competing directory were not removed by the merge as
		 * evidenced by write_completed_directory() writing a value
		 * to ci->merged.result.mode.
		 */
		struct conflict_info *new_ci;
		const char *branch;
		const char *old_path = path;
		int i;

		assert(ci->merged.result.mode == S_IFDIR);

		/*
		 * If filemask is 1, we can just ignore the file as having
		 * been deleted on both sides.  We do not want to overwrite
		 * ci->merged.result, since it stores the tree for all the
		 * files under it.
		 */
		if (ci->filemask == 1) {
			ci->filemask = 0;
			return;
		}

		/*
		 * This file still exists on at least one side, and we want
		 * the directory to remain here, so we need to move this
		 * path to some new location.
		 */
		CALLOC_ARRAY(new_ci, 1);
		/* We don't really want new_ci->merged.result copied, but it'll
		 * be overwritten below so it doesn't matter.  We also don't
		 * want any directory mode/oid values copied, but we'll zero
		 * those out immediately.  We do want the rest of ci copied.
		 */
		memcpy(new_ci, ci, sizeof(*ci));
		new_ci->match_mask = (new_ci->match_mask & ~new_ci->dirmask);
		new_ci->dirmask = 0;
		for (i = MERGE_BASE; i <= MERGE_SIDE2; i++) {
			if (new_ci->filemask & (1 << i))
				continue;
			/* zero out any entries related to directories */
			new_ci->stages[i].mode = 0;
			oidcpy(&new_ci->stages[i].oid, null_oid());
		}

		/*
		 * Find out which side this file came from; note that we
		 * cannot just use ci->filemask, because renames could cause
		 * the filemask to go back to 7.  So we use dirmask, then
		 * pick the opposite side's index.
		 */
		df_file_index = (ci->dirmask & (1 << 1)) ? 2 : 1;
		branch = (df_file_index == 1) ? opt->branch1 : opt->branch2;
		path = unique_path(&opt->priv->paths, path, branch);
		strmap_put(&opt->priv->paths, path, new_ci);

		path_msg(opt, path, 0,
			 _("CONFLICT (file/directory): directory in the way "
			   "of %s from %s; moving it to %s instead."),
			 old_path, branch, path);

		/*
		 * Zero out the filemask for the old ci.  At this point, ci
		 * was just an entry for a directory, so we don't need to
		 * do anything more with it.
		 */
		ci->filemask = 0;

		/*
		 * Now note that we're working on the new entry (path was
		 * updated above.
		 */
		ci = new_ci;
	}

	/*
	 * NOTE: Below there is a long switch-like if-elseif-elseif... block
	 *       which the code goes through even for the df_conflict cases
	 *       above.
	 */
	if (ci->match_mask) {
		ci->merged.clean = 1;
		if (ci->match_mask == 6) {
			/* stages[1] == stages[2] */
			ci->merged.result.mode = ci->stages[1].mode;
			oidcpy(&ci->merged.result.oid, &ci->stages[1].oid);
		} else {
			/* determine the mask of the side that didn't match */
			unsigned int othermask = 7 & ~ci->match_mask;
			int side = (othermask == 4) ? 2 : 1;

			ci->merged.result.mode = ci->stages[side].mode;
			ci->merged.is_null = !ci->merged.result.mode;
			oidcpy(&ci->merged.result.oid, &ci->stages[side].oid);

			assert(othermask == 2 || othermask == 4);
			assert(ci->merged.is_null ==
			       (ci->filemask == ci->match_mask));
		}
	} else if (ci->filemask >= 6 &&
		   (S_IFMT & ci->stages[1].mode) !=
		   (S_IFMT & ci->stages[2].mode)) {
		/* Two different items from (file/submodule/symlink) */
		if (opt->priv->call_depth) {
			/* Just use the version from the merge base */
			ci->merged.clean = 0;
			oidcpy(&ci->merged.result.oid, &ci->stages[0].oid);
			ci->merged.result.mode = ci->stages[0].mode;
			ci->merged.is_null = (ci->merged.result.mode == 0);
		} else {
			/* Handle by renaming one or both to separate paths. */
			unsigned o_mode = ci->stages[0].mode;
			unsigned a_mode = ci->stages[1].mode;
			unsigned b_mode = ci->stages[2].mode;
			struct conflict_info *new_ci;
			const char *a_path = NULL, *b_path = NULL;
			int rename_a = 0, rename_b = 0;

			new_ci = xmalloc(sizeof(*new_ci));

			if (S_ISREG(a_mode))
				rename_a = 1;
			else if (S_ISREG(b_mode))
				rename_b = 1;
			else {
				rename_a = 1;
				rename_b = 1;
			}

			path_msg(opt, path, 0,
				 _("CONFLICT (distinct types): %s had different "
				   "types on each side; renamed %s of them so "
				   "each can be recorded somewhere."),
				 path,
				 (rename_a && rename_b) ? _("both") : _("one"));

			ci->merged.clean = 0;
			memcpy(new_ci, ci, sizeof(*new_ci));

			/* Put b into new_ci, removing a from stages */
			new_ci->merged.result.mode = ci->stages[2].mode;
			oidcpy(&new_ci->merged.result.oid, &ci->stages[2].oid);
			new_ci->stages[1].mode = 0;
			oidcpy(&new_ci->stages[1].oid, null_oid());
			new_ci->filemask = 5;
			if ((S_IFMT & b_mode) != (S_IFMT & o_mode)) {
				new_ci->stages[0].mode = 0;
				oidcpy(&new_ci->stages[0].oid, null_oid());
				new_ci->filemask = 4;
			}

			/* Leave only a in ci, fixing stages. */
			ci->merged.result.mode = ci->stages[1].mode;
			oidcpy(&ci->merged.result.oid, &ci->stages[1].oid);
			ci->stages[2].mode = 0;
			oidcpy(&ci->stages[2].oid, null_oid());
			ci->filemask = 3;
			if ((S_IFMT & a_mode) != (S_IFMT & o_mode)) {
				ci->stages[0].mode = 0;
				oidcpy(&ci->stages[0].oid, null_oid());
				ci->filemask = 2;
			}

			/* Insert entries into opt->priv_paths */
			assert(rename_a || rename_b);
			if (rename_a) {
				a_path = unique_path(&opt->priv->paths,
						     path, opt->branch1);
				strmap_put(&opt->priv->paths, a_path, ci);
			}

			if (rename_b)
				b_path = unique_path(&opt->priv->paths,
						     path, opt->branch2);
			else
				b_path = path;
			strmap_put(&opt->priv->paths, b_path, new_ci);

			if (rename_a && rename_b) {
				strmap_remove(&opt->priv->paths, path, 0);
				/*
				 * We removed path from opt->priv->paths.  path
				 * will also eventually need to be freed, but
				 * it may still be used by e.g.  ci->pathnames.
				 * So, store it in another string-list for now.
				 */
				string_list_append(&opt->priv->paths_to_free,
						   path);
			}

			/*
			 * Do special handling for b_path since process_entry()
			 * won't be called on it specially.
			 */
			strmap_put(&opt->priv->conflicted, b_path, new_ci);
			record_entry_for_tree(dir_metadata, b_path,
					      &new_ci->merged);

			/*
			 * Remaining code for processing this entry should
			 * think in terms of processing a_path.
			 */
			if (a_path)
				path = a_path;
		}
	} else if (ci->filemask >= 6) {
		/* Need a two-way or three-way content merge */
		struct version_info merged_file;
		unsigned clean_merge;
		struct version_info *o = &ci->stages[0];
		struct version_info *a = &ci->stages[1];
		struct version_info *b = &ci->stages[2];

		clean_merge = handle_content_merge(opt, path, o, a, b,
						   ci->pathnames,
						   opt->priv->call_depth * 2,
						   &merged_file);
		ci->merged.clean = clean_merge &&
				   !ci->df_conflict && !ci->path_conflict;
		ci->merged.result.mode = merged_file.mode;
		ci->merged.is_null = (merged_file.mode == 0);
		oidcpy(&ci->merged.result.oid, &merged_file.oid);
		if (clean_merge && ci->df_conflict) {
			assert(df_file_index == 1 || df_file_index == 2);
			ci->filemask = 1 << df_file_index;
			ci->stages[df_file_index].mode = merged_file.mode;
			oidcpy(&ci->stages[df_file_index].oid, &merged_file.oid);
		}
		if (!clean_merge) {
			const char *reason = _("content");
			if (ci->filemask == 6)
				reason = _("add/add");
			if (S_ISGITLINK(merged_file.mode))
				reason = _("submodule");
			path_msg(opt, path, 0,
				 _("CONFLICT (%s): Merge conflict in %s"),
				 reason, path);
		}
	} else if (ci->filemask == 3 || ci->filemask == 5) {
		/* Modify/delete */
		const char *modify_branch, *delete_branch;
		int side = (ci->filemask == 5) ? 2 : 1;
		int index = opt->priv->call_depth ? 0 : side;

		ci->merged.result.mode = ci->stages[index].mode;
		oidcpy(&ci->merged.result.oid, &ci->stages[index].oid);
		ci->merged.clean = 0;

		modify_branch = (side == 1) ? opt->branch1 : opt->branch2;
		delete_branch = (side == 1) ? opt->branch2 : opt->branch1;

		if (opt->renormalize &&
		    blob_unchanged(opt, &ci->stages[0], &ci->stages[side],
				   path)) {
			ci->merged.is_null = 1;
			ci->merged.clean = 1;
		} else if (ci->path_conflict &&
			   oideq(&ci->stages[0].oid, &ci->stages[side].oid)) {
			/*
			 * This came from a rename/delete; no action to take,
			 * but avoid printing "modify/delete" conflict notice
			 * since the contents were not modified.
			 */
		} else {
			path_msg(opt, path, 0,
				 _("CONFLICT (modify/delete): %s deleted in %s "
				   "and modified in %s.  Version %s of %s left "
				   "in tree."),
				 path, delete_branch, modify_branch,
				 modify_branch, path);
		}
	} else if (ci->filemask == 2 || ci->filemask == 4) {
		/* Added on one side */
		int side = (ci->filemask == 4) ? 2 : 1;
		ci->merged.result.mode = ci->stages[side].mode;
		oidcpy(&ci->merged.result.oid, &ci->stages[side].oid);
		ci->merged.clean = !ci->df_conflict && !ci->path_conflict;
	} else if (ci->filemask == 1) {
		/* Deleted on both sides */
		ci->merged.is_null = 1;
		ci->merged.result.mode = 0;
		oidcpy(&ci->merged.result.oid, null_oid());
		ci->merged.clean = !ci->path_conflict;
	}

	/*
	 * If still conflicted, record it separately.  This allows us to later
	 * iterate over just conflicted entries when updating the index instead
	 * of iterating over all entries.
	 */
	if (!ci->merged.clean)
		strmap_put(&opt->priv->conflicted, path, ci);
	record_entry_for_tree(dir_metadata, path, &ci->merged);
}

static void process_entries(struct merge_options *opt,
			    struct object_id *result_oid)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;
	struct string_list plist = STRING_LIST_INIT_NODUP;
	struct string_list_item *entry;
	struct directory_versions dir_metadata = { STRING_LIST_INIT_NODUP,
						   STRING_LIST_INIT_NODUP,
						   NULL, 0 };

	trace2_region_enter("merge", "process_entries setup", opt->repo);
	if (strmap_empty(&opt->priv->paths)) {
		oidcpy(result_oid, opt->repo->hash_algo->empty_tree);
		return;
	}

	/* Hack to pre-allocate plist to the desired size */
	trace2_region_enter("merge", "plist grow", opt->repo);
	ALLOC_GROW(plist.items, strmap_get_size(&opt->priv->paths), plist.alloc);
	trace2_region_leave("merge", "plist grow", opt->repo);

	/* Put every entry from paths into plist, then sort */
	trace2_region_enter("merge", "plist copy", opt->repo);
	strmap_for_each_entry(&opt->priv->paths, &iter, e) {
		string_list_append(&plist, e->key)->util = e->value;
	}
	trace2_region_leave("merge", "plist copy", opt->repo);

	trace2_region_enter("merge", "plist special sort", opt->repo);
	plist.cmp = string_list_df_name_compare;
	string_list_sort(&plist);
	trace2_region_leave("merge", "plist special sort", opt->repo);

	trace2_region_leave("merge", "process_entries setup", opt->repo);

	/*
	 * Iterate over the items in reverse order, so we can handle paths
	 * below a directory before needing to handle the directory itself.
	 *
	 * This allows us to write subtrees before we need to write trees,
	 * and it also enables sane handling of directory/file conflicts
	 * (because it allows us to know whether the directory is still in
	 * the way when it is time to process the file at the same path).
	 */
	trace2_region_enter("merge", "processing", opt->repo);
	for (entry = &plist.items[plist.nr-1]; entry >= plist.items; --entry) {
		char *path = entry->string;
		/*
		 * NOTE: mi may actually be a pointer to a conflict_info, but
		 * we have to check mi->clean first to see if it's safe to
		 * reassign to such a pointer type.
		 */
		struct merged_info *mi = entry->util;

		write_completed_directory(opt, mi->directory_name,
					  &dir_metadata);
		if (mi->clean)
			record_entry_for_tree(&dir_metadata, path, mi);
		else {
			struct conflict_info *ci = (struct conflict_info *)mi;
			process_entry(opt, path, ci, &dir_metadata);
		}
	}
	trace2_region_leave("merge", "processing", opt->repo);

	trace2_region_enter("merge", "process_entries cleanup", opt->repo);
	if (dir_metadata.offsets.nr != 1 ||
	    (uintptr_t)dir_metadata.offsets.items[0].util != 0) {
		printf("dir_metadata.offsets.nr = %d (should be 1)\n",
		       dir_metadata.offsets.nr);
		printf("dir_metadata.offsets.items[0].util = %u (should be 0)\n",
		       (unsigned)(uintptr_t)dir_metadata.offsets.items[0].util);
		fflush(stdout);
		BUG("dir_metadata accounting completely off; shouldn't happen");
	}
	write_tree(result_oid, &dir_metadata.versions, 0,
		   opt->repo->hash_algo->rawsz);
	string_list_clear(&plist, 0);
	string_list_clear(&dir_metadata.versions, 0);
	string_list_clear(&dir_metadata.offsets, 0);
	trace2_region_leave("merge", "process_entries cleanup", opt->repo);
}

/*** Function Grouping: functions related to merge_switch_to_result() ***/

static int checkout(struct merge_options *opt,
		    struct tree *prev,
		    struct tree *next)
{
	/* Switch the index/working copy from old to new */
	int ret;
	struct tree_desc trees[2];
	struct unpack_trees_options unpack_opts;

	memset(&unpack_opts, 0, sizeof(unpack_opts));
	unpack_opts.head_idx = -1;
	unpack_opts.src_index = opt->repo->index;
	unpack_opts.dst_index = opt->repo->index;

	setup_unpack_trees_porcelain(&unpack_opts, "merge");

	/*
	 * NOTE: if this were just "git checkout" code, we would probably
	 * read or refresh the cache and check for a conflicted index, but
	 * builtin/merge.c or sequencer.c really needs to read the index
	 * and check for conflicted entries before starting merging for a
	 * good user experience (no sense waiting for merges/rebases before
	 * erroring out), so there's no reason to duplicate that work here.
	 */

	/* 2-way merge to the new branch */
	unpack_opts.update = 1;
	unpack_opts.merge = 1;
	unpack_opts.quiet = 0; /* FIXME: sequencer might want quiet? */
	unpack_opts.verbose_update = (opt->verbosity > 2);
	unpack_opts.fn = twoway_merge;
	if (1/* FIXME: opts->overwrite_ignore*/) {
		CALLOC_ARRAY(unpack_opts.dir, 1);
		unpack_opts.dir->flags |= DIR_SHOW_IGNORED;
		setup_standard_excludes(unpack_opts.dir);
	}
	parse_tree(prev);
	init_tree_desc(&trees[0], prev->buffer, prev->size);
	parse_tree(next);
	init_tree_desc(&trees[1], next->buffer, next->size);

	ret = unpack_trees(2, trees, &unpack_opts);
	clear_unpack_trees_porcelain(&unpack_opts);
	dir_clear(unpack_opts.dir);
	FREE_AND_NULL(unpack_opts.dir);
	return ret;
}

static int record_conflicted_index_entries(struct merge_options *opt)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;
	struct index_state *index = opt->repo->index;
	struct checkout state = CHECKOUT_INIT;
	int errs = 0;
	int original_cache_nr;

	if (strmap_empty(&opt->priv->conflicted))
		return 0;

	/* If any entries have skip_worktree set, we'll have to check 'em out */
	state.force = 1;
	state.quiet = 1;
	state.refresh_cache = 1;
	state.istate = index;
	original_cache_nr = index->cache_nr;

	/* Put every entry from paths into plist, then sort */
	strmap_for_each_entry(&opt->priv->conflicted, &iter, e) {
		const char *path = e->key;
		struct conflict_info *ci = e->value;
		int pos;
		struct cache_entry *ce;
		int i;

		VERIFY_CI(ci);

		/*
		 * The index will already have a stage=0 entry for this path,
		 * because we created an as-merged-as-possible version of the
		 * file and checkout() moved the working copy and index over
		 * to that version.
		 *
		 * However, previous iterations through this loop will have
		 * added unstaged entries to the end of the cache which
		 * ignore the standard alphabetical ordering of cache
		 * entries and break invariants needed for index_name_pos()
		 * to work.  However, we know the entry we want is before
		 * those appended cache entries, so do a temporary swap on
		 * cache_nr to only look through entries of interest.
		 */
		SWAP(index->cache_nr, original_cache_nr);
		pos = index_name_pos(index, path, strlen(path));
		SWAP(index->cache_nr, original_cache_nr);
		if (pos < 0) {
			if (ci->filemask != 1)
				BUG("Conflicted %s but nothing in basic working tree or index; this shouldn't happen", path);
			cache_tree_invalidate_path(index, path);
		} else {
			ce = index->cache[pos];

			/*
			 * Clean paths with CE_SKIP_WORKTREE set will not be
			 * written to the working tree by the unpack_trees()
			 * call in checkout().  Our conflicted entries would
			 * have appeared clean to that code since we ignored
			 * the higher order stages.  Thus, we need override
			 * the CE_SKIP_WORKTREE bit and manually write those
			 * files to the working disk here.
			 */
			if (ce_skip_worktree(ce)) {
				struct stat st;

				if (!lstat(path, &st)) {
					char *new_name = unique_path(&opt->priv->paths,
								     path,
								     "cruft");

					path_msg(opt, path, 1,
						 _("Note: %s not up to date and in way of checking out conflicted version; old copy renamed to %s"),
						 path, new_name);
					errs |= rename(path, new_name);
					free(new_name);
				}
				errs |= checkout_entry(ce, &state, NULL, NULL);
			}

			/*
			 * Mark this cache entry for removal and instead add
			 * new stage>0 entries corresponding to the
			 * conflicts.  If there are many conflicted entries, we
			 * want to avoid memmove'ing O(NM) entries by
			 * inserting the new entries one at a time.  So,
			 * instead, we just add the new cache entries to the
			 * end (ignoring normal index requirements on sort
			 * order) and sort the index once we're all done.
			 */
			ce->ce_flags |= CE_REMOVE;
		}

		for (i = MERGE_BASE; i <= MERGE_SIDE2; i++) {
			struct version_info *vi;
			if (!(ci->filemask & (1ul << i)))
				continue;
			vi = &ci->stages[i];
			ce = make_cache_entry(index, vi->mode, &vi->oid,
					      path, i+1, 0);
			add_index_entry(index, ce, ADD_CACHE_JUST_APPEND);
		}
	}

	/*
	 * Remove the unused cache entries (and invalidate the relevant
	 * cache-trees), then sort the index entries to get the conflicted
	 * entries we added to the end into their right locations.
	 */
	remove_marked_cache_entries(index, 1);
	/*
	 * No need for STABLE_QSORT -- cmp_cache_name_compare sorts primarily
	 * on filename and secondarily on stage, and (name, stage #) are a
	 * unique tuple.
	 */
	QSORT(index->cache, index->cache_nr, cmp_cache_name_compare);

	return errs;
}

void merge_switch_to_result(struct merge_options *opt,
			    struct tree *head,
			    struct merge_result *result,
			    int update_worktree_and_index,
			    int display_update_msgs)
{
	assert(opt->priv == NULL);
	if (result->clean >= 0 && update_worktree_and_index) {
		const char *filename;
		FILE *fp;

		trace2_region_enter("merge", "checkout", opt->repo);
		if (checkout(opt, head, result->tree)) {
			/* failure to function */
			result->clean = -1;
			return;
		}
		trace2_region_leave("merge", "checkout", opt->repo);

		trace2_region_enter("merge", "record_conflicted", opt->repo);
		opt->priv = result->priv;
		if (record_conflicted_index_entries(opt)) {
			/* failure to function */
			opt->priv = NULL;
			result->clean = -1;
			return;
		}
		opt->priv = NULL;
		trace2_region_leave("merge", "record_conflicted", opt->repo);

		trace2_region_enter("merge", "write_auto_merge", opt->repo);
		filename = git_path_auto_merge(opt->repo);
		fp = xfopen(filename, "w");
		fprintf(fp, "%s\n", oid_to_hex(&result->tree->object.oid));
		fclose(fp);
		trace2_region_leave("merge", "write_auto_merge", opt->repo);
	}

	if (display_update_msgs) {
		struct merge_options_internal *opti = result->priv;
		struct hashmap_iter iter;
		struct strmap_entry *e;
		struct string_list olist = STRING_LIST_INIT_NODUP;
		int i;

		trace2_region_enter("merge", "display messages", opt->repo);

		/* Hack to pre-allocate olist to the desired size */
		ALLOC_GROW(olist.items, strmap_get_size(&opti->output),
			   olist.alloc);

		/* Put every entry from output into olist, then sort */
		strmap_for_each_entry(&opti->output, &iter, e) {
			string_list_append(&olist, e->key)->util = e->value;
		}
		string_list_sort(&olist);

		/* Iterate over the items, printing them */
		for (i = 0; i < olist.nr; ++i) {
			struct strbuf *sb = olist.items[i].util;

			printf("%s", sb->buf);
		}
		string_list_clear(&olist, 0);

		/* Also include needed rename limit adjustment now */
		diff_warn_rename_limit("merge.renamelimit",
				       opti->renames.needed_limit, 0);

		trace2_region_leave("merge", "display messages", opt->repo);
	}

	merge_finalize(opt, result);
}

void merge_finalize(struct merge_options *opt,
		    struct merge_result *result)
{
	struct merge_options_internal *opti = result->priv;

	if (opt->renormalize)
		git_attr_set_direction(GIT_ATTR_CHECKIN);
	assert(opt->priv == NULL);

	clear_or_reinit_internal_opts(opti, 0);
	FREE_AND_NULL(opti);
}

/*** Function Grouping: helper functions for merge_incore_*() ***/

static struct tree *shift_tree_object(struct repository *repo,
				      struct tree *one, struct tree *two,
				      const char *subtree_shift)
{
	struct object_id shifted;

	if (!*subtree_shift) {
		shift_tree(repo, &one->object.oid, &two->object.oid, &shifted, 0);
	} else {
		shift_tree_by(repo, &one->object.oid, &two->object.oid, &shifted,
			      subtree_shift);
	}
	if (oideq(&two->object.oid, &shifted))
		return two;
	return lookup_tree(repo, &shifted);
}

static inline void set_commit_tree(struct commit *c, struct tree *t)
{
	c->maybe_tree = t;
}

static struct commit *make_virtual_commit(struct repository *repo,
					  struct tree *tree,
					  const char *comment)
{
	struct commit *commit = alloc_commit_node(repo);

	set_merge_remote_desc(commit, comment, (struct object *)commit);
	set_commit_tree(commit, tree);
	commit->object.parsed = 1;
	return commit;
}

static void merge_start(struct merge_options *opt, struct merge_result *result)
{
	struct rename_info *renames;
	int i;

	/* Sanity checks on opt */
	trace2_region_enter("merge", "sanity checks", opt->repo);
	assert(opt->repo);

	assert(opt->branch1 && opt->branch2);

	assert(opt->detect_directory_renames >= MERGE_DIRECTORY_RENAMES_NONE &&
	       opt->detect_directory_renames <= MERGE_DIRECTORY_RENAMES_TRUE);
	assert(opt->rename_limit >= -1);
	assert(opt->rename_score >= 0 && opt->rename_score <= MAX_SCORE);
	assert(opt->show_rename_progress >= 0 && opt->show_rename_progress <= 1);

	assert(opt->xdl_opts >= 0);
	assert(opt->recursive_variant >= MERGE_VARIANT_NORMAL &&
	       opt->recursive_variant <= MERGE_VARIANT_THEIRS);

	/*
	 * detect_renames, verbosity, buffer_output, and obuf are ignored
	 * fields that were used by "recursive" rather than "ort" -- but
	 * sanity check them anyway.
	 */
	assert(opt->detect_renames >= -1 &&
	       opt->detect_renames <= DIFF_DETECT_COPY);
	assert(opt->verbosity >= 0 && opt->verbosity <= 5);
	assert(opt->buffer_output <= 2);
	assert(opt->obuf.len == 0);

	assert(opt->priv == NULL);
	if (result->priv) {
		opt->priv = result->priv;
		result->priv = NULL;
		/*
		 * opt->priv non-NULL means we had results from a previous
		 * run; do a few sanity checks that user didn't mess with
		 * it in an obvious fashion.
		 */
		assert(opt->priv->call_depth == 0);
		assert(!opt->priv->toplevel_dir ||
		       0 == strlen(opt->priv->toplevel_dir));
	}
	trace2_region_leave("merge", "sanity checks", opt->repo);

	/* Default to histogram diff.  Actually, just hardcode it...for now. */
	opt->xdl_opts = DIFF_WITH_ALG(opt, HISTOGRAM_DIFF);

	/* Handle attr direction stuff for renormalization */
	if (opt->renormalize)
		git_attr_set_direction(GIT_ATTR_CHECKOUT);

	/* Initialization of opt->priv, our internal merge data */
	trace2_region_enter("merge", "allocate/init", opt->repo);
	if (opt->priv) {
		clear_or_reinit_internal_opts(opt->priv, 1);
		trace2_region_leave("merge", "allocate/init", opt->repo);
		return;
	}
	opt->priv = xcalloc(1, sizeof(*opt->priv));

	/* Initialization of various renames fields */
	renames = &opt->priv->renames;
	for (i = MERGE_SIDE1; i <= MERGE_SIDE2; i++) {
		strintmap_init_with_options(&renames->dirs_removed[i],
					    NOT_RELEVANT, NULL, 0);
		strmap_init_with_options(&renames->dir_rename_count[i],
					 NULL, 1);
		strmap_init_with_options(&renames->dir_renames[i],
					 NULL, 0);
		strintmap_init_with_options(&renames->relevant_sources[i],
					    0, NULL, 0);
	}

	/*
	 * Although we initialize opt->priv->paths with strdup_strings=0,
	 * that's just to avoid making yet another copy of an allocated
	 * string.  Putting the entry into paths means we are taking
	 * ownership, so we will later free it.  paths_to_free is similar.
	 *
	 * In contrast, conflicted just has a subset of keys from paths, so
	 * we don't want to free those (it'd be a duplicate free).
	 */
	strmap_init_with_options(&opt->priv->paths, NULL, 0);
	strmap_init_with_options(&opt->priv->conflicted, NULL, 0);
	string_list_init(&opt->priv->paths_to_free, 0);

	/*
	 * keys & strbufs in output will sometimes need to outlive "paths",
	 * so it will have a copy of relevant keys.  It's probably a small
	 * subset of the overall paths that have special output.
	 */
	strmap_init(&opt->priv->output);

	trace2_region_leave("merge", "allocate/init", opt->repo);
}

/*** Function Grouping: merge_incore_*() and their internal variants ***/

/*
 * Originally from merge_trees_internal(); heavily adapted, though.
 */
static void merge_ort_nonrecursive_internal(struct merge_options *opt,
					    struct tree *merge_base,
					    struct tree *side1,
					    struct tree *side2,
					    struct merge_result *result)
{
	struct object_id working_tree_oid;

	if (opt->subtree_shift) {
		side2 = shift_tree_object(opt->repo, side1, side2,
					  opt->subtree_shift);
		merge_base = shift_tree_object(opt->repo, side1, merge_base,
					       opt->subtree_shift);
	}

	trace2_region_enter("merge", "collect_merge_info", opt->repo);
	if (collect_merge_info(opt, merge_base, side1, side2) != 0) {
		/*
		 * TRANSLATORS: The %s arguments are: 1) tree hash of a merge
		 * base, and 2-3) the trees for the two trees we're merging.
		 */
		err(opt, _("collecting merge info failed for trees %s, %s, %s"),
		    oid_to_hex(&merge_base->object.oid),
		    oid_to_hex(&side1->object.oid),
		    oid_to_hex(&side2->object.oid));
		result->clean = -1;
		return;
	}
	trace2_region_leave("merge", "collect_merge_info", opt->repo);

	trace2_region_enter("merge", "renames", opt->repo);
	result->clean = detect_and_process_renames(opt, merge_base,
						   side1, side2);
	trace2_region_leave("merge", "renames", opt->repo);

	trace2_region_enter("merge", "process_entries", opt->repo);
	process_entries(opt, &working_tree_oid);
	trace2_region_leave("merge", "process_entries", opt->repo);

	/* Set return values */
	result->tree = parse_tree_indirect(&working_tree_oid);
	/* existence of conflicted entries implies unclean */
	result->clean &= strmap_empty(&opt->priv->conflicted);
	if (!opt->priv->call_depth) {
		result->priv = opt->priv;
		opt->priv = NULL;
	}
}

/*
 * Originally from merge_recursive_internal(); somewhat adapted, though.
 */
static void merge_ort_internal(struct merge_options *opt,
			       struct commit_list *merge_bases,
			       struct commit *h1,
			       struct commit *h2,
			       struct merge_result *result)
{
	struct commit_list *iter;
	struct commit *merged_merge_bases;
	const char *ancestor_name;
	struct strbuf merge_base_abbrev = STRBUF_INIT;

	if (!merge_bases) {
		merge_bases = get_merge_bases(h1, h2);
		/* See merge-ort.h:merge_incore_recursive() declaration NOTE */
		merge_bases = reverse_commit_list(merge_bases);
	}

	merged_merge_bases = pop_commit(&merge_bases);
	if (merged_merge_bases == NULL) {
		/* if there is no common ancestor, use an empty tree */
		struct tree *tree;

		tree = lookup_tree(opt->repo, opt->repo->hash_algo->empty_tree);
		merged_merge_bases = make_virtual_commit(opt->repo, tree,
							 "ancestor");
		ancestor_name = "empty tree";
	} else if (merge_bases) {
		ancestor_name = "merged common ancestors";
	} else {
		strbuf_add_unique_abbrev(&merge_base_abbrev,
					 &merged_merge_bases->object.oid,
					 DEFAULT_ABBREV);
		ancestor_name = merge_base_abbrev.buf;
	}

	for (iter = merge_bases; iter; iter = iter->next) {
		const char *saved_b1, *saved_b2;
		struct commit *prev = merged_merge_bases;

		opt->priv->call_depth++;
		/*
		 * When the merge fails, the result contains files
		 * with conflict markers. The cleanness flag is
		 * ignored (unless indicating an error), it was never
		 * actually used, as result of merge_trees has always
		 * overwritten it: the committed "conflicts" were
		 * already resolved.
		 */
		saved_b1 = opt->branch1;
		saved_b2 = opt->branch2;
		opt->branch1 = "Temporary merge branch 1";
		opt->branch2 = "Temporary merge branch 2";
		merge_ort_internal(opt, NULL, prev, iter->item, result);
		if (result->clean < 0)
			return;
		opt->branch1 = saved_b1;
		opt->branch2 = saved_b2;
		opt->priv->call_depth--;

		merged_merge_bases = make_virtual_commit(opt->repo,
							 result->tree,
							 "merged tree");
		commit_list_insert(prev, &merged_merge_bases->parents);
		commit_list_insert(iter->item,
				   &merged_merge_bases->parents->next);

		clear_or_reinit_internal_opts(opt->priv, 1);
	}

	opt->ancestor = ancestor_name;
	merge_ort_nonrecursive_internal(opt,
					repo_get_commit_tree(opt->repo,
							     merged_merge_bases),
					repo_get_commit_tree(opt->repo, h1),
					repo_get_commit_tree(opt->repo, h2),
					result);
	strbuf_release(&merge_base_abbrev);
	opt->ancestor = NULL;  /* avoid accidental re-use of opt->ancestor */
}

void merge_incore_nonrecursive(struct merge_options *opt,
			       struct tree *merge_base,
			       struct tree *side1,
			       struct tree *side2,
			       struct merge_result *result)
{
	trace2_region_enter("merge", "incore_nonrecursive", opt->repo);

	trace2_region_enter("merge", "merge_start", opt->repo);
	assert(opt->ancestor != NULL);
	merge_start(opt, result);
	trace2_region_leave("merge", "merge_start", opt->repo);

	merge_ort_nonrecursive_internal(opt, merge_base, side1, side2, result);
	trace2_region_leave("merge", "incore_nonrecursive", opt->repo);
}

void merge_incore_recursive(struct merge_options *opt,
			    struct commit_list *merge_bases,
			    struct commit *side1,
			    struct commit *side2,
			    struct merge_result *result)
{
	trace2_region_enter("merge", "incore_recursive", opt->repo);

	/* We set the ancestor label based on the merge_bases */
	assert(opt->ancestor == NULL);

	trace2_region_enter("merge", "merge_start", opt->repo);
	merge_start(opt, result);
	trace2_region_leave("merge", "merge_start", opt->repo);

	merge_ort_internal(opt, merge_bases, side1, side2, result);
	trace2_region_leave("merge", "incore_recursive", opt->repo);
}
