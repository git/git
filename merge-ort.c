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
	struct string_list_item pi;  /* Path Info */
	struct conflict_info *ci; /* typed alias to pi.util (which is void*) */
	struct name_entry *p;
	size_t len;
	char *fullpath;
	const char *dirname = opti->current_dir_name;
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
		ret = traverse_trees(NULL, 3, t, &newinfo);
		opti->current_dir_name = original_dir_name;

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
	const char *toplevel_dir_placeholder = "";

	opt->priv->current_dir_name = toplevel_dir_placeholder;
	setup_traverse_info(&info, toplevel_dir_placeholder);
	info.fn = collect_merge_info_callback;
	info.data = opt;
	info.show_all_errors = 1;

	parse_tree(merge_base);
	parse_tree(side1);
	parse_tree(side2);
	init_tree_desc(t + 0, merge_base->buffer, merge_base->size);
	init_tree_desc(t + 1, side1->buffer, side1->size);
	init_tree_desc(t + 2, side2->buffer, side2->size);

	ret = traverse_trees(NULL, 3, t, &info);

	return ret;
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
