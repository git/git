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

#include "diff.h"
#include "diffcore.h"
#include "strmap.h"
#include "tree.h"
#include "xdiff-interface.h"

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
	 * current_dir_name: temporary var used in collect_merge_info_callback()
	 *
	 * Used to set merged_info.directory_name; see documentation for that
	 * variable and the requirements placed on that field.
	 */
	const char *current_dir_name;

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

static int collect_merge_info(struct merge_options *opt,
			      struct tree *merge_base,
			      struct tree *side1,
			      struct tree *side2)
{
	/* TODO: Implement this using traverse_trees() */
	die("Not yet implemented.");
}

static int detect_and_process_renames(struct merge_options *opt,
				      struct tree *merge_base,
				      struct tree *side1,
				      struct tree *side2)
{
	int clean = 1;

	/*
	 * Rename detection works by detecting file similarity.  Here we use
	 * a really easy-to-implement scheme: files are similar IFF they have
	 * the same filename.  Therefore, by this scheme, there are no renames.
	 *
	 * TODO: Actually implement a real rename detection scheme.
	 */
	return clean;
}

static void process_entries(struct merge_options *opt,
			    struct object_id *result_oid)
{
	die("Not yet implemented.");
}

void merge_switch_to_result(struct merge_options *opt,
			    struct tree *head,
			    struct merge_result *result,
			    int update_worktree_and_index,
			    int display_update_msgs)
{
	die("Not yet implemented");
	merge_finalize(opt, result);
}

void merge_finalize(struct merge_options *opt,
		    struct merge_result *result)
{
	die("Not yet implemented");
}

static void merge_start(struct merge_options *opt, struct merge_result *result)
{
	/* Sanity checks on opt */
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

	/* Default to histogram diff.  Actually, just hardcode it...for now. */
	opt->xdl_opts = DIFF_WITH_ALG(opt, HISTOGRAM_DIFF);

	/* Initialization of opt->priv, our internal merge data */
	opt->priv = xcalloc(1, sizeof(*opt->priv));

	/*
	 * Although we initialize opt->priv->paths with strdup_strings=0,
	 * that's just to avoid making yet another copy of an allocated
	 * string.  Putting the entry into paths means we are taking
	 * ownership, so we will later free it.
	 *
	 * In contrast, conflicted just has a subset of keys from paths, so
	 * we don't want to free those (it'd be a duplicate free).
	 */
	strmap_init_with_options(&opt->priv->paths, NULL, 0);
	strmap_init_with_options(&opt->priv->conflicted, NULL, 0);
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

	collect_merge_info(opt, merge_base, side1, side2);
	result->clean = detect_and_process_renames(opt, merge_base,
						   side1, side2);
	process_entries(opt, &working_tree_oid);

	/* Set return values */
	result->tree = parse_tree_indirect(&working_tree_oid);
	/* existence of conflicted entries implies unclean */
	result->clean &= strmap_empty(&opt->priv->conflicted);
	if (!opt->priv->call_depth) {
		result->priv = opt->priv;
		opt->priv = NULL;
	}
}

void merge_incore_nonrecursive(struct merge_options *opt,
			       struct tree *merge_base,
			       struct tree *side1,
			       struct tree *side2,
			       struct merge_result *result)
{
	assert(opt->ancestor != NULL);
	merge_start(opt, result);
	merge_ort_nonrecursive_internal(opt, merge_base, side1, side2, result);
}

void merge_incore_recursive(struct merge_options *opt,
			    struct commit_list *merge_bases,
			    struct commit *side1,
			    struct commit *side2,
			    struct merge_result *result)
{
	die("Not yet implemented");
}
