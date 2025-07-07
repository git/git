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

#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "merge-ort.h"

#include "alloc.h"
#include "advice.h"
#include "attr.h"
#include "cache-tree.h"
#include "commit.h"
#include "commit-reach.h"
#include "config.h"
#include "diff.h"
#include "diffcore.h"
#include "dir.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "entry.h"
#include "merge-ll.h"
#include "match-trees.h"
#include "mem-pool.h"
#include "object-file.h"
#include "object-name.h"
#include "odb.h"
#include "oid-array.h"
#include "path.h"
#include "promisor-remote.h"
#include "read-cache-ll.h"
#include "refs.h"
#include "revision.h"
#include "sparse-index.h"
#include "strmap.h"
#include "trace2.h"
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

static unsigned RESULT_INITIALIZED = 0x1abe11ed; /* unlikely accidental value */

struct traversal_callback_data {
	unsigned long mask;
	unsigned long dirmask;
	struct name_entry names[3];
};

struct deferred_traversal_data {
	/*
	 * possible_trivial_merges: directories to be explored only when needed
	 *
	 * possible_trivial_merges is a map of directory names to
	 * dir_rename_mask.  When we detect that a directory is unchanged on
	 * one side, we can sometimes resolve the directory without recursing
	 * into it.  Renames are the only things that can prevent such an
	 * optimization.  However, for rename sources:
	 *   - If no parent directory needed directory rename detection, then
	 *     no path under such a directory can be a relevant_source.
	 * and for rename destinations:
	 *   - If no cached rename has a target path under the directory AND
	 *   - If there are no unpaired relevant_sources elsewhere in the
	 *     repository
	 * then we don't need any path under this directory for a rename
	 * destination.  The only way to know the last item above is to defer
	 * handling such directories until the end of collect_merge_info(),
	 * in handle_deferred_entries().
	 *
	 * For each we store dir_rename_mask, since that's the only bit of
	 * information we need, other than the path, to resume the recursive
	 * traversal.
	 */
	struct strintmap possible_trivial_merges;

	/*
	 * trivial_merges_okay: if trivial directory merges are okay
	 *
	 * See possible_trivial_merges above.  The "no unpaired
	 * relevant_sources elsewhere in the repository" is a single boolean
	 * per merge side, which we store here.  Note that while 0 means no,
	 * 1 only means "maybe" rather than "yes"; we optimistically set it
	 * to 1 initially and only clear when we determine it is unsafe to
	 * do trivial directory merges.
	 */
	unsigned trivial_merges_okay;

	/*
	 * target_dirs: ancestor directories of rename targets
	 *
	 * target_dirs contains all directory names that are an ancestor of
	 * any rename destination.
	 */
	struct strset target_dirs;
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

	struct deferred_traversal_data deferred[3];

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
	 * merge_trees: trees passed to the merge algorithm for the merge
	 *
	 * merge_trees records the trees passed to the merge algorithm.  But,
	 * this data also is stored in merge_result->priv.  If a sequence of
	 * merges are being done (such as when cherry-picking or rebasing),
	 * the next merge can look at this and re-use information from
	 * previous merges under certain circumstances.
	 *
	 * See also all the cached_* variables.
	 */
	struct tree *merge_trees[3];

	/*
	 * cached_pairs_valid_side: which side's cached info can be reused
	 *
	 * See the description for merge_trees.  For repeated merges, at most
	 * only one side's cached information can be used.  Valid values:
	 *   MERGE_SIDE2: cached data from side2 can be reused
	 *   MERGE_SIDE1: cached data from side1 can be reused
	 *   0:           no cached data can be reused
	 *   -1:          See redo_after_renames; both sides can be reused.
	 */
	int cached_pairs_valid_side;

	/*
	 * cached_pairs: Caching of renames and deletions.
	 *
	 * These are mappings recording renames and deletions of individual
	 * files (not directories).  They are thus a map from an old
	 * filename to either NULL (for deletions) or a new filename (for
	 * renames).
	 */
	struct strmap cached_pairs[3];

	/*
	 * cached_target_names: just the destinations from cached_pairs
	 *
	 * We sometimes want a fast lookup to determine if a given filename
	 * is one of the destinations in cached_pairs.  cached_target_names
	 * is thus duplicative information, but it provides a fast lookup.
	 */
	struct strset cached_target_names[3];

	/*
	 * cached_irrelevant: Caching of rename_sources that aren't relevant.
	 *
	 * If we try to detect a rename for a source path and succeed, it's
	 * part of a rename.  If we try to detect a rename for a source path
	 * and fail, then it's a delete.  If we do not try to detect a rename
	 * for a path, then we don't know if it's a rename or a delete.  If
	 * merge-ort doesn't think the path is relevant, then we just won't
	 * cache anything for that path.  But there's a slight problem in
	 * that merge-ort can think a path is RELEVANT_LOCATION, but due to
	 * commit 9bd342137e ("diffcore-rename: determine which
	 * relevant_sources are no longer relevant", 2021-03-13),
	 * diffcore-rename can downgrade the path to RELEVANT_NO_MORE.  To
	 * avoid excessive calls to diffcore_rename_extended() we still need
	 * to cache such paths, though we cannot record them as either
	 * renames or deletes.  So we cache them here as a "turned out to be
	 * irrelevant *for this commit*" as they are often also irrelevant
	 * for subsequent commits, though we will have to do some extra
	 * checking to see whether such paths become relevant for rename
	 * detection when cherry-picking/rebasing subsequent commits.
	 */
	struct strset cached_irrelevant[3];

	/*
	 * redo_after_renames: optimization flag for "restarting" the merge
	 *
	 * Sometimes it pays to detect renames, cache them, and then
	 * restart the merge operation from the beginning.  The reason for
	 * this is that when we know where all the renames are, we know
	 * whether a certain directory has any paths under it affected --
	 * and if a directory is not affected then it permits us to do
	 * trivial tree merging in more cases.  Doing trivial tree merging
	 * prevents the need to run process_entry() on every path
	 * underneath trees that can be trivially merged, and
	 * process_entry() is more expensive than collect_merge_info() --
	 * plus, the second collect_merge_info() will be much faster since
	 * it doesn't have to recurse into the relevant trees.
	 *
	 * Values for this flag:
	 *   0 = don't bother, not worth it (or conditions not yet checked)
	 *   1 = conditions for optimization met, optimization worthwhile
	 *   2 = we already did it (don't restart merge yet again)
	 */
	unsigned redo_after_renames;

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
	 * pool: memory pool for fast allocation/deallocation
	 *
	 * We allocate room for lots of filenames and auxiliary data
	 * structures in merge_options_internal, and it tends to all be
	 * freed together too.  Using a memory pool for these provides a
	 * nice speedup.
	 */
	struct mem_pool pool;

	/*
	 * conflicts: logical conflicts and messages stored by _primary_ path
	 *
	 * This is a map of pathnames (a subset of the keys in "paths" above)
	 * to struct string_list, with each item's `util` containing a
	 * `struct logical_conflict_info`. Note, though, that for each path,
	 * it only stores the logical conflicts for which that path is the
	 * primary path; the path might be part of additional conflicts.
	 */
	struct strmap conflicts;

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

	/* field that holds submodule conflict information */
	struct string_list conflicted_submodules;
};

struct conflicted_submodule_item {
	char *abbrev;
	int flag;
};

static void conflicted_submodule_item_free(void *util, const char *str UNUSED)
{
	struct conflicted_submodule_item *item = util;

	free(item->abbrev);
	free(item);
}

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

enum conflict_and_info_types {
	/* "Simple" conflicts and informational messages */
	INFO_AUTO_MERGING = 0,
	CONFLICT_CONTENTS,       /* text file that failed to merge */
	CONFLICT_BINARY,
	CONFLICT_FILE_DIRECTORY,
	CONFLICT_DISTINCT_MODES,
	CONFLICT_MODIFY_DELETE,

	/* Regular rename */
	CONFLICT_RENAME_RENAME,   /* same file renamed differently */
	CONFLICT_RENAME_COLLIDES, /* rename/add or two files renamed to 1 */
	CONFLICT_RENAME_DELETE,

	/* Basic directory rename */
	CONFLICT_DIR_RENAME_SUGGESTED,
	INFO_DIR_RENAME_APPLIED,

	/* Special directory rename cases */
	INFO_DIR_RENAME_SKIPPED_DUE_TO_RERENAME,
	CONFLICT_DIR_RENAME_FILE_IN_WAY,
	CONFLICT_DIR_RENAME_COLLISION,
	CONFLICT_DIR_RENAME_SPLIT,

	/* Basic submodule */
	INFO_SUBMODULE_FAST_FORWARDING,
	CONFLICT_SUBMODULE_FAILED_TO_MERGE,

	/* Special submodule cases broken out from FAILED_TO_MERGE */
	CONFLICT_SUBMODULE_FAILED_TO_MERGE_BUT_POSSIBLE_RESOLUTION,
	CONFLICT_SUBMODULE_NOT_INITIALIZED,
	CONFLICT_SUBMODULE_HISTORY_NOT_AVAILABLE,
	CONFLICT_SUBMODULE_MAY_HAVE_REWINDS,
	CONFLICT_SUBMODULE_NULL_MERGE_BASE,

	/* INSERT NEW ENTRIES HERE */

	/*
	 * Keep this entry after all regular conflict and info types; only
	 * errors (failures causing immediate abort of the merge) should
	 * come after this.
	 */
	NB_REGULAR_CONFLICT_TYPES,

	/*
	 * Something is seriously wrong; cannot even perform merge;
	 * Keep this group _last_ other than NB_TOTAL_TYPES
	 */
	ERROR_SUBMODULE_CORRUPT,
	ERROR_THREEWAY_CONTENT_MERGE_FAILED,
	ERROR_OBJECT_WRITE_FAILED,
	ERROR_OBJECT_READ_FAILED,
	ERROR_OBJECT_NOT_A_BLOB,

	/* Keep this entry _last_ in the list */
	NB_TOTAL_TYPES,
};

/*
 * Short description of conflict type, relied upon by external tools.
 *
 * We can add more entries, but DO NOT change any of these strings.  Also,
 * please ensure the order matches what is used in conflict_info_and_types.
 */
static const char *type_short_descriptions[] = {
	/*** "Simple" conflicts and informational messages ***/
	[INFO_AUTO_MERGING] = "Auto-merging",
	[CONFLICT_CONTENTS] = "CONFLICT (contents)",
	[CONFLICT_BINARY] = "CONFLICT (binary)",
	[CONFLICT_FILE_DIRECTORY] = "CONFLICT (file/directory)",
	[CONFLICT_DISTINCT_MODES] = "CONFLICT (distinct modes)",
	[CONFLICT_MODIFY_DELETE] = "CONFLICT (modify/delete)",

	/*** Regular rename ***/
	[CONFLICT_RENAME_RENAME] = "CONFLICT (rename/rename)",
	[CONFLICT_RENAME_COLLIDES] = "CONFLICT (rename involved in collision)",
	[CONFLICT_RENAME_DELETE] = "CONFLICT (rename/delete)",

	/*** Basic directory rename ***/
	[CONFLICT_DIR_RENAME_SUGGESTED] =
		"CONFLICT (directory rename suggested)",
	[INFO_DIR_RENAME_APPLIED] = "Path updated due to directory rename",

	/*** Special directory rename cases ***/
	[INFO_DIR_RENAME_SKIPPED_DUE_TO_RERENAME] =
		"Directory rename skipped since directory was renamed on both sides",
	[CONFLICT_DIR_RENAME_FILE_IN_WAY] =
		"CONFLICT (file in way of directory rename)",
	[CONFLICT_DIR_RENAME_COLLISION] = "CONFLICT(directory rename collision)",
	[CONFLICT_DIR_RENAME_SPLIT] = "CONFLICT(directory rename unclear split)",

	/*** Basic submodule ***/
	[INFO_SUBMODULE_FAST_FORWARDING] = "Fast forwarding submodule",
	[CONFLICT_SUBMODULE_FAILED_TO_MERGE] = "CONFLICT (submodule)",

	/*** Special submodule cases broken out from FAILED_TO_MERGE ***/
	[CONFLICT_SUBMODULE_FAILED_TO_MERGE_BUT_POSSIBLE_RESOLUTION] =
		"CONFLICT (submodule with possible resolution)",
	[CONFLICT_SUBMODULE_NOT_INITIALIZED] =
		"CONFLICT (submodule not initialized)",
	[CONFLICT_SUBMODULE_HISTORY_NOT_AVAILABLE] =
		"CONFLICT (submodule history not available)",
	[CONFLICT_SUBMODULE_MAY_HAVE_REWINDS] =
		"CONFLICT (submodule may have rewinds)",
	[CONFLICT_SUBMODULE_NULL_MERGE_BASE] =
		"CONFLICT (submodule lacks merge base)",

	/* Something is seriously wrong; cannot even perform merge */
	[ERROR_SUBMODULE_CORRUPT] =
		"ERROR (submodule corrupt)",
	[ERROR_THREEWAY_CONTENT_MERGE_FAILED] =
		"ERROR (three-way content merge failed)",
	[ERROR_OBJECT_WRITE_FAILED] =
		"ERROR (object write failed)",
	[ERROR_OBJECT_READ_FAILED] =
		"ERROR (object read failed)",
	[ERROR_OBJECT_NOT_A_BLOB] =
		"ERROR (object is not a blob)",
};

struct logical_conflict_info {
	enum conflict_and_info_types type;
	struct strvec paths;
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
	void (*strmap_clear_func)(struct strmap *, int) =
		reinitialize ? strmap_partial_clear : strmap_clear;
	void (*strintmap_clear_func)(struct strintmap *) =
		reinitialize ? strintmap_partial_clear : strintmap_clear;
	void (*strset_clear_func)(struct strset *) =
		reinitialize ? strset_partial_clear : strset_clear;

	strmap_clear_func(&opti->paths, 0);

	/*
	 * All keys and values in opti->conflicted are a subset of those in
	 * opti->paths.  We don't want to deallocate anything twice, so we
	 * don't free the keys and we pass 0 for free_values.
	 */
	strmap_clear_func(&opti->conflicted, 0);

	discard_index(&opti->attr_index);

	/* Free memory used by various renames maps */
	for (i = MERGE_SIDE1; i <= MERGE_SIDE2; ++i) {
		strintmap_clear_func(&renames->dirs_removed[i]);
		strmap_clear_func(&renames->dir_renames[i], 0);
		strintmap_clear_func(&renames->relevant_sources[i]);
		if (!reinitialize)
			assert(renames->cached_pairs_valid_side == 0);
		if (i != renames->cached_pairs_valid_side &&
		    -1 != renames->cached_pairs_valid_side) {
			strset_clear_func(&renames->cached_target_names[i]);
			strmap_clear_func(&renames->cached_pairs[i], 1);
			strset_clear_func(&renames->cached_irrelevant[i]);
			partial_clear_dir_rename_count(&renames->dir_rename_count[i]);
			if (!reinitialize)
				strmap_clear(&renames->dir_rename_count[i], 1);
		}
	}
	for (i = MERGE_SIDE1; i <= MERGE_SIDE2; ++i) {
		strintmap_clear_func(&renames->deferred[i].possible_trivial_merges);
		strset_clear_func(&renames->deferred[i].target_dirs);
		renames->deferred[i].trivial_merges_okay = 1; /* 1 == maybe */
	}
	renames->cached_pairs_valid_side = 0;
	renames->dir_rename_mask = 0;

	if (!reinitialize) {
		struct hashmap_iter iter;
		struct strmap_entry *e;

		/* Release and free each strbuf found in output */
		strmap_for_each_entry(&opti->conflicts, &iter, e) {
			struct string_list *list = e->value;
			for (int i = 0; i < list->nr; i++) {
				struct logical_conflict_info *info =
					list->items[i].util;
				strvec_clear(&info->paths);
			}
			/*
			 * While strictly speaking we don't need to
			 * free(conflicts) here because we could pass
			 * free_values=1 when calling strmap_clear() on
			 * opti->conflicts, that would require strmap_clear
			 * to do another strmap_for_each_entry() loop, so we
			 * just free it while we're iterating anyway.
			 */
			string_list_clear(list, 1);
			free(list);
		}
		strmap_clear(&opti->conflicts, 0);
	}

	mem_pool_discard(&opti->pool, 0);

	string_list_clear_func(&opti->conflicted_submodules,
					conflicted_submodule_item_free);

	/* Clean out callback_data as well. */
	FREE_AND_NULL(renames->callback_data);
	renames->callback_data_nr = renames->callback_data_alloc = 0;
}

static void format_commit(struct strbuf *sb,
			  int indent,
			  struct repository *repo,
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

	repo_format_commit_message(repo, commit, "%h %s", sb, &ctx);
	strbuf_addch(sb, '\n');
}

__attribute__((format (printf, 8, 9)))
static void path_msg(struct merge_options *opt,
		     enum conflict_and_info_types type,
		     int omittable_hint, /* skippable under --remerge-diff */
		     const char *primary_path,
		     const char *other_path_1, /* may be NULL */
		     const char *other_path_2, /* may be NULL */
		     struct string_list *other_paths, /* may be NULL */
		     const char *fmt, ...)
{
	va_list ap;
	struct string_list *path_conflicts;
	struct logical_conflict_info *info;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf *dest;
	struct strbuf tmp = STRBUF_INIT;

	/* Sanity checks */
	ASSERT(omittable_hint ==
	       (!starts_with(type_short_descriptions[type], "CONFLICT") &&
		!starts_with(type_short_descriptions[type], "ERROR")) ||
	       type == CONFLICT_DIR_RENAME_SUGGESTED);
	if (opt->record_conflict_msgs_as_headers && omittable_hint)
		return; /* Do not record mere hints in headers */
	if (opt->priv->call_depth && opt->verbosity < 5)
		return; /* Ignore messages from inner merges */

	/* Ensure path_conflicts (ptr to array of logical_conflict) allocated */
	path_conflicts = strmap_get(&opt->priv->conflicts, primary_path);
	if (!path_conflicts) {
		path_conflicts = xmalloc(sizeof(*path_conflicts));
		string_list_init_dup(path_conflicts);
		strmap_put(&opt->priv->conflicts, primary_path, path_conflicts);
	}

	/* Add a logical_conflict at the end to store info from this call */
	info = xcalloc(1, sizeof(*info));
	info->type = type;
	strvec_init(&info->paths);

	/* Handle the list of paths */
	strvec_push(&info->paths, primary_path);
	if (other_path_1)
		strvec_push(&info->paths, other_path_1);
	if (other_path_2)
		strvec_push(&info->paths, other_path_2);
	if (other_paths)
		for (int i = 0; i < other_paths->nr; i++)
		strvec_push(&info->paths, other_paths->items[i].string);

	/* Handle message and its format, in normal case */
	dest = (opt->record_conflict_msgs_as_headers ? &tmp : &buf);

	va_start(ap, fmt);
	if (opt->priv->call_depth) {
		strbuf_addchars(dest, ' ', 2);
		strbuf_addstr(dest, "From inner merge:");
		strbuf_addchars(dest, ' ', opt->priv->call_depth * 2);
	}
	strbuf_vaddf(dest, fmt, ap);
	va_end(ap);

	/* Handle specialized formatting of message under --remerge-diff */
	if (opt->record_conflict_msgs_as_headers) {
		int i_sb = 0, i_tmp = 0;

		/* Start with the specified prefix */
		if (opt->msg_header_prefix)
			strbuf_addf(&buf, "%s ", opt->msg_header_prefix);

		/* Copy tmp to sb, adding spaces after newlines */
		strbuf_grow(&buf, buf.len + 2*tmp.len); /* more than sufficient */
		for (; i_tmp < tmp.len; i_tmp++, i_sb++) {
			/* Copy next character from tmp to sb */
			buf.buf[buf.len + i_sb] = tmp.buf[i_tmp];

			/* If we copied a newline, add a space */
			if (tmp.buf[i_tmp] == '\n')
				buf.buf[++i_sb] = ' ';
		}
		/* Update length and ensure it's NUL-terminated */
		buf.len += i_sb;
		buf.buf[buf.len] = '\0';

		strbuf_release(&tmp);
	}
	string_list_append_nodup(path_conflicts, strbuf_detach(&buf, NULL))
		->util = info;
}

static struct diff_filespec *pool_alloc_filespec(struct mem_pool *pool,
						 const char *path)
{
	/* Similar to alloc_filespec(), but allocate from pool and reuse path */
	struct diff_filespec *spec;

	spec = mem_pool_calloc(pool, 1, sizeof(*spec));
	spec->path = (char*)path; /* spec won't modify it */

	spec->count = 1;
	spec->is_binary = -1;
	return spec;
}

static struct diff_filepair *pool_diff_queue(struct mem_pool *pool,
					     struct diff_queue_struct *queue,
					     struct diff_filespec *one,
					     struct diff_filespec *two)
{
	/* Same code as diff_queue(), except allocate from pool */
	struct diff_filepair *dp;

	dp = mem_pool_calloc(pool, 1, sizeof(*dp));
	dp->one = one;
	dp->two = two;
	if (queue)
		diff_q(queue, dp);
	return dp;
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

static char *unique_path(struct merge_options *opt,
			 const char *path,
			 const char *branch)
{
	char *ret = NULL;
	struct strbuf newpath = STRBUF_INIT;
	int suffix = 0;
	size_t base_len;
	struct strmap *existing_paths = &opt->priv->paths;

	strbuf_addf(&newpath, "%s~", path);
	add_flattened_path(&newpath, branch);

	base_len = newpath.len;
	while (strmap_contains(existing_paths, newpath.buf)) {
		strbuf_setlen(&newpath, base_len);
		strbuf_addf(&newpath, "_%d", suffix++);
	}

	/* Track the new path in our memory pool */
	ret = mem_pool_alloc(&opt->priv->pool, newpath.len + 1);
	memcpy(ret, newpath.buf, newpath.len + 1);
	strbuf_release(&newpath);
	return ret;
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

	mi = mem_pool_calloc(&opt->priv->pool, 1,
			     resolved ? sizeof(struct merged_info) :
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

	if (is_add) {
		assert(match_mask == 0 || match_mask == 6);
		if (strset_contains(&renames->cached_target_names[side],
				    pathname))
			return;
	} else {
		unsigned content_relevant = (match_mask == 0);
		unsigned location_relevant = (dir_rename_mask == 0x07);

		assert(match_mask == 0 || match_mask == 3 || match_mask == 5);

		/*
		 * If pathname is found in cached_irrelevant[side] due to
		 * previous pick but for this commit content is relevant,
		 * then we need to remove it from cached_irrelevant.
		 */
		if (content_relevant)
			/* strset_remove is no-op if strset doesn't have key */
			strset_remove(&renames->cached_irrelevant[side],
				      pathname);

		/*
		 * We do not need to re-detect renames for paths that we already
		 * know the pairing, i.e. for cached_pairs (or
		 * cached_irrelevant).  However, handle_deferred_entries() needs
		 * to loop over the union of keys from relevant_sources[side] and
		 * cached_pairs[side], so for simplicity we set relevant_sources
		 * for all the cached_pairs too and then strip them back out in
		 * prune_cached_from_relevant() at the beginning of
		 * detect_regular_renames().
		 */
		if (content_relevant || location_relevant) {
			/* content_relevant trumps location_relevant */
			strintmap_set(&renames->relevant_sources[side], pathname,
				      content_relevant ? RELEVANT_CONTENT : RELEVANT_LOCATION);
		}

		/*
		 * Avoid creating pair if we've already cached rename results.
		 * Note that we do this after setting relevant_sources[side]
		 * as noted in the comment above.
		 */
		if (strmap_contains(&renames->cached_pairs[side], pathname) ||
		    strset_contains(&renames->cached_irrelevant[side], pathname))
			return;
	}

	one = pool_alloc_filespec(&opt->priv->pool, pathname);
	two = pool_alloc_filespec(&opt->priv->pool, pathname);
	fill_filespec(is_add ? two : one,
		      &names[names_idx].oid, 1, names[names_idx].mode);
	pool_diff_queue(&opt->priv->pool, &renames->pairs[side], one, two);
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
	 * detection may be relevant.  Basically, whenever a directory is
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
	fullpath = mem_pool_alloc(&opt->priv->pool, len + 1);
	make_traverse_path(fullpath, len + 1, info, p->path, p->pathlen);

	/*
	 * If mbase, side1, and side2 all match, we can resolve early.  Even
	 * if these are trees, there will be no renames or anything
	 * underneath.
	 */
	if (side1_matches_mbase && side2_matches_mbase) {
		/* mbase, side1, & side2 all match; use mbase as resolution */
		setup_path_info(opt, &pi, dirname, info->pathlen, fullpath,
				names, names+0, mbase_null, 0 /* df_conflict */,
				filemask, dirmask, 1 /* resolved */);
		return mask;
	}

	/*
	 * If the sides match, and all three paths are present and are
	 * files, then we can take either as the resolution.  We can't do
	 * this with trees, because there may be rename sources from the
	 * merge_base.
	 */
	if (sides_match && filemask == 0x07) {
		/* use side1 (== side2) version as resolution */
		setup_path_info(opt, &pi, dirname, info->pathlen, fullpath,
				names, names+1, side1_null, 0,
				filemask, dirmask, 1);
		return mask;
	}

	/*
	 * If side1 matches mbase and all three paths are present and are
	 * files, then we can use side2 as the resolution.  We cannot
	 * necessarily do so this for trees, because there may be rename
	 * destinations within side2.
	 */
	if (side1_matches_mbase && filemask == 0x07) {
		/* use side2 version as resolution */
		setup_path_info(opt, &pi, dirname, info->pathlen, fullpath,
				names, names+2, side2_null, 0,
				filemask, dirmask, 1);
		return mask;
	}

	/* Similar to above but swapping sides 1 and 2 */
	if (side2_matches_mbase && filemask == 0x07) {
		/* use side1 version as resolution */
		setup_path_info(opt, &pi, dirname, info->pathlen, fullpath,
				names, names+1, side1_null, 0,
				filemask, dirmask, 1);
		return mask;
	}

	/*
	 * Sometimes we can tell that a source path need not be included in
	 * rename detection -- namely, whenever either
	 *    side1_matches_mbase && side2_null
	 * or
	 *    side2_matches_mbase && side1_null
	 * However, we call collect_rename_info() even in those cases,
	 * because exact renames are cheap and would let us remove both a
	 * source and destination path.  We'll cull the unneeded sources
	 * later.
	 */
	collect_rename_info(opt, names, dirname, fullpath,
			    filemask, dirmask, match_mask);

	/*
	 * None of the special cases above matched, so we have a
	 * provisional conflict.  (Rename detection might allow us to
	 * unconflict some more cases, but that comes later so all we can
	 * do now is record the different non-null file hashes.)
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
		int i, ret, side;

		/*
		 * Check for whether we can avoid recursing due to one side
		 * matching the merge base.  The side that does NOT match is
		 * the one that might have a rename destination we need.
		 */
		assert(!side1_matches_mbase || !side2_matches_mbase);
		side = side1_matches_mbase ? MERGE_SIDE2 :
			side2_matches_mbase ? MERGE_SIDE1 : MERGE_BASE;
		if (filemask == 0 && (dirmask == 2 || dirmask == 4)) {
			/*
			 * Also defer recursing into new directories; set up a
			 * few variables to let us do so.
			 */
			ci->match_mask = (7 - dirmask);
			side = dirmask / 2;
		}
		if (renames->dir_rename_mask != 0x07 &&
		    side != MERGE_BASE &&
		    renames->deferred[side].trivial_merges_okay &&
		    !strset_contains(&renames->deferred[side].target_dirs,
				     pi.string)) {
			strintmap_set(&renames->deferred[side].possible_trivial_merges,
				      pi.string, renames->dir_rename_mask);
			renames->dir_rename_mask = prev_dir_rename_mask;
			return mask;
		}

		/* We need to recurse */
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

static void resolve_trivial_directory_merge(struct conflict_info *ci, int side)
{
	VERIFY_CI(ci);
	assert((side == 1 && ci->match_mask == 5) ||
	       (side == 2 && ci->match_mask == 3));
	oidcpy(&ci->merged.result.oid, &ci->stages[side].oid);
	ci->merged.result.mode = ci->stages[side].mode;
	ci->merged.is_null = is_null_oid(&ci->stages[side].oid);
	ci->match_mask = 0;
	ci->merged.clean = 1; /* (ci->filemask == 0); */
}

static int handle_deferred_entries(struct merge_options *opt,
				   struct traverse_info *info)
{
	struct rename_info *renames = &opt->priv->renames;
	struct hashmap_iter iter;
	struct strmap_entry *entry;
	int side, ret = 0;
	int path_count_before, path_count_after = 0;

	path_count_before = strmap_get_size(&opt->priv->paths);
	for (side = MERGE_SIDE1; side <= MERGE_SIDE2; side++) {
		unsigned optimization_okay = 1;
		struct strintmap copy;

		/* Loop over the set of paths we need to know rename info for */
		strintmap_for_each_entry(&renames->relevant_sources[side],
					 &iter, entry) {
			char *rename_target, *dir, *dir_marker;
			struct strmap_entry *e;

			/*
			 * If we don't know delete/rename info for this path,
			 * then we need to recurse into all trees to get all
			 * adds to make sure we have it.
			 */
			if (strset_contains(&renames->cached_irrelevant[side],
					    entry->key))
				continue;
			e = strmap_get_entry(&renames->cached_pairs[side],
					     entry->key);
			if (!e) {
				optimization_okay = 0;
				break;
			}

			/* If this is a delete, we have enough info already */
			rename_target = e->value;
			if (!rename_target)
				continue;

			/* If we already walked the rename target, we're good */
			if (strmap_contains(&opt->priv->paths, rename_target))
				continue;

			/*
			 * Otherwise, we need to get a list of directories that
			 * will need to be recursed into to get this
			 * rename_target.
			 */
			dir = xstrdup(rename_target);
			while ((dir_marker = strrchr(dir, '/'))) {
				*dir_marker = '\0';
				if (strset_contains(&renames->deferred[side].target_dirs,
						    dir))
					break;
				strset_add(&renames->deferred[side].target_dirs,
					   dir);
			}
			free(dir);
		}
		renames->deferred[side].trivial_merges_okay = optimization_okay;
		/*
		 * We need to recurse into any directories in
		 * possible_trivial_merges[side] found in target_dirs[side].
		 * But when we recurse, we may need to queue up some of the
		 * subdirectories for possible_trivial_merges[side].  Since
		 * we can't safely iterate through a hashmap while also adding
		 * entries, move the entries into 'copy', iterate over 'copy',
		 * and then we'll also iterate anything added into
		 * possible_trivial_merges[side] once this loop is done.
		 */
		copy = renames->deferred[side].possible_trivial_merges;
		strintmap_init_with_options(&renames->deferred[side].possible_trivial_merges,
					    0,
					    &opt->priv->pool,
					    0);
		strintmap_for_each_entry(&copy, &iter, entry) {
			const char *path = entry->key;
			unsigned dir_rename_mask = (intptr_t)entry->value;
			struct conflict_info *ci;
			unsigned dirmask;
			struct tree_desc t[3];
			void *buf[3] = {NULL,};
			int i;

			ci = strmap_get(&opt->priv->paths, path);
			VERIFY_CI(ci);
			dirmask = ci->dirmask;

			if (optimization_okay &&
			    !strset_contains(&renames->deferred[side].target_dirs,
					     path)) {
				resolve_trivial_directory_merge(ci, side);
				continue;
			}

			info->name = path;
			info->namelen = strlen(path);
			info->pathlen = info->namelen + 1;

			for (i = 0; i < 3; i++, dirmask >>= 1) {
				if (i == 1 && ci->match_mask == 3)
					t[1] = t[0];
				else if (i == 2 && ci->match_mask == 5)
					t[2] = t[0];
				else if (i == 2 && ci->match_mask == 6)
					t[2] = t[1];
				else {
					const struct object_id *oid = NULL;
					if (dirmask & 1)
						oid = &ci->stages[i].oid;
					buf[i] = fill_tree_descriptor(opt->repo,
								      t+i, oid);
				}
			}

			ci->match_mask &= ci->filemask;
			opt->priv->current_dir_name = path;
			renames->dir_rename_mask = dir_rename_mask;
			if (renames->dir_rename_mask == 0 ||
			    renames->dir_rename_mask == 0x07)
				ret = traverse_trees(NULL, 3, t, info);
			else
				ret = traverse_trees_wrapper(NULL, 3, t, info);

			for (i = MERGE_BASE; i <= MERGE_SIDE2; i++)
				free(buf[i]);

			if (ret < 0)
				return ret;
		}
		strintmap_clear(&copy);
		strintmap_for_each_entry(&renames->deferred[side].possible_trivial_merges,
					 &iter, entry) {
			const char *path = entry->key;
			struct conflict_info *ci;

			ci = strmap_get(&opt->priv->paths, path);
			VERIFY_CI(ci);

			ASSERT(renames->deferred[side].trivial_merges_okay &&
			       !strset_contains(&renames->deferred[side].target_dirs,
						path));
			resolve_trivial_directory_merge(ci, side);
		}
		if (!optimization_okay || path_count_after)
			path_count_after = strmap_get_size(&opt->priv->paths);
	}
	if (path_count_after) {
		/*
		 * The choice of wanted_factor here does not affect
		 * correctness, only performance.  When the
		 *    path_count_after / path_count_before
		 * ratio is high, redoing after renames is a big
		 * performance boost.  I suspect that redoing is a wash
		 * somewhere near a value of 2, and below that redoing will
		 * slow things down.  I applied a fudge factor and picked
		 * 3; see the commit message when this was introduced for
		 * back of the envelope calculations for this ratio.
		 */
		const int wanted_factor = 3;

		/* We should only redo collect_merge_info one time */
		assert(renames->redo_after_renames == 0);

		if (path_count_after / path_count_before >= wanted_factor) {
			renames->redo_after_renames = 1;
			renames->cached_pairs_valid_side = -1;
		}
	} else if (renames->redo_after_renames == 2)
		renames->redo_after_renames = 0;
	return ret;
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

	if (parse_tree(merge_base) < 0 ||
	    parse_tree(side1) < 0 ||
	    parse_tree(side2) < 0)
		return -1;
	init_tree_desc(t + 0, &merge_base->object.oid,
		       merge_base->buffer, merge_base->size);
	init_tree_desc(t + 1, &side1->object.oid, side1->buffer, side1->size);
	init_tree_desc(t + 2, &side2->object.oid, side2->buffer, side2->size);

	trace2_region_enter("merge", "traverse_trees", opt->repo);
	ret = traverse_trees(NULL, 3, t, &info);
	if (ret == 0)
		ret = handle_deferred_entries(opt, &info);
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
	/* FIXME: can't handle linked worktrees in submodules yet */
	revs.single_worktree = path != NULL;
	setup_revisions(ARRAY_SIZE(rev_args)-1, rev_args, &revs, &rev_opts);

	/* save all revisions from the above list that contain b */
	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	while ((commit = get_revision(&revs)) != NULL) {
		struct object *o = &(commit->object);
		int ret = repo_in_merge_bases(repo, b, commit);

		if (ret < 0) {
			object_array_clear(&merges);
			release_revisions(&revs);
			return ret;
		}
		if (ret > 0)
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
			if (i != j) {
				int ret = repo_in_merge_bases(repo, m2, m1);
				if (ret < 0) {
					object_array_clear(&merges);
					release_revisions(&revs);
					return ret;
				}
				if (ret > 0) {
					contains_another = 1;
					break;
				}
			}
		}

		if (!contains_another)
			add_object_array(merges.objects[i].item, NULL, result);
	}

	object_array_clear(&merges);
	release_revisions(&revs);
	return result->nr;
}

static int merge_submodule(struct merge_options *opt,
			   const char *path,
			   const struct object_id *o,
			   const struct object_id *a,
			   const struct object_id *b,
			   struct object_id *result)
{
	struct repository subrepo;
	struct strbuf sb = STRBUF_INIT;
	int ret = 0, ret2;
	struct commit *commit_o, *commit_a, *commit_b;
	int parent_count;
	struct object_array merges;

	int i;
	int search = !opt->priv->call_depth;
	int sub_not_initialized = 1;
	int sub_flag = CONFLICT_SUBMODULE_FAILED_TO_MERGE;

	/* store fallback answer in result in case we fail */
	oidcpy(result, opt->priv->call_depth ? o : a);

	/* we can not handle deletion conflicts */
	if (is_null_oid(a) || is_null_oid(b))
		BUG("submodule deleted on one side; this should be handled outside of merge_submodule()");

	if ((sub_not_initialized = repo_submodule_init(&subrepo,
		opt->repo, path, null_oid(the_hash_algo)))) {
		path_msg(opt, CONFLICT_SUBMODULE_NOT_INITIALIZED, 0,
			 path, NULL, NULL, NULL,
			 _("Failed to merge submodule %s (not checked out)"),
			 path);
		sub_flag = CONFLICT_SUBMODULE_NOT_INITIALIZED;
		goto cleanup;
	}

	if (is_null_oid(o)) {
		path_msg(opt, CONFLICT_SUBMODULE_NULL_MERGE_BASE, 0,
			 path, NULL, NULL, NULL,
			 _("Failed to merge submodule %s (no merge base)"),
			 path);
		goto cleanup;
	}

	if (!(commit_o = lookup_commit_reference(&subrepo, o)) ||
	    !(commit_a = lookup_commit_reference(&subrepo, a)) ||
	    !(commit_b = lookup_commit_reference(&subrepo, b))) {
		path_msg(opt, CONFLICT_SUBMODULE_HISTORY_NOT_AVAILABLE, 0,
			 path, NULL, NULL, NULL,
			 _("Failed to merge submodule %s (commits not present)"),
			 path);
		sub_flag = CONFLICT_SUBMODULE_HISTORY_NOT_AVAILABLE;
		goto cleanup;
	}

	/* check whether both changes are forward */
	ret2 = repo_in_merge_bases(&subrepo, commit_o, commit_a);
	if (ret2 < 0) {
		path_msg(opt, ERROR_SUBMODULE_CORRUPT, 0,
			 path, NULL, NULL, NULL,
			 _("error: failed to merge submodule %s "
			   "(repository corrupt)"),
			 path);
		ret = -1;
		goto cleanup;
	}
	if (ret2 > 0)
		ret2 = repo_in_merge_bases(&subrepo, commit_o, commit_b);
	if (ret2 < 0) {
		path_msg(opt, ERROR_SUBMODULE_CORRUPT, 0,
			 path, NULL, NULL, NULL,
			 _("error: failed to merge submodule %s "
			   "(repository corrupt)"),
			 path);
		ret = -1;
		goto cleanup;
	}
	if (!ret2) {
		path_msg(opt, CONFLICT_SUBMODULE_MAY_HAVE_REWINDS, 0,
			 path, NULL, NULL, NULL,
			 _("Failed to merge submodule %s "
			   "(commits don't follow merge-base)"),
			 path);
		goto cleanup;
	}

	/* Case #1: a is contained in b or vice versa */
	ret2 = repo_in_merge_bases(&subrepo, commit_a, commit_b);
	if (ret2 < 0) {
		path_msg(opt, ERROR_SUBMODULE_CORRUPT, 0,
			 path, NULL, NULL, NULL,
			 _("error: failed to merge submodule %s "
			   "(repository corrupt)"),
			 path);
		ret = -1;
		goto cleanup;
	}
	if (ret2 > 0) {
		oidcpy(result, b);
		path_msg(opt, INFO_SUBMODULE_FAST_FORWARDING, 1,
			 path, NULL, NULL, NULL,
			 _("Note: Fast-forwarding submodule %s to %s"),
			 path, oid_to_hex(b));
		ret = 1;
		goto cleanup;
	}
	ret2 = repo_in_merge_bases(&subrepo, commit_b, commit_a);
	if (ret2 < 0) {
		path_msg(opt, ERROR_SUBMODULE_CORRUPT, 0,
			 path, NULL, NULL, NULL,
			 _("error: failed to merge submodule %s "
			   "(repository corrupt)"),
			 path);
		ret = -1;
		goto cleanup;
	}
	if (ret2 > 0) {
		oidcpy(result, a);
		path_msg(opt, INFO_SUBMODULE_FAST_FORWARDING, 1,
			 path, NULL, NULL, NULL,
			 _("Note: Fast-forwarding submodule %s to %s"),
			 path, oid_to_hex(a));
		ret = 1;
		goto cleanup;
	}

	/*
	 * Case #2: There are one or more merges that contain a and b in
	 * the submodule. If there is only one, then present it as a
	 * suggestion to the user, but leave it marked unmerged so the
	 * user needs to confirm the resolution.
	 */

	/* Skip the search if makes no sense to the calling context.  */
	if (!search)
		goto cleanup;

	/* find commit which merges them */
	parent_count = find_first_merges(&subrepo, path, commit_a, commit_b,
					 &merges);
	switch (parent_count) {
	case -1:
		path_msg(opt, ERROR_SUBMODULE_CORRUPT, 0,
			 path, NULL, NULL, NULL,
			 _("error: failed to merge submodule %s "
			   "(repository corrupt)"),
			 path);
		ret = -1;
		break;
	case 0:
		path_msg(opt, CONFLICT_SUBMODULE_FAILED_TO_MERGE, 0,
			 path, NULL, NULL, NULL,
			 _("Failed to merge submodule %s"), path);
		break;

	case 1:
		format_commit(&sb, 4, &subrepo,
			      (struct commit *)merges.objects[0].item);
		path_msg(opt, CONFLICT_SUBMODULE_FAILED_TO_MERGE_BUT_POSSIBLE_RESOLUTION, 0,
			 path, NULL, NULL, NULL,
			 _("Failed to merge submodule %s, but a possible merge "
			   "resolution exists: %s"),
			 path, sb.buf);
		strbuf_release(&sb);
		break;
	default:
		for (i = 0; i < merges.nr; i++)
			format_commit(&sb, 4, &subrepo,
				      (struct commit *)merges.objects[i].item);
		path_msg(opt, CONFLICT_SUBMODULE_FAILED_TO_MERGE_BUT_POSSIBLE_RESOLUTION, 0,
			 path, NULL, NULL, NULL,
			 _("Failed to merge submodule %s, but multiple "
			   "possible merges exist:\n%s"), path, sb.buf);
		strbuf_release(&sb);
	}

	object_array_clear(&merges);
cleanup:
	if (!opt->priv->call_depth && !ret) {
		struct string_list *csub = &opt->priv->conflicted_submodules;
		struct conflicted_submodule_item *util;
		const char *abbrev;

		util = xmalloc(sizeof(*util));
		util->flag = sub_flag;
		util->abbrev = NULL;
		if (!sub_not_initialized) {
			abbrev = repo_find_unique_abbrev(&subrepo, b, DEFAULT_ABBREV);
			util->abbrev = xstrdup(abbrev);
		}
		string_list_append(csub, path)->util = util;
	}

	if (!sub_not_initialized)
		repo_clear(&subrepo);
	return ret;
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

	attr_index->repo = opt->repo;
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
	struct ll_merge_options ll_opts = LL_MERGE_OPTIONS_INIT;
	char *base, *name1, *name2;
	enum ll_merge_result merge_status;

	if (!opt->priv->attr_index.initialized)
		initialize_attr_index(opt);

	ll_opts.renormalize = opt->renormalize;
	ll_opts.extra_marker_size = extra_marker_size;
	ll_opts.xdl_opts = opt->xdl_opts;
	ll_opts.conflict_style = opt->conflict_style;

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
	if (merge_status == LL_MERGE_BINARY_CONFLICT)
		path_msg(opt, CONFLICT_BINARY, 0,
			 path, NULL, NULL, NULL,
			 "warning: Cannot merge binary files: %s (%s vs. %s)",
			 path, name1, name2);

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
				const int record_object,
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
	 * ll_merge; this is needed if we have content merges of content
	 * merges, which happens for example with rename/rename(2to1) and
	 * rename/add conflicts.
	 */
	int clean = 1;

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
					  two_way ? null_oid(the_hash_algo) : &o->oid,
					  &a->oid, &b->oid,
					  pathnames, extra_marker_size,
					  &result_buf);

		if ((merge_status < 0) || !result_buf.ptr) {
			path_msg(opt, ERROR_THREEWAY_CONTENT_MERGE_FAILED, 0,
				 pathnames[0], pathnames[1], pathnames[2], NULL,
				 _("error: failed to execute internal merge for %s"),
				 path);
			ret = -1;
		}

		if (!ret && record_object &&
		    write_object_file(result_buf.ptr, result_buf.size,
				      OBJ_BLOB, &result->oid)) {
			path_msg(opt, ERROR_OBJECT_WRITE_FAILED, 0,
				 pathnames[0], pathnames[1], pathnames[2], NULL,
				 _("error: unable to add %s to database"), path);
			ret = -1;
		}
		free(result_buf.ptr);

		if (ret)
			return -1;
		if (merge_status > 0)
			clean = 0;
		path_msg(opt, INFO_AUTO_MERGING, 1, path, NULL, NULL, NULL,
			 _("Auto-merging %s"), path);
	} else if (S_ISGITLINK(a->mode)) {
		int two_way = ((S_IFMT & o->mode) != (S_IFMT & a->mode));
		clean = merge_submodule(opt, pathnames[0],
					two_way ? null_oid(the_hash_algo) : &o->oid,
					&a->oid, &b->oid, &result->oid);
		if (clean < 0)
			return -1;
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
	if (!c_info)
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
		path_msg(opt, CONFLICT_DIR_RENAME_FILE_IN_WAY, 0,
			 new_path, NULL, NULL, &c_info->source_files,
			 _("CONFLICT (implicit dir rename): Existing "
			   "file/dir at %s in the way of implicit "
			   "directory rename(s) putting the following "
			   "path(s) there: %s."),
			 new_path, collision_paths.buf);
		clean = 0;
	} else if (c_info->source_files.nr > 1) {
		c_info->reported_already = 1;
		strbuf_add_separated_string_list(&collision_paths, ", ",
						 &c_info->source_files);
		path_msg(opt, CONFLICT_DIR_RENAME_COLLISION, 0,
			 new_path, NULL, NULL, &c_info->source_files,
			 _("CONFLICT (implicit dir rename): Cannot map "
			   "more than one path to %s; implicit directory "
			   "renames tried to put these paths there: %s"),
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
			path_msg(opt, CONFLICT_DIR_RENAME_SPLIT, 0,
				 source_dir, NULL, NULL, NULL,
				 _("CONFLICT (directory rename split): "
				   "Unclear where to rename %s to; it was "
				   "renamed to multiple other directories, "
				   "with no destination getting a majority of "
				   "the files."),
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
			string_list_init_nodup(&collision_info->source_files);
			strmap_put(collisions, new_path, collision_info);
		}
		string_list_insert(&collision_info->source_files,
				   pair->two->path);
	}
}

static void free_collisions(struct strmap *collisions)
{
	struct hashmap_iter iter;
	struct strmap_entry *entry;

	/* Free each value in the collisions map */
	strmap_for_each_entry(collisions, &iter, entry) {
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
	free_strmap_strings(collisions);
	strmap_clear(collisions, 1);
}

static char *check_for_directory_rename(struct merge_options *opt,
					const char *path,
					unsigned side_index,
					struct strmap *dir_renames,
					struct strmap *dir_rename_exclusions,
					struct strmap *collisions,
					int *clean_merge)
{
	char *new_path;
	struct strmap_entry *rename_info;
	struct strmap_entry *otherinfo;
	const char *new_dir;
	int other_side = 3 - side_index;

	/*
	 * Cases where we don't have or don't want a directory rename for
	 * this path.
	 */
	if (strmap_empty(dir_renames))
		return NULL;
	if (strmap_get(&collisions[other_side], path))
		return NULL;
	rename_info = check_dir_renamed(path, dir_renames);
	if (!rename_info)
		return NULL;

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
	new_dir = rename_info->value; /* old_dir = rename_info->key; */
	otherinfo = strmap_get_entry(dir_rename_exclusions, new_dir);
	if (otherinfo) {
		path_msg(opt, INFO_DIR_RENAME_SKIPPED_DUE_TO_RERENAME, 1,
			 rename_info->key, path, new_dir, NULL,
			 _("WARNING: Avoiding applying %s -> %s rename "
			   "to %s, because %s itself was renamed."),
			 rename_info->key, new_dir, path, new_dir);
		return NULL;
	}

	new_path = handle_path_level_conflicts(opt, path, side_index,
					       rename_info,
					       &collisions[side_index]);
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
	cur_path = mem_pool_strdup(&opt->priv->pool, new_path);
	free((char*)new_path);
	new_path = (char *)cur_path;

	while (1) {
		/* Find the parent directory of cur_path */
		char *last_slash = strrchr(cur_path, '/');
		if (last_slash) {
			parent_name = mem_pool_strndup(&opt->priv->pool,
						       cur_path,
						       last_slash - cur_path);
		} else {
			parent_name = opt->priv->toplevel_dir;
			break;
		}

		/* Look it up in opt->priv->paths */
		entry = strmap_get_entry(&opt->priv->paths, parent_name);
		if (entry) {
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

		dir_ci = mem_pool_calloc(&opt->priv->pool, 1, sizeof(*dir_ci));

		dir_ci->merged.directory_name = parent_name;
		len = strlen(parent_name);
		/* len+1 because of trailing '/' character */
		dir_ci->merged.basename_offset = (len > 0 ? len+1 : len);
		dir_ci->dirmask = ci->filemask;
		strmap_put(&opt->priv->paths, cur_dir, dir_ci);

		parent_name = cur_dir;
	}

	assert(ci->filemask == 2 || ci->filemask == 4);
	assert(ci->dirmask == 0 || ci->dirmask == 1);
	if (ci->dirmask == 0)
		strmap_remove(&opt->priv->paths, old_path, 0);
	else {
		/*
		 * This file exists on one side, but we still had a directory
		 * at the old location that we can't remove until after
		 * processing all paths below it.  So, make a copy of ci in
		 * new_ci and only put the file information into it.
		 */
		new_ci = mem_pool_calloc(&opt->priv->pool, 1, sizeof(*new_ci));
		memcpy(new_ci, ci, sizeof(*ci));
		assert(!new_ci->match_mask);
		new_ci->dirmask = 0;
		new_ci->stages[1].mode = 0;
		oidcpy(&new_ci->stages[1].oid, null_oid(the_hash_algo));

		/*
		 * Now that we have the file information in new_ci, make sure
		 * ci only has the directory information.
		 */
		ci->filemask = 0;
		ci->merged.clean = 1;
		for (i = MERGE_BASE; i <= MERGE_SIDE2; i++) {
			if (ci->dirmask & (1 << i))
				continue;
			/* zero out any entries related to files */
			ci->stages[i].mode = 0;
			oidcpy(&ci->stages[i].oid, null_oid(the_hash_algo));
		}

		/* Now we want to focus on new_ci, so reassign ci to it. */
		ci = new_ci;
	}

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

		ci = new_ci;
	}

	if (opt->detect_directory_renames == MERGE_DIRECTORY_RENAMES_TRUE) {
		/* Notify user of updated path */
		if (pair->status == 'A')
			path_msg(opt, INFO_DIR_RENAME_APPLIED, 1,
				 new_path, old_path, NULL, NULL,
				 _("Path updated: %s added in %s inside a "
				   "directory that was renamed in %s; moving "
				   "it to %s."),
				 old_path, branch_with_new_path,
				 branch_with_dir_rename, new_path);
		else
			path_msg(opt, INFO_DIR_RENAME_APPLIED, 1,
				 new_path, old_path, NULL, NULL,
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
			path_msg(opt, CONFLICT_DIR_RENAME_SUGGESTED, 1,
				 new_path, old_path, NULL, NULL,
				 _("CONFLICT (file location): %s added in %s "
				   "inside a directory that was renamed in %s, "
				   "suggesting it should perhaps be moved to "
				   "%s."),
				 old_path, branch_with_new_path,
				 branch_with_dir_rename, new_path);
		else
			path_msg(opt, CONFLICT_DIR_RENAME_SUGGESTED, 1,
				 new_path, old_path, NULL, NULL,
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

	string_list_clear(&dirs_to_insert, 0);
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
			const int record_object = true;

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
				struct rename_info *ri = &opt->priv->renames;
				int j;

				/* Both sides renamed the same way */
				assert(side1 == side2);
				memcpy(&side1->stages[0], &base->stages[0],
				       sizeof(merged));
				side1->filemask |= (1 << MERGE_BASE);
				/* Mark base as resolved by removal */
				base->merged.is_null = 1;
				base->merged.clean = 1;

				/*
				 * Disable remembering renames optimization;
				 * rename/rename(1to1) is incredibly rare, and
				 * just disabling the optimization is easier
				 * than purging cached_pairs,
				 * cached_target_names, and dir_rename_counts.
				 */
				for (j = 0; j < 3; j++)
					ri->merge_trees[j] = NULL;

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
							   record_object,
							   &merged);
			if (clean_merge < 0)
				return -1;
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
			path_msg(opt, CONFLICT_RENAME_RENAME, 0,
				 pathnames[0], pathnames[1], pathnames[2], NULL,
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

		assert(source_deleted || oldinfo->filemask & old_sidemask ||
		       !strcmp(pair->one->path, pair->two->path));

		/* Need to check for special types of rename conflicts... */
		if (collision && !source_deleted) {
			/* collision: rename/add or rename/rename(2to1) */
			const char *pathnames[3];
			struct version_info merged;

			struct conflict_info *base, *side1, *side2;
			int clean;
			const int record_object = true;

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
						     record_object,
						     &merged);
			if (clean < 0)
				return -1;

			memcpy(&newinfo->stages[target_index], &merged,
			       sizeof(merged));
			if (!clean) {
				path_msg(opt, CONFLICT_RENAME_COLLIDES, 0,
					 newpath, oldpath, NULL, NULL,
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
			path_msg(opt, CONFLICT_RENAME_DELETE, 0,
				 newpath, oldpath, NULL, NULL,
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
				memcpy(&oldinfo->stages[0].oid, null_oid(the_hash_algo),
				       sizeof(oldinfo->stages[0].oid));
				oldinfo->stages[0].mode = 0;
				oldinfo->filemask &= 0x06;
			} else if (source_deleted) {
				/* rename/delete */
				newinfo->path_conflict = 1;
				path_msg(opt, CONFLICT_RENAME_DELETE, 0,
					 newpath, oldpath, NULL, NULL,
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
	       possible_side_renames(renames, 2) ||
	       !strmap_empty(&renames->cached_pairs[1]) ||
	       !strmap_empty(&renames->cached_pairs[2]);
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

static void prune_cached_from_relevant(struct rename_info *renames,
				       unsigned side)
{
	/* Reason for this function described in add_pair() */
	struct hashmap_iter iter;
	struct strmap_entry *entry;

	/* Remove from relevant_sources all entries in cached_pairs[side] */
	strmap_for_each_entry(&renames->cached_pairs[side], &iter, entry) {
		strintmap_remove(&renames->relevant_sources[side],
				 entry->key);
	}
	/* Remove from relevant_sources all entries in cached_irrelevant[side] */
	strset_for_each_entry(&renames->cached_irrelevant[side], &iter, entry) {
		strintmap_remove(&renames->relevant_sources[side],
				 entry->key);
	}
}

static void use_cached_pairs(struct merge_options *opt,
			     struct strmap *cached_pairs,
			     struct diff_queue_struct *pairs)
{
	struct hashmap_iter iter;
	struct strmap_entry *entry;

	/*
	 * Add to side_pairs all entries from renames->cached_pairs[side_index].
	 * (Info in cached_irrelevant[side_index] is not relevant here.)
	 */
	strmap_for_each_entry(cached_pairs, &iter, entry) {
		struct diff_filespec *one, *two;
		const char *old_name = entry->key;
		const char *new_name = entry->value;
		if (!new_name)
			new_name = old_name;

		/*
		 * cached_pairs has *copies* of old_name and new_name,
		 * because it has to persist across merges.  Since
		 * pool_alloc_filespec() will just re-use the existing
		 * filenames, which will also get re-used by
		 * opt->priv->paths if they become renames, and then
		 * get freed at the end of the merge, that would leave
		 * the copy in cached_pairs dangling.  Avoid this by
		 * making a copy here.
		 */
		old_name = mem_pool_strdup(&opt->priv->pool, old_name);
		new_name = mem_pool_strdup(&opt->priv->pool, new_name);

		/* We don't care about oid/mode, only filenames and status */
		one = pool_alloc_filespec(&opt->priv->pool, old_name);
		two = pool_alloc_filespec(&opt->priv->pool, new_name);
		pool_diff_queue(&opt->priv->pool, pairs, one, two);
		pairs->queue[pairs->nr-1]->status = entry->value ? 'R' : 'D';
	}
}

static void cache_new_pair(struct rename_info *renames,
			   int side,
			   char *old_path,
			   char *new_path,
			   int free_old_value)
{
	char *old_value;
	new_path = xstrdup(new_path);
	old_value = strmap_put(&renames->cached_pairs[side],
			       old_path, new_path);
	strset_add(&renames->cached_target_names[side], new_path);
	if (free_old_value)
		free(old_value);
	else
		assert(!old_value);
}

static void possibly_cache_new_pair(struct rename_info *renames,
				    struct diff_filepair *p,
				    unsigned side,
				    char *new_path)
{
	int dir_renamed_side = 0;

	if (new_path) {
		/*
		 * Directory renames happen on the other side of history from
		 * the side that adds new files to the old directory.
		 */
		dir_renamed_side = 3 - side;
	} else {
		int val = strintmap_get(&renames->relevant_sources[side],
					p->one->path);
		if (val == RELEVANT_NO_MORE) {
			assert(p->status == 'D');
			strset_add(&renames->cached_irrelevant[side],
				   p->one->path);
		}
		if (val <= 0)
			return;
	}

	if (p->status == 'D') {
		/*
		 * If we already had this delete, we'll just set it's value
		 * to NULL again, so no harm.
		 */
		strmap_put(&renames->cached_pairs[side], p->one->path, NULL);
	} else if (p->status == 'R') {
		if (!new_path)
			new_path = p->two->path;
		else
			cache_new_pair(renames, dir_renamed_side,
				       p->two->path, new_path, 0);
		cache_new_pair(renames, side, p->one->path, new_path, 1);
	} else if (p->status == 'A' && new_path) {
		cache_new_pair(renames, dir_renamed_side,
			       p->two->path, new_path, 0);
	}
}

static int compare_pairs(const void *a_, const void *b_)
{
	const struct diff_filepair *a = *((const struct diff_filepair **)a_);
	const struct diff_filepair *b = *((const struct diff_filepair **)b_);

	return strcmp(a->one->path, b->one->path);
}

/* Call diffcore_rename() to update deleted/added pairs into rename pairs */
static int detect_regular_renames(struct merge_options *opt,
				  unsigned side_index)
{
	struct diff_options diff_opts;
	struct rename_info *renames = &opt->priv->renames;

	prune_cached_from_relevant(renames, side_index);
	if (!possible_side_renames(renames, side_index)) {
		/*
		 * No rename detection needed for this side, but we still need
		 * to make sure 'adds' are marked correctly in case the other
		 * side had directory renames.
		 */
		resolve_diffpair_statuses(&renames->pairs[side_index]);
		return 0;
	}

	partial_clear_dir_rename_count(&renames->dir_rename_count[side_index]);
	repo_diff_setup(opt->repo, &diff_opts);
	diff_opts.flags.recursive = 1;
	diff_opts.flags.rename_empty = 0;
	diff_opts.detect_rename = DIFF_DETECT_RENAME;
	diff_opts.rename_limit = opt->rename_limit;
	if (opt->rename_limit <= 0)
		diff_opts.rename_limit = 7000;
	diff_opts.rename_score = opt->rename_score;
	diff_opts.show_rename_progress = opt->show_rename_progress;
	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_setup_done(&diff_opts);

	diff_queued_diff = renames->pairs[side_index];
	trace2_region_enter("diff", "diffcore_rename", opt->repo);
	diffcore_rename_extended(&diff_opts,
				 &opt->priv->pool,
				 &renames->relevant_sources[side_index],
				 &renames->dirs_removed[side_index],
				 &renames->dir_rename_count[side_index],
				 &renames->cached_pairs[side_index]);
	trace2_region_leave("diff", "diffcore_rename", opt->repo);
	resolve_diffpair_statuses(&diff_queued_diff);

	if (diff_opts.needed_rename_limit > 0)
		renames->redo_after_renames = 0;
	if (diff_opts.needed_rename_limit > renames->needed_limit)
		renames->needed_limit = diff_opts.needed_rename_limit;

	renames->pairs[side_index] = diff_queued_diff;

	diff_opts.output_format = DIFF_FORMAT_NO_OUTPUT;
	diff_queued_diff.nr = 0;
	diff_queued_diff.queue = NULL;
	diff_flush(&diff_opts);

	return 1;
}

/*
 * Get information of all renames which occurred in 'side_pairs', making use
 * of any implicit directory renames in side_dir_renames (also making use of
 * implicit directory renames rename_exclusions as needed by
 * check_for_directory_rename()).  Add all (updated) renames into result.
 */
static int collect_renames(struct merge_options *opt,
			   struct diff_queue_struct *result,
			   unsigned side_index,
			   struct strmap *collisions,
			   struct strmap *dir_renames_for_side,
			   struct strmap *rename_exclusions)
{
	int i, clean = 1;
	struct diff_queue_struct *side_pairs;
	struct rename_info *renames = &opt->priv->renames;

	side_pairs = &renames->pairs[side_index];

	for (i = 0; i < side_pairs->nr; ++i) {
		struct diff_filepair *p = side_pairs->queue[i];
		char *new_path; /* non-NULL only with directory renames */

		if (p->status != 'A' && p->status != 'R') {
			possibly_cache_new_pair(renames, p, side_index, NULL);
			pool_diff_free_filepair(&opt->priv->pool, p);
			continue;
		}
		if (opt->detect_directory_renames == MERGE_DIRECTORY_RENAMES_NONE &&
		    p->status == 'R' && 1) {
			possibly_cache_new_pair(renames, p, side_index, NULL);
			goto skip_directory_renames;
		}

		new_path = check_for_directory_rename(opt, p->two->path,
						      side_index,
						      dir_renames_for_side,
						      rename_exclusions,
						      collisions,
						      &clean);

		possibly_cache_new_pair(renames, p, side_index, new_path);
		if (p->status != 'R' && !new_path) {
			pool_diff_free_filepair(&opt->priv->pool, p);
			continue;
		}

		if (new_path)
			apply_directory_rename_modifications(opt, p, new_path);

skip_directory_renames:
		/*
		 * p->score comes back from diffcore_rename_extended() with
		 * the similarity of the renamed file.  The similarity was
		 * used to determine that the two files were related and
		 * are a rename, which we have already used, but beyond
		 * that we have no use for the similarity.  So p->score is
		 * now irrelevant.  However, process_renames() will need to
		 * know which side of the merge this rename was associated
		 * with, so overwrite p->score with that value.
		 */
		p->score = side_index;
		result->queue[result->nr++] = p;
	}

	return clean;
}

static int detect_and_process_renames(struct merge_options *opt)
{
	struct diff_queue_struct combined = { 0 };
	struct rename_info *renames = &opt->priv->renames;
	struct strmap collisions[3];
	int need_dir_renames, s, i, clean = 1;
	unsigned detection_run = 0;

	if (!possible_renames(renames))
		goto cleanup;
	if (!opt->detect_renames) {
		renames->redo_after_renames = 0;
		renames->cached_pairs_valid_side = 0;
		goto cleanup;
	}

	trace2_region_enter("merge", "regular renames", opt->repo);
	detection_run |= detect_regular_renames(opt, MERGE_SIDE1);
	detection_run |= detect_regular_renames(opt, MERGE_SIDE2);
	if (renames->needed_limit) {
		renames->cached_pairs_valid_side = 0;
		renames->redo_after_renames = 0;
	}
	if (renames->redo_after_renames && detection_run) {
		int i, side;
		struct diff_filepair *p;

		/* Cache the renames, we found */
		for (side = MERGE_SIDE1; side <= MERGE_SIDE2; side++) {
			for (i = 0; i < renames->pairs[side].nr; ++i) {
				p = renames->pairs[side].queue[i];
				possibly_cache_new_pair(renames, p, side, NULL);
			}
		}

		/* Restart the merge with the cached renames */
		renames->redo_after_renames = 2;
		trace2_region_leave("merge", "regular renames", opt->repo);
		goto cleanup;
	}
	use_cached_pairs(opt, &renames->cached_pairs[1], &renames->pairs[1]);
	use_cached_pairs(opt, &renames->cached_pairs[2], &renames->pairs[2]);
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
	for (i = MERGE_SIDE1; i <= MERGE_SIDE2; i++) {
		int other_side = 3 - i;
		compute_collisions(&collisions[i],
				   &renames->dir_renames[other_side],
				   &renames->pairs[i]);
	}
	clean &= collect_renames(opt, &combined, MERGE_SIDE1,
				 collisions,
				 &renames->dir_renames[2],
				 &renames->dir_renames[1]);
	clean &= collect_renames(opt, &combined, MERGE_SIDE2,
				 collisions,
				 &renames->dir_renames[1],
				 &renames->dir_renames[2]);
	for (i = MERGE_SIDE1; i <= MERGE_SIDE2; i++)
		free_collisions(&collisions[i]);
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
			pool_diff_free_filepair(&opt->priv->pool, p);
		}
	}

simple_cleanup:
	/* Free memory for renames->pairs[] and combined */
	for (s = MERGE_SIDE1; s <= MERGE_SIDE2; s++) {
		free(renames->pairs[s].queue);
		diff_queue_init(&renames->pairs[s]);
	}
	for (i = 0; i < combined.nr; i++)
		pool_diff_free_filepair(&opt->priv->pool, combined.queue[i]);
	free(combined.queue);

	return clean;
}

/*** Function Grouping: functions related to process_entries() ***/

static int sort_dirs_next_to_their_children(const char *one, const char *two)
{
	unsigned char c1, c2;

	/*
	 * Here we only care that entries for directories appear adjacent
	 * to and before files underneath the directory.  We can achieve
	 * that by pretending to add a trailing slash to every file and
	 * then sorting.  In other words, we do not want the natural
	 * sorting of
	 *     foo
	 *     foo.txt
	 *     foo/bar
	 * Instead, we want "foo" to sort as though it were "foo/", so that
	 * we instead get
	 *     foo.txt
	 *     foo
	 *     foo/bar
	 * To achieve this, we basically implement our own strcmp, except that
	 * if we get to the end of either string instead of comparing NUL to
	 * another character, we compare '/' to it.
	 *
	 * If this unusual "sort as though '/' were appended" perplexes
	 * you, perhaps it will help to note that this is not the final
	 * sort.  write_tree() will sort again without the trailing slash
	 * magic, but just on paths immediately under a given tree.
	 *
	 * The reason to not use df_name_compare directly was that it was
	 * just too expensive (we don't have the string lengths handy), so
	 * it was reimplemented.
	 */

	/*
	 * NOTE: This function will never be called with two equal strings,
	 * because it is used to sort the keys of a strmap, and strmaps have
	 * unique keys by construction.  That simplifies our c1==c2 handling
	 * below.
	 */

	while (*one && (*one == *two)) {
		one++;
		two++;
	}

	c1 = *one ? *one : '/';
	c2 = *two ? *two : '/';

	if (c1 == c2) {
		/* Getting here means one is a leading directory of the other */
		return (*one) ? 1 : -1;
	} else
		return c1 - c2;
}

static int read_oid_strbuf(struct merge_options *opt,
			   const struct object_id *oid,
			   struct strbuf *dst,
			   const char *path)
{
	void *buf;
	enum object_type type;
	unsigned long size;
	buf = odb_read_object(the_repository->objects, oid, &type, &size);
	if (!buf) {
		path_msg(opt, ERROR_OBJECT_READ_FAILED, 0,
			 path, NULL, NULL, NULL,
			 _("error: cannot read object %s"), oid_to_hex(oid));
		return -1;
	}
	if (type != OBJ_BLOB) {
		free(buf);
		path_msg(opt, ERROR_OBJECT_NOT_A_BLOB, 0,
			 path, NULL, NULL, NULL,
			 _("error: object %s is not a blob"), oid_to_hex(oid));
		return -1;
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

	if (read_oid_strbuf(opt, &base->oid, &basebuf, path) ||
	    read_oid_strbuf(opt, &side->oid, &sidebuf, path))
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

static int write_tree(struct object_id *result_oid,
		      struct string_list *versions,
		      unsigned int offset,
		      size_t hash_size)
{
	size_t maxlen = 0, extra;
	unsigned int nr;
	struct strbuf buf = STRBUF_INIT;
	int i, ret = 0;

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
	if (write_object_file(buf.buf, buf.len, OBJ_TREE, result_oid))
		ret = -1;
	strbuf_release(&buf);
	return ret;
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

static int write_completed_directory(struct merge_options *opt,
				     const char *new_directory_name,
				     struct directory_versions *info)
{
	const char *prev_dir;
	struct merged_info *dir_info = NULL;
	unsigned int offset, ret = 0;

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
	 *     toplevel directory, while umm.c & stuff.h & baz.c are from the
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
		return 0;

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
		return 0;
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
		int record_tree = (!opt->mergeability_only ||
				   opt->priv->call_depth);
		dir_info->is_null = 0;
		dir_info->result.mode = S_IFDIR;
		if (record_tree &&
		    write_tree(&dir_info->result.oid, &info->versions, offset,
			       opt->repo->hash_algo->rawsz) < 0)
			ret = -1;
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

	return ret;
}

/* Per entry merge function */
static int process_entry(struct merge_options *opt,
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
			return 0;
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
			oidcpy(&ci->stages[i].oid, null_oid(the_hash_algo));
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
			return 0;
		}

		/*
		 * This file still exists on at least one side, and we want
		 * the directory to remain here, so we need to move this
		 * path to some new location.
		 */
		new_ci = mem_pool_calloc(&opt->priv->pool, 1, sizeof(*new_ci));

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
			oidcpy(&new_ci->stages[i].oid, null_oid(the_hash_algo));
		}

		/*
		 * Find out which side this file came from; note that we
		 * cannot just use ci->filemask, because renames could cause
		 * the filemask to go back to 7.  So we use dirmask, then
		 * pick the opposite side's index.
		 */
		df_file_index = (ci->dirmask & (1 << 1)) ? 2 : 1;
		branch = (df_file_index == 1) ? opt->branch1 : opt->branch2;
		path = unique_path(opt, path, branch);
		strmap_put(&opt->priv->paths, path, new_ci);

		path_msg(opt, CONFLICT_FILE_DIRECTORY, 0,
			 path, old_path, NULL, NULL,
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
		ci->merged.clean = !ci->df_conflict && !ci->path_conflict;
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
			if (ci->merged.is_null)
				ci->merged.clean = 1;
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

			new_ci = mem_pool_alloc(&opt->priv->pool,
						sizeof(*new_ci));

			if (S_ISREG(a_mode))
				rename_a = 1;
			else if (S_ISREG(b_mode))
				rename_b = 1;
			else {
				rename_a = 1;
				rename_b = 1;
			}

			if (rename_a)
				a_path = unique_path(opt, path, opt->branch1);
			if (rename_b)
				b_path = unique_path(opt, path, opt->branch2);

			if (rename_a && rename_b) {
				path_msg(opt, CONFLICT_DISTINCT_MODES, 0,
					 path, a_path, b_path, NULL,
					 _("CONFLICT (distinct types): %s had "
					   "different types on each side; "
					   "renamed both of them so each can "
					   "be recorded somewhere."),
					 path);
			} else {
				path_msg(opt, CONFLICT_DISTINCT_MODES, 0,
					 path, rename_a ? a_path : b_path,
					 NULL, NULL,
					 _("CONFLICT (distinct types): %s had "
					   "different types on each side; "
					   "renamed one of them so each can be "
					   "recorded somewhere."),
					 path);
			}

			ci->merged.clean = 0;
			memcpy(new_ci, ci, sizeof(*new_ci));

			/* Put b into new_ci, removing a from stages */
			new_ci->merged.result.mode = ci->stages[2].mode;
			oidcpy(&new_ci->merged.result.oid, &ci->stages[2].oid);
			new_ci->stages[1].mode = 0;
			oidcpy(&new_ci->stages[1].oid, null_oid(the_hash_algo));
			new_ci->filemask = 5;
			if ((S_IFMT & b_mode) != (S_IFMT & o_mode)) {
				new_ci->stages[0].mode = 0;
				oidcpy(&new_ci->stages[0].oid, null_oid(the_hash_algo));
				new_ci->filemask = 4;
			}

			/* Leave only a in ci, fixing stages. */
			ci->merged.result.mode = ci->stages[1].mode;
			oidcpy(&ci->merged.result.oid, &ci->stages[1].oid);
			ci->stages[2].mode = 0;
			oidcpy(&ci->stages[2].oid, null_oid(the_hash_algo));
			ci->filemask = 3;
			if ((S_IFMT & a_mode) != (S_IFMT & o_mode)) {
				ci->stages[0].mode = 0;
				oidcpy(&ci->stages[0].oid, null_oid(the_hash_algo));
				ci->filemask = 2;
			}

			/* Insert entries into opt->priv_paths */
			assert(rename_a || rename_b);
			if (rename_a)
				strmap_put(&opt->priv->paths, a_path, ci);

			if (!rename_b)
				b_path = path;
			strmap_put(&opt->priv->paths, b_path, new_ci);

			if (rename_a && rename_b)
				strmap_remove(&opt->priv->paths, path, 0);

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
		int clean_merge;
		struct version_info *o = &ci->stages[0];
		struct version_info *a = &ci->stages[1];
		struct version_info *b = &ci->stages[2];
		int record_object = (!opt->mergeability_only ||
				     opt->priv->call_depth);

		clean_merge = handle_content_merge(opt, path, o, a, b,
						   ci->pathnames,
						   opt->priv->call_depth * 2,
						   record_object,
						   &merged_file);
		if (clean_merge < 0)
			return -1;
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
			path_msg(opt, CONFLICT_CONTENTS, 0,
				 path, NULL, NULL, NULL,
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
			if (!ci->path_conflict) {
				/*
				 * Blob unchanged after renormalization, so
				 * there's no modify/delete conflict after all;
				 * we can just remove the file.
				 */
				ci->merged.is_null = 1;
				ci->merged.clean = 1;
				 /*
				  * file goes away => even if there was a
				  * directory/file conflict there isn't one now.
				  */
				ci->df_conflict = 0;
			} else {
				/* rename/delete, so conflict remains */
			}
		} else if (ci->path_conflict &&
			   oideq(&ci->stages[0].oid, &ci->stages[side].oid)) {
			/*
			 * This came from a rename/delete; no action to take,
			 * but avoid printing "modify/delete" conflict notice
			 * since the contents were not modified.
			 */
		} else {
			path_msg(opt, CONFLICT_MODIFY_DELETE, 0,
				 path, NULL, NULL, NULL,
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
		oidcpy(&ci->merged.result.oid, null_oid(the_hash_algo));
		assert(!ci->df_conflict);
		ci->merged.clean = !ci->path_conflict;
	}

	/*
	 * If still conflicted, record it separately.  This allows us to later
	 * iterate over just conflicted entries when updating the index instead
	 * of iterating over all entries.
	 */
	if (!ci->merged.clean)
		strmap_put(&opt->priv->conflicted, path, ci);

	/* Record metadata for ci->merged in dir_metadata */
	record_entry_for_tree(dir_metadata, path, &ci->merged);
	return 0;
}

static void prefetch_for_content_merges(struct merge_options *opt,
					struct string_list *plist)
{
	struct string_list_item *e;
	struct oid_array to_fetch = OID_ARRAY_INIT;

	if (opt->repo != the_repository || !repo_has_promisor_remote(the_repository))
		return;

	for (e = &plist->items[plist->nr-1]; e >= plist->items; --e) {
		/* char *path = e->string; */
		struct conflict_info *ci = e->util;
		int i;

		/* Ignore clean entries */
		if (ci->merged.clean)
			continue;

		/* Ignore entries that don't need a content merge */
		if (ci->match_mask || ci->filemask < 6 ||
		    !S_ISREG(ci->stages[1].mode) ||
		    !S_ISREG(ci->stages[2].mode) ||
		    oideq(&ci->stages[1].oid, &ci->stages[2].oid))
			continue;

		/* Also don't need content merge if base matches either side */
		if (ci->filemask == 7 &&
		    S_ISREG(ci->stages[0].mode) &&
		    (oideq(&ci->stages[0].oid, &ci->stages[1].oid) ||
		     oideq(&ci->stages[0].oid, &ci->stages[2].oid)))
			continue;

		for (i = 0; i < 3; i++) {
			unsigned side_mask = (1 << i);
			struct version_info *vi = &ci->stages[i];

			if ((ci->filemask & side_mask) &&
			    S_ISREG(vi->mode) &&
			    odb_read_object_info_extended(opt->repo->objects, &vi->oid, NULL,
							  OBJECT_INFO_FOR_PREFETCH))
				oid_array_append(&to_fetch, &vi->oid);
		}
	}

	promisor_remote_get_direct(opt->repo, to_fetch.oid, to_fetch.nr);
	oid_array_clear(&to_fetch);
}

static int process_entries(struct merge_options *opt,
			   struct object_id *result_oid)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;
	struct string_list plist = STRING_LIST_INIT_NODUP;
	struct string_list_item *entry;
	struct directory_versions dir_metadata = { STRING_LIST_INIT_NODUP,
						   STRING_LIST_INIT_NODUP,
						   NULL, 0 };
	int ret = 0;
	const int record_tree = (!opt->mergeability_only ||
				 opt->priv->call_depth);

	trace2_region_enter("merge", "process_entries setup", opt->repo);
	if (strmap_empty(&opt->priv->paths)) {
		oidcpy(result_oid, opt->repo->hash_algo->empty_tree);
		return 0;
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
	plist.cmp = sort_dirs_next_to_their_children;
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
	prefetch_for_content_merges(opt, &plist);
	for (entry = &plist.items[plist.nr-1]; entry >= plist.items; --entry) {
		char *path = entry->string;
		/*
		 * NOTE: mi may actually be a pointer to a conflict_info, but
		 * we have to check mi->clean first to see if it's safe to
		 * reassign to such a pointer type.
		 */
		struct merged_info *mi = entry->util;

		if (write_completed_directory(opt, mi->directory_name,
					      &dir_metadata) < 0) {
			ret = -1;
			goto cleanup;
		}
		if (mi->clean)
			record_entry_for_tree(&dir_metadata, path, mi);
		else {
			struct conflict_info *ci = (struct conflict_info *)mi;
			if (process_entry(opt, path, ci, &dir_metadata) < 0) {
				ret = -1;
				goto cleanup;
			};
			if (!ci->merged.clean && opt->mergeability_only &&
			    !opt->priv->call_depth) {
				ret = 0;
				goto cleanup;
			}

		}
	}
	trace2_region_leave("merge", "processing", opt->repo);

	trace2_region_enter("merge", "process_entries cleanup", opt->repo);
	if (dir_metadata.offsets.nr != 1 ||
	    (uintptr_t)dir_metadata.offsets.items[0].util != 0) {
		printf("dir_metadata.offsets.nr = %"PRIuMAX" (should be 1)\n",
		       (uintmax_t)dir_metadata.offsets.nr);
		printf("dir_metadata.offsets.items[0].util = %u (should be 0)\n",
		       (unsigned)(uintptr_t)dir_metadata.offsets.items[0].util);
		fflush(stdout);
		BUG("dir_metadata accounting completely off; shouldn't happen");
	}
	if (record_tree &&
	    write_tree(result_oid, &dir_metadata.versions, 0,
		       opt->repo->hash_algo->rawsz) < 0)
		ret = -1;
cleanup:
	string_list_clear(&plist, 0);
	string_list_clear(&dir_metadata.versions, 0);
	string_list_clear(&dir_metadata.offsets, 0);
	trace2_region_leave("merge", "process_entries cleanup", opt->repo);

	return ret;
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
	unpack_opts.preserve_ignored = 0; /* FIXME: !opts->overwrite_ignore */
	if (parse_tree(prev) < 0)
		return -1;
	init_tree_desc(&trees[0], &prev->object.oid, prev->buffer, prev->size);
	if (parse_tree(next) < 0)
		return -1;
	init_tree_desc(&trees[1], &next->object.oid, next->buffer, next->size);

	ret = unpack_trees(2, trees, &unpack_opts);
	clear_unpack_trees_porcelain(&unpack_opts);
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

	/*
	 * We are in a conflicted state. These conflicts might be inside
	 * sparse-directory entries, so check if any entries are outside
	 * of the sparse-checkout cone preemptively.
	 *
	 * We set original_cache_nr below, but that might change if
	 * index_name_pos() calls ask for paths within sparse directories.
	 */
	strmap_for_each_entry(&opt->priv->conflicted, &iter, e) {
		if (!path_in_sparse_checkout(e->key, index)) {
			ensure_full_index(index);
			break;
		}
	}

	/* If any entries have skip_worktree set, we'll have to check 'em out */
	state.force = 1;
	state.quiet = 1;
	state.refresh_cache = 1;
	state.istate = index;
	original_cache_nr = index->cache_nr;

	/* Append every entry from conflicted into index, then sort */
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
			if (ce_skip_worktree(ce))
				errs |= checkout_entry(ce, &state, NULL, NULL);

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

static void print_submodule_conflict_suggestion(struct string_list *csub) {
	struct string_list_item *item;
	struct strbuf msg = STRBUF_INIT;
	struct strbuf tmp = STRBUF_INIT;
	struct strbuf subs = STRBUF_INIT;

	if (!csub->nr)
		return;

	strbuf_add_separated_string_list(&subs, " ", csub);
	for_each_string_list_item(item, csub) {
		struct conflicted_submodule_item *util = item->util;

		/*
		 * NEEDSWORK: The steps to resolve these errors deserve a more
		 * detailed explanation than what is currently printed below.
		 */
		if (util->flag == CONFLICT_SUBMODULE_NOT_INITIALIZED ||
		    util->flag == CONFLICT_SUBMODULE_HISTORY_NOT_AVAILABLE)
			continue;

		/*
		 * TRANSLATORS: This is a line of advice to resolve a merge
		 * conflict in a submodule. The first argument is the submodule
		 * name, and the second argument is the abbreviated id of the
		 * commit that needs to be merged.  For example:
		 *  - go to submodule (mysubmodule), and either merge commit abc1234"
		 */
		strbuf_addf(&tmp, _(" - go to submodule (%s), and either merge commit %s\n"
				    "   or update to an existing commit which has merged those changes\n"),
			    item->string, util->abbrev);
	}

	/*
	 * TRANSLATORS: This is a detailed message for resolving submodule
	 * conflicts.  The first argument is string containing one step per
	 * submodule.  The second is a space-separated list of submodule names.
	 */
	strbuf_addf(&msg,
		    _("Recursive merging with submodules currently only supports trivial cases.\n"
		      "Please manually handle the merging of each conflicted submodule.\n"
		      "This can be accomplished with the following steps:\n"
		      "%s"
		      " - come back to superproject and run:\n\n"
		      "      git add %s\n\n"
		      "   to record the above merge or update\n"
		      " - resolve any other conflicts in the superproject\n"
		      " - commit the resulting index in the superproject\n"),
		    tmp.buf, subs.buf);

	advise_if_enabled(ADVICE_SUBMODULE_MERGE_CONFLICT, "%s", msg.buf);

	strbuf_release(&subs);
	strbuf_release(&tmp);
	strbuf_release(&msg);
}

void merge_display_update_messages(struct merge_options *opt,
				   int detailed,
				   struct merge_result *result)
{
	struct merge_options_internal *opti = result->priv;
	struct hashmap_iter iter;
	struct strmap_entry *e;
	struct string_list olist = STRING_LIST_INIT_NODUP;
	FILE *o = stdout;

	if (opt->record_conflict_msgs_as_headers)
		BUG("Either display conflict messages or record them as headers, not both");
	if (opt->mergeability_only)
		BUG("Displaying conflict messages incompatible with mergeability-only checks");

	trace2_region_enter("merge", "display messages", opt->repo);

	/* Hack to pre-allocate olist to the desired size */
	ALLOC_GROW(olist.items, strmap_get_size(&opti->conflicts),
		   olist.alloc);

	/* Put every entry from output into olist, then sort */
	strmap_for_each_entry(&opti->conflicts, &iter, e) {
		string_list_append(&olist, e->key)->util = e->value;
	}
	string_list_sort(&olist);

	/* Print to stderr if we hit errors rather than just conflicts */
	if (result->clean < 0)
		o = stderr;

	/* Iterate over the items, printing them */
	for (int path_nr = 0; path_nr < olist.nr; ++path_nr) {
		struct string_list *conflicts = olist.items[path_nr].util;
		for (int i = 0; i < conflicts->nr; i++) {
			struct logical_conflict_info *info =
				conflicts->items[i].util;

			/* On failure, ignore regular conflict types */
			if (result->clean < 0 &&
			    info->type < NB_REGULAR_CONFLICT_TYPES)
				continue;

			if (detailed) {
				fprintf(o, "%lu", (unsigned long)info->paths.nr);
				fputc('\0', o);
				for (int n = 0; n < info->paths.nr; n++) {
					fputs(info->paths.v[n], o);
					fputc('\0', o);
				}
				fputs(type_short_descriptions[info->type], o);
				fputc('\0', o);
			}
			fputs(conflicts->items[i].string, o);
			fputc('\n', o);
			if (detailed)
				fputc('\0', o);
		}
	}
	string_list_clear(&olist, 0);

	if (result->clean >= 0)
		print_submodule_conflict_suggestion(&opti->conflicted_submodules);

	/* Also include needed rename limit adjustment now */
	diff_warn_rename_limit("merge.renamelimit",
			       opti->renames.needed_limit, 0);

	trace2_region_leave("merge", "display messages", opt->repo);
}

void merge_get_conflicted_files(struct merge_result *result,
				struct string_list *conflicted_files)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;
	struct merge_options_internal *opti = result->priv;

	strmap_for_each_entry(&opti->conflicted, &iter, e) {
		const char *path = e->key;
		struct conflict_info *ci = e->value;
		int i;

		VERIFY_CI(ci);

		for (i = MERGE_BASE; i <= MERGE_SIDE2; i++) {
			struct stage_info *si;

			if (!(ci->filemask & (1ul << i)))
				continue;

			si = xmalloc(sizeof(*si));
			si->stage = i+1;
			si->mode = ci->stages[i].mode;
			oidcpy(&si->oid, &ci->stages[i].oid);
			string_list_append(conflicted_files, path)->util = si;
		}
	}
	/* string_list_sort() uses a stable sort, so we're good */
	string_list_sort(conflicted_files);
}

void merge_switch_to_result(struct merge_options *opt,
			    struct tree *head,
			    struct merge_result *result,
			    int update_worktree_and_index,
			    int display_update_msgs)
{
	assert(opt->priv == NULL);
	if (result->clean >= 0 && update_worktree_and_index) {
		trace2_region_enter("merge", "checkout", opt->repo);
		if (checkout(opt, head, result->tree)) {
			/* failure to function */
			result->clean = -1;
			merge_finalize(opt, result);
			trace2_region_leave("merge", "checkout", opt->repo);
			return;
		}
		trace2_region_leave("merge", "checkout", opt->repo);

		trace2_region_enter("merge", "record_conflicted", opt->repo);
		opt->priv = result->priv;
		if (record_conflicted_index_entries(opt)) {
			/* failure to function */
			opt->priv = NULL;
			result->clean = -1;
			merge_finalize(opt, result);
			trace2_region_leave("merge", "record_conflicted",
					    opt->repo);
			return;
		}
		opt->priv = NULL;
		trace2_region_leave("merge", "record_conflicted", opt->repo);

		trace2_region_enter("merge", "write_auto_merge", opt->repo);
		if (refs_update_ref(get_main_ref_store(opt->repo), "", "AUTO_MERGE",
				    &result->tree->object.oid, NULL, REF_NO_DEREF,
				    UPDATE_REFS_MSG_ON_ERR)) {
			/* failure to function */
			opt->priv = NULL;
			result->clean = -1;
			merge_finalize(opt, result);
			trace2_region_leave("merge", "write_auto_merge",
					    opt->repo);
			return;
		}
		trace2_region_leave("merge", "write_auto_merge", opt->repo);
	}
	if (display_update_msgs)
		merge_display_update_messages(opt, /* detailed */ 0, result);

	merge_finalize(opt, result);
}

void merge_finalize(struct merge_options *opt,
		    struct merge_result *result)
{
	if (opt->renormalize)
		git_attr_set_direction(GIT_ATTR_CHECKIN);
	assert(opt->priv == NULL);

	if (result->priv) {
		clear_or_reinit_internal_opts(result->priv, 0);
		FREE_AND_NULL(result->priv);
	}
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

struct commit *make_virtual_commit(struct repository *repo,
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
	struct mem_pool *pool = NULL;

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

	if (opt->msg_header_prefix)
		assert(opt->record_conflict_msgs_as_headers);

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
	if (result->_properly_initialized != 0 &&
	    result->_properly_initialized != RESULT_INITIALIZED)
		BUG("struct merge_result passed to merge_incore_*recursive() must be zeroed or filled with values from a previous run");
	assert(!!result->priv == !!result->_properly_initialized);
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

	/* Handle attr direction stuff for renormalization */
	if (opt->renormalize)
		git_attr_set_direction(GIT_ATTR_CHECKOUT);

	/* Initialization of opt->priv, our internal merge data */
	trace2_region_enter("merge", "allocate/init", opt->repo);
	if (opt->priv) {
		clear_or_reinit_internal_opts(opt->priv, 1);
		string_list_init_nodup(&opt->priv->conflicted_submodules);
		trace2_region_leave("merge", "allocate/init", opt->repo);
		return;
	}
	opt->priv = xcalloc(1, sizeof(*opt->priv));

	/* Initialization of various renames fields */
	renames = &opt->priv->renames;
	mem_pool_init(&opt->priv->pool, 0);
	pool = &opt->priv->pool;
	for (i = MERGE_SIDE1; i <= MERGE_SIDE2; i++) {
		strintmap_init_with_options(&renames->dirs_removed[i],
					    NOT_RELEVANT, pool, 0);
		strmap_init_with_options(&renames->dir_rename_count[i],
					 NULL, 1);
		strmap_init_with_options(&renames->dir_renames[i],
					 NULL, 0);
		/*
		 * relevant_sources uses -1 for the default, because we need
		 * to be able to distinguish not-in-strintmap from valid
		 * relevant_source values from enum file_rename_relevance.
		 * In particular, possibly_cache_new_pair() expects a negative
		 * value for not-found entries.
		 */
		strintmap_init_with_options(&renames->relevant_sources[i],
					    -1 /* explicitly invalid */,
					    pool, 0);
		strmap_init_with_options(&renames->cached_pairs[i],
					 NULL, 1);
		strset_init_with_options(&renames->cached_irrelevant[i],
					 NULL, 1);
		strset_init_with_options(&renames->cached_target_names[i],
					 NULL, 0);
	}
	for (i = MERGE_SIDE1; i <= MERGE_SIDE2; i++) {
		strintmap_init_with_options(&renames->deferred[i].possible_trivial_merges,
					    0, pool, 0);
		strset_init_with_options(&renames->deferred[i].target_dirs,
					 pool, 1);
		renames->deferred[i].trivial_merges_okay = 1; /* 1 == maybe */
	}

	/*
	 * Although we initialize opt->priv->paths with strdup_strings=0,
	 * that's just to avoid making yet another copy of an allocated
	 * string.  Putting the entry into paths means we are taking
	 * ownership, so we will later free it.
	 *
	 * In contrast, conflicted just has a subset of keys from paths, so
	 * we don't want to free those (it'd be a duplicate free).
	 */
	strmap_init_with_options(&opt->priv->paths, pool, 0);
	strmap_init_with_options(&opt->priv->conflicted, pool, 0);

	/*
	 * keys & string_lists in conflicts will sometimes need to outlive
	 * "paths", so it will have a copy of relevant keys.  It's probably
	 * a small subset of the overall paths that have special output.
	 */
	strmap_init(&opt->priv->conflicts);

	trace2_region_leave("merge", "allocate/init", opt->repo);
}

static void merge_check_renames_reusable(struct merge_options *opt,
					 struct merge_result *result,
					 struct tree *merge_base,
					 struct tree *side1,
					 struct tree *side2)
{
	struct rename_info *renames;
	struct tree **merge_trees;
	struct merge_options_internal *opti = result->priv;

	if (!opti)
		return;

	renames = &opti->renames;
	merge_trees = renames->merge_trees;

	/*
	 * Handle case where previous merge operation did not want cache to
	 * take effect, e.g. because rename/rename(1to1) makes it invalid.
	 */
	if (!merge_trees[0]) {
		assert(!merge_trees[0] && !merge_trees[1] && !merge_trees[2]);
		renames->cached_pairs_valid_side = 0; /* neither side valid */
		return;
	}

	/*
	 * Avoid using cached renames when directory rename detection is
	 * turned off.  Cached renames are far less important in that case,
	 * and they lead to testcases with an interesting intersection of
	 * effects from relevant renames optimization, trivial directory
	 * resolution optimization, and cached renames all converging when
	 * the target of a cached rename is in a directory that
	 * collect_merge_info() does not recurse into.  To avoid such
	 * problems, simply disable cached renames for this case (similar
	 * to the rename/rename(1to1) case; see the "disabling the
	 * optimization" comment near that case).
	 *
	 * This could be revisited in the future; see the commit message
	 * where this comment was added for some possible pointers.
	 */
	if (opt->detect_directory_renames == MERGE_DIRECTORY_RENAMES_NONE) {
		renames->cached_pairs_valid_side = 0; /* neither side valid */
		return;
	}

	/*
	 * Handle other cases; note that merge_trees[0..2] will only
	 * be NULL if opti is, or if all three were manually set to
	 * NULL by e.g. rename/rename(1to1) handling.
	 */
	assert(merge_trees[0] && merge_trees[1] && merge_trees[2]);

	/* Check if we meet a condition for re-using cached_pairs */
	if (oideq(&merge_base->object.oid, &merge_trees[2]->object.oid) &&
	    oideq(&side1->object.oid, &result->tree->object.oid))
		renames->cached_pairs_valid_side = MERGE_SIDE1;
	else if (oideq(&merge_base->object.oid, &merge_trees[1]->object.oid) &&
		 oideq(&side2->object.oid, &result->tree->object.oid))
		renames->cached_pairs_valid_side = MERGE_SIDE2;
	else
		renames->cached_pairs_valid_side = 0; /* neither side valid */
}

/*** Function Grouping: merge_incore_*() and their internal variants ***/

static void move_opt_priv_to_result_priv(struct merge_options *opt,
					 struct merge_result *result)
{
	/*
	 * opt->priv and result->priv are a bit weird.  opt->priv contains
	 * information that we can re-use in subsequent merge operations to
	 * enable our cached renames optimization.  The best way to provide
	 * that to subsequent merges is putting it in result->priv.
	 * However, putting it directly there would mean retrofitting lots
	 * of functions in this file to also take a merge_result pointer,
	 * which is ugly and annoying.  So, we just make sure at the end of
	 * the merge (the outer merge if there are internal recursive ones)
	 * to move it.
	 */
	assert(opt->priv && !result->priv);
	result->priv = opt->priv;
	result->_properly_initialized = RESULT_INITIALIZED;
	opt->priv = NULL;
}

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

redo:
	trace2_region_enter("merge", "collect_merge_info", opt->repo);
	if (collect_merge_info(opt, merge_base, side1, side2) != 0) {
		/*
		 * TRANSLATORS: The %s arguments are: 1) tree hash of a merge
		 * base, and 2-3) the trees for the two trees we're merging.
		 */
		error(_("collecting merge info failed for trees %s, %s, %s"),
		    oid_to_hex(&merge_base->object.oid),
		    oid_to_hex(&side1->object.oid),
		    oid_to_hex(&side2->object.oid));
		result->clean = -1;
		move_opt_priv_to_result_priv(opt, result);
		return;
	}
	trace2_region_leave("merge", "collect_merge_info", opt->repo);

	trace2_region_enter("merge", "renames", opt->repo);
	result->clean = detect_and_process_renames(opt);
	trace2_region_leave("merge", "renames", opt->repo);
	if (opt->priv->renames.redo_after_renames == 2) {
		trace2_region_enter("merge", "reset_maps", opt->repo);
		clear_or_reinit_internal_opts(opt->priv, 1);
		trace2_region_leave("merge", "reset_maps", opt->repo);
		goto redo;
	}

	trace2_region_enter("merge", "process_entries", opt->repo);
	if (process_entries(opt, &working_tree_oid) < 0)
		result->clean = -1;
	trace2_region_leave("merge", "process_entries", opt->repo);

	/* Set return values */
	result->path_messages = &opt->priv->conflicts;

	if (result->clean >= 0) {
		if (!opt->mergeability_only) {
			result->tree = parse_tree_indirect(&working_tree_oid);
			if (!result->tree)
				die(_("unable to read tree (%s)"),
				    oid_to_hex(&working_tree_oid));
		}
		/* existence of conflicted entries implies unclean */
		result->clean &= strmap_empty(&opt->priv->conflicted);
	}
	if (!opt->priv->call_depth || result->clean < 0)
		move_opt_priv_to_result_priv(opt, result);
}

/*
 * Originally from merge_recursive_internal(); somewhat adapted, though.
 */
static void merge_ort_internal(struct merge_options *opt,
			       const struct commit_list *_merge_bases,
			       struct commit *h1,
			       struct commit *h2,
			       struct merge_result *result)
{
	struct commit_list *merge_bases = copy_commit_list(_merge_bases);
	struct commit *next;
	struct commit *merged_merge_bases;
	const char *ancestor_name;
	struct strbuf merge_base_abbrev = STRBUF_INIT;

	if (!merge_bases) {
		if (repo_get_merge_bases(the_repository, h1, h2,
					 &merge_bases) < 0) {
			result->clean = -1;
			goto out;
		}
		/* See merge-ort.h:merge_incore_recursive() declaration NOTE */
		merge_bases = reverse_commit_list(merge_bases);
	}

	merged_merge_bases = pop_commit(&merge_bases);
	if (!merged_merge_bases) {
		/* if there is no common ancestor, use an empty tree */
		struct tree *tree;

		tree = lookup_tree(opt->repo, opt->repo->hash_algo->empty_tree);
		merged_merge_bases = make_virtual_commit(opt->repo, tree,
							 "ancestor");
		ancestor_name = "empty tree";
	} else if (merge_bases) {
		ancestor_name = "merged common ancestors";
	} else if (opt->ancestor) {
		ancestor_name = opt->ancestor;
	} else {
		strbuf_add_unique_abbrev(&merge_base_abbrev,
					 &merged_merge_bases->object.oid,
					 DEFAULT_ABBREV);
		ancestor_name = merge_base_abbrev.buf;
	}

	for (next = pop_commit(&merge_bases); next;
	     next = pop_commit(&merge_bases)) {
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
		merge_ort_internal(opt, NULL, prev, next, result);
		if (result->clean < 0)
			goto out;
		opt->branch1 = saved_b1;
		opt->branch2 = saved_b2;
		opt->priv->call_depth--;

		merged_merge_bases = make_virtual_commit(opt->repo,
							 result->tree,
							 "merged tree");
		commit_list_insert(prev, &merged_merge_bases->parents);
		commit_list_insert(next, &merged_merge_bases->parents->next);

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

out:
	free_commit_list(merge_bases);
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
	merge_check_renames_reusable(opt, result, merge_base, side1, side2);
	merge_start(opt, result);
	/*
	 * Record the trees used in this merge, so if there's a next merge in
	 * a cherry-pick or rebase sequence it might be able to take advantage
	 * of the cached_pairs in that next merge.
	 */
	opt->priv->renames.merge_trees[0] = merge_base;
	opt->priv->renames.merge_trees[1] = side1;
	opt->priv->renames.merge_trees[2] = side2;
	trace2_region_leave("merge", "merge_start", opt->repo);

	merge_ort_nonrecursive_internal(opt, merge_base, side1, side2, result);
	trace2_region_leave("merge", "incore_nonrecursive", opt->repo);
}

void merge_incore_recursive(struct merge_options *opt,
			    const struct commit_list *merge_bases,
			    struct commit *side1,
			    struct commit *side2,
			    struct merge_result *result)
{
	trace2_region_enter("merge", "incore_recursive", opt->repo);

	/*
	 * We set the ancestor label based on the merge_bases...but we
	 * allow one exception through so that builtin/am can override
	 * with its constructed fake ancestor.
	 */
	assert(opt->ancestor == NULL ||
	       (merge_bases && !merge_bases->next));

	trace2_region_enter("merge", "merge_start", opt->repo);
	merge_start(opt, result);
	trace2_region_leave("merge", "merge_start", opt->repo);

	merge_ort_internal(opt, merge_bases, side1, side2, result);
	trace2_region_leave("merge", "incore_recursive", opt->repo);
}

static void merge_recursive_config(struct merge_options *opt, int ui)
{
	char *value = NULL;
	int renormalize = 0;
	git_config_get_int("merge.verbosity", &opt->verbosity);
	git_config_get_int("diff.renamelimit", &opt->rename_limit);
	git_config_get_int("merge.renamelimit", &opt->rename_limit);
	git_config_get_bool("merge.renormalize", &renormalize);
	opt->renormalize = renormalize;
	if (!git_config_get_string("diff.renames", &value)) {
		opt->detect_renames = git_config_rename("diff.renames", value);
		free(value);
	}
	if (!git_config_get_string("merge.renames", &value)) {
		opt->detect_renames = git_config_rename("merge.renames", value);
		free(value);
	}
	if (!git_config_get_string("merge.directoryrenames", &value)) {
		int boolval = git_parse_maybe_bool(value);
		if (0 <= boolval) {
			opt->detect_directory_renames = boolval ?
				MERGE_DIRECTORY_RENAMES_TRUE :
				MERGE_DIRECTORY_RENAMES_NONE;
		} else if (!strcasecmp(value, "conflict")) {
			opt->detect_directory_renames =
				MERGE_DIRECTORY_RENAMES_CONFLICT;
		} /* avoid erroring on values from future versions of git */
		free(value);
	}
	if (ui) {
		if (!git_config_get_string("diff.algorithm", &value)) {
			long diff_algorithm = parse_algorithm_value(value);
			if (diff_algorithm < 0)
				die(_("unknown value for config '%s': %s"), "diff.algorithm", value);
			opt->xdl_opts = (opt->xdl_opts & ~XDF_DIFF_ALGORITHM_MASK) | diff_algorithm;
			free(value);
		}
	}
	git_config(git_xmerge_config, NULL);
}

static void init_merge_options(struct merge_options *opt,
			struct repository *repo, int ui)
{
	const char *merge_verbosity;
	memset(opt, 0, sizeof(struct merge_options));

	opt->repo = repo;

	opt->detect_renames = -1;
	opt->detect_directory_renames = MERGE_DIRECTORY_RENAMES_CONFLICT;
	opt->rename_limit = -1;

	opt->verbosity = 2;
	opt->buffer_output = 1;
	strbuf_init(&opt->obuf, 0);

	opt->renormalize = 0;

	opt->conflict_style = -1;
	opt->xdl_opts = DIFF_WITH_ALG(opt, HISTOGRAM_DIFF);

	merge_recursive_config(opt, ui);
	merge_verbosity = getenv("GIT_MERGE_VERBOSITY");
	if (merge_verbosity)
		opt->verbosity = strtol(merge_verbosity, NULL, 10);
	if (opt->verbosity >= 5)
		opt->buffer_output = 0;
}

void init_ui_merge_options(struct merge_options *opt,
			struct repository *repo)
{
	init_merge_options(opt, repo, 1);
}

void init_basic_merge_options(struct merge_options *opt,
			struct repository *repo)
{
	init_merge_options(opt, repo, 0);
}

/*
 * For now, members of merge_options do not need deep copying, but
 * it may change in the future, in which case we would need to update
 * this, and also make a matching change to clear_merge_options() to
 * release the resources held by a copied instance.
 */
void copy_merge_options(struct merge_options *dst, struct merge_options *src)
{
	*dst = *src;
}

void clear_merge_options(struct merge_options *opt UNUSED)
{
	; /* no-op as our copy is shallow right now */
}

int parse_merge_opt(struct merge_options *opt, const char *s)
{
	const char *arg;

	if (!s || !*s)
		return -1;
	if (!strcmp(s, "ours"))
		opt->recursive_variant = MERGE_VARIANT_OURS;
	else if (!strcmp(s, "theirs"))
		opt->recursive_variant = MERGE_VARIANT_THEIRS;
	else if (!strcmp(s, "subtree"))
		opt->subtree_shift = "";
	else if (skip_prefix(s, "subtree=", &arg))
		opt->subtree_shift = arg;
	else if (!strcmp(s, "patience"))
		opt->xdl_opts = DIFF_WITH_ALG(opt, PATIENCE_DIFF);
	else if (!strcmp(s, "histogram"))
		opt->xdl_opts = DIFF_WITH_ALG(opt, HISTOGRAM_DIFF);
	else if (skip_prefix(s, "diff-algorithm=", &arg)) {
		long value = parse_algorithm_value(arg);
		if (value < 0)
			return -1;
		/* clear out previous settings */
		DIFF_XDL_CLR(opt, NEED_MINIMAL);
		opt->xdl_opts &= ~XDF_DIFF_ALGORITHM_MASK;
		opt->xdl_opts |= value;
	}
	else if (!strcmp(s, "ignore-space-change"))
		DIFF_XDL_SET(opt, IGNORE_WHITESPACE_CHANGE);
	else if (!strcmp(s, "ignore-all-space"))
		DIFF_XDL_SET(opt, IGNORE_WHITESPACE);
	else if (!strcmp(s, "ignore-space-at-eol"))
		DIFF_XDL_SET(opt, IGNORE_WHITESPACE_AT_EOL);
	else if (!strcmp(s, "ignore-cr-at-eol"))
		DIFF_XDL_SET(opt, IGNORE_CR_AT_EOL);
	else if (!strcmp(s, "renormalize"))
		opt->renormalize = 1;
	else if (!strcmp(s, "no-renormalize"))
		opt->renormalize = 0;
	else if (!strcmp(s, "no-renames"))
		opt->detect_renames = 0;
	else if (!strcmp(s, "find-renames")) {
		opt->detect_renames = 1;
		opt->rename_score = 0;
	}
	else if (skip_prefix(s, "find-renames=", &arg) ||
		 skip_prefix(s, "rename-threshold=", &arg)) {
		if ((opt->rename_score = parse_rename_score(&arg)) == -1 || *arg != 0)
			return -1;
		opt->detect_renames = 1;
	}
	/*
	 * Please update $__git_merge_strategy_options in
	 * git-completion.bash when you add new options
	 */
	else
		return -1;
	return 0;
}
