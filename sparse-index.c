#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "environment.h"
#include "ewah/ewok.h"
#include "gettext.h"
#include "name-hash.h"
#include "read-cache-ll.h"
#include "repository.h"
#include "sparse-index.h"
#include "tree.h"
#include "pathspec.h"
#include "trace2.h"
#include "cache-tree.h"
#include "config.h"
#include "dir.h"
#include "fsmonitor-ll.h"
#include "advice.h"

/**
 * This global is used by expand_index() to determine if we should give the
 * advice for advice.sparseIndexExpanded when expanding a sparse index to a full
 * one. However, this is sometimes done on purpose, such as in the sparse-checkout
 * builtin, even when index.sparse=false. This may be disabled in
 * convert_to_sparse() or by commands that know they will lead to a full
 * expansion, but this message is not actionable.
 */
int give_advice_on_expansion = 1;
#define ADVICE_MSG \
	"The sparse index is expanding to a full index, a slow operation.\n"   \
	"Your working directory likely has contents that are outside of\n"     \
	"your sparse-checkout patterns. Use 'git sparse-checkout list' to\n"   \
	"see your sparse-checkout definition and compare it to your working\n" \
	"directory contents. Running 'git clean' may assist in this cleanup."

struct modify_index_context {
	struct index_state *write;
	struct pattern_list *pl;
};

static struct cache_entry *construct_sparse_dir_entry(
				struct index_state *istate,
				const char *sparse_dir,
				struct cache_tree *tree)
{
	struct cache_entry *de;

	de = make_cache_entry(istate, S_IFDIR, &tree->oid, sparse_dir, 0, 0);

	de->ce_flags |= CE_SKIP_WORKTREE;
	return de;
}

/*
 * Returns the number of entries "inserted" into the index.
 */
static int convert_to_sparse_rec(struct index_state *istate,
				 int num_converted,
				 int start, int end,
				 const char *ct_path, size_t ct_pathlen,
				 struct cache_tree *ct)
{
	int i, can_convert = 1;
	int start_converted = num_converted;
	struct strbuf child_path = STRBUF_INIT;

	/*
	 * Is the current path outside of the sparse cone?
	 * Then check if the region can be replaced by a sparse
	 * directory entry (everything is sparse and merged).
	 */
	if (path_in_sparse_checkout(ct_path, istate))
		can_convert = 0;

	for (i = start; can_convert && i < end; i++) {
		struct cache_entry *ce = istate->cache[i];

		if (ce_stage(ce) ||
		    S_ISGITLINK(ce->ce_mode) ||
		    !(ce->ce_flags & CE_SKIP_WORKTREE))
			can_convert = 0;
	}

	if (can_convert) {
		struct cache_entry *se;
		se = construct_sparse_dir_entry(istate, ct_path, ct);

		istate->cache[num_converted++] = se;
		return 1;
	}

	for (i = start; i < end; ) {
		int count, span, pos = -1;
		const char *base, *slash;
		struct cache_entry *ce = istate->cache[i];

		/*
		 * Detect if this is a normal entry outside of any subtree
		 * entry.
		 */
		base = ce->name + ct_pathlen;
		slash = strchr(base, '/');

		if (slash)
			pos = cache_tree_subtree_pos(ct, base, slash - base);

		if (pos < 0) {
			istate->cache[num_converted++] = ce;
			i++;
			continue;
		}

		strbuf_setlen(&child_path, 0);
		strbuf_add(&child_path, ce->name, slash - ce->name + 1);

		span = ct->down[pos]->cache_tree->entry_count;
		count = convert_to_sparse_rec(istate,
					      num_converted, i, i + span,
					      child_path.buf, child_path.len,
					      ct->down[pos]->cache_tree);
		num_converted += count;
		i += span;
	}

	strbuf_release(&child_path);
	return num_converted - start_converted;
}

int set_sparse_index_config(struct repository *repo, int enable)
{
	int res = repo_config_set_worktree_gently(repo,
						  "index.sparse",
						  enable ? "true" : "false");
	prepare_repo_settings(repo);
	repo->settings.sparse_index = enable;
	return res;
}

static int index_has_unmerged_entries(struct index_state *istate)
{
	int i;
	for (i = 0; i < istate->cache_nr; i++) {
		if (ce_stage(istate->cache[i]))
			return 1;
	}

	return 0;
}

int is_sparse_index_allowed(struct index_state *istate, int flags)
{
	if (!core_apply_sparse_checkout || !core_sparse_checkout_cone)
		return 0;

	if (!(flags & SPARSE_INDEX_MEMORY_ONLY)) {
		int test_env;

		/*
		 * The sparse index is not (yet) integrated with a split index.
		 */
		if (istate->split_index || git_env_bool("GIT_TEST_SPLIT_INDEX", 0))
			return 0;
		/*
		 * The GIT_TEST_SPARSE_INDEX environment variable triggers the
		 * index.sparse config variable to be on.
		 */
		test_env = git_env_bool("GIT_TEST_SPARSE_INDEX", -1);
		if (test_env >= 0)
			set_sparse_index_config(istate->repo, test_env);

		/*
		 * Only convert to sparse if index.sparse is set.
		 */
		prepare_repo_settings(istate->repo);
		if (!istate->repo->settings.sparse_index)
			return 0;
	}

	if (init_sparse_checkout_patterns(istate))
		return 0;

	/*
	 * We need cone-mode patterns to use sparse-index. If a user edits
	 * their sparse-checkout file manually, then we can detect during
	 * parsing that they are not actually using cone-mode patterns and
	 * hence we need to abort this conversion _without error_. Warnings
	 * already exist in the pattern parsing to inform the user of their
	 * bad patterns.
	 */
	if (!istate->sparse_checkout_patterns->use_cone_patterns)
		return 0;

	return 1;
}

int convert_to_sparse(struct index_state *istate, int flags)
{
	/*
	 * If the index is already sparse, empty, or otherwise
	 * cannot be converted to sparse, do not convert.
	 */
	if (istate->sparse_index == INDEX_COLLAPSED || !istate->cache_nr ||
	    !is_sparse_index_allowed(istate, flags))
		return 0;

	/*
	 * If we are purposefully collapsing a full index, then don't give
	 * advice when it is expanded later.
	 */
	give_advice_on_expansion = 0;

	/*
	 * NEEDSWORK: If we have unmerged entries, then stay full.
	 * Unmerged entries prevent the cache-tree extension from working.
	 */
	if (index_has_unmerged_entries(istate))
		return 0;

	if (!cache_tree_fully_valid(istate->cache_tree)) {
		/* Clear and recompute the cache-tree */
		cache_tree_free(&istate->cache_tree);

		/*
		 * Silently return if there is a problem with the cache tree update,
		 * which might just be due to a conflict state in some entry.
		 *
		 * This might create new tree objects, so be sure to use
		 * WRITE_TREE_MISSING_OK.
		 */
		if (cache_tree_update(istate, WRITE_TREE_MISSING_OK))
			return 0;
	}

	remove_fsmonitor(istate);

	trace2_region_enter("index", "convert_to_sparse", istate->repo);
	istate->cache_nr = convert_to_sparse_rec(istate,
						 0, 0, istate->cache_nr,
						 "", 0, istate->cache_tree);

	/* Clear and recompute the cache-tree */
	cache_tree_free(&istate->cache_tree);
	cache_tree_update(istate, 0);

	istate->fsmonitor_has_run_once = 0;
	ewah_free(istate->fsmonitor_dirty);
	istate->fsmonitor_dirty = NULL;
	FREE_AND_NULL(istate->fsmonitor_last_update);

	istate->sparse_index = INDEX_COLLAPSED;
	trace2_region_leave("index", "convert_to_sparse", istate->repo);
	return 0;
}

static void set_index_entry(struct index_state *istate, int nr, struct cache_entry *ce)
{
	ALLOC_GROW(istate->cache, nr + 1, istate->cache_alloc);

	istate->cache[nr] = ce;
	add_name_hash(istate, ce);
}

static int add_path_to_index(const struct object_id *oid,
			     struct strbuf *base, const char *path,
			     unsigned int mode, void *context)
{
	struct modify_index_context *ctx = (struct modify_index_context *)context;
	struct cache_entry *ce;
	size_t len = base->len;

	if (S_ISDIR(mode)) {
		int dtype;
		size_t baselen = base->len;
		if (!ctx->pl)
			return READ_TREE_RECURSIVE;

		/*
		 * Have we expanded to a point outside of the sparse-checkout?
		 *
		 * Artificially pad the path name with a slash "/" to
		 * indicate it as a directory, and add an arbitrary file
		 * name ("-") so we can consider base->buf as a file name
		 * to match against the cone-mode patterns.
		 *
		 * If we compared just "path", then we would expand more
		 * than we should. Since every file at root is always
		 * included, we would expand every directory at root at
		 * least one level deep instead of using sparse directory
		 * entries.
		 */
		strbuf_addstr(base, path);
		strbuf_add(base, "/-", 2);

		if (path_matches_pattern_list(base->buf, base->len,
					      NULL, &dtype,
					      ctx->pl, ctx->write)) {
			strbuf_setlen(base, baselen);
			return READ_TREE_RECURSIVE;
		}

		/*
		 * The path "{base}{path}/" is a sparse directory. Create the correct
		 * name for inserting the entry into the index.
		 */
		strbuf_setlen(base, base->len - 1);
	} else {
		strbuf_addstr(base, path);
	}

	ce = make_cache_entry(ctx->write, mode, oid, base->buf, 0, 0);
	ce->ce_flags |= CE_SKIP_WORKTREE | CE_EXTENDED;
	set_index_entry(ctx->write, ctx->write->cache_nr++, ce);

	strbuf_setlen(base, len);
	return 0;
}

void expand_index(struct index_state *istate, struct pattern_list *pl)
{
	int i;
	struct index_state *full;
	struct strbuf base = STRBUF_INIT;
	const char *tr_region;
	struct modify_index_context ctx;

	/*
	 * If the index is already full, then keep it full. We will convert
	 * it to a sparse index on write, if possible.
	 */
	if (istate->sparse_index == INDEX_EXPANDED)
		return;

	/*
	 * If our index is sparse, but our new pattern set does not use
	 * cone mode patterns, then we need to expand the index before we
	 * continue. A NULL pattern set indicates a full expansion to a
	 * full index.
	 */
	if (pl && !pl->use_cone_patterns) {
		pl = NULL;
	} else {
		/*
		 * We might contract file entries into sparse-directory
		 * entries, and for that we will need the cache tree to
		 * be recomputed.
		 */
		cache_tree_free(&istate->cache_tree);

		/*
		 * If there is a problem creating the cache tree, then we
		 * need to expand to a full index since we cannot satisfy
		 * the current request as a sparse index.
		 */
		if (cache_tree_update(istate, 0))
			pl = NULL;
	}

	if (!pl && give_advice_on_expansion) {
		give_advice_on_expansion = 0;
		advise_if_enabled(ADVICE_SPARSE_INDEX_EXPANDED,
				  _(ADVICE_MSG));
	}

	/*
	 * A NULL pattern set indicates we are expanding a full index, so
	 * we use a special region name that indicates the full expansion.
	 * This is used by test cases, but also helps to differentiate the
	 * two cases.
	 */
	tr_region = pl ? "expand_index" : "ensure_full_index";
	trace2_region_enter("index", tr_region, istate->repo);

	/* initialize basics of new index */
	full = xcalloc(1, sizeof(struct index_state));
	memcpy(full, istate, sizeof(struct index_state));

	/*
	 * This slightly-misnamed 'full' index might still be sparse if we
	 * are only modifying the list of sparse directories. This hinges
	 * on whether we have a non-NULL pattern list.
	 */
	full->sparse_index = pl ? INDEX_PARTIALLY_SPARSE : INDEX_EXPANDED;

	/* then change the necessary things */
	full->cache_alloc = (3 * istate->cache_alloc) / 2;
	full->cache_nr = 0;
	ALLOC_ARRAY(full->cache, full->cache_alloc);

	ctx.write = full;
	ctx.pl = pl;

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		struct tree *tree;
		struct pathspec ps;
		int dtype;

		if (!S_ISSPARSEDIR(ce->ce_mode)) {
			set_index_entry(full, full->cache_nr++, ce);
			continue;
		}

		/* We now have a sparse directory entry. Should we expand? */
		if (pl &&
		    path_matches_pattern_list(ce->name, ce->ce_namelen,
					      NULL, &dtype,
					      pl, istate) == NOT_MATCHED) {
			set_index_entry(full, full->cache_nr++, ce);
			continue;
		}

		if (!(ce->ce_flags & CE_SKIP_WORKTREE))
			warning(_("index entry is a directory, but not sparse (%08x)"),
				ce->ce_flags);

		/* recursively walk into cd->name */
		tree = lookup_tree(istate->repo, &ce->oid);

		memset(&ps, 0, sizeof(ps));
		ps.recursive = 1;
		ps.has_wildcard = 1;
		ps.max_depth = -1;

		strbuf_setlen(&base, 0);
		strbuf_add(&base, ce->name, strlen(ce->name));

		read_tree_at(istate->repo, tree, &base, 0, &ps,
			     add_path_to_index, &ctx);

		/* free directory entries. full entries are re-used */
		discard_cache_entry(ce);
	}

	/* Copy back into original index. */
	memcpy(&istate->name_hash, &full->name_hash, sizeof(full->name_hash));
	memcpy(&istate->dir_hash, &full->dir_hash, sizeof(full->dir_hash));
	istate->sparse_index = pl ? INDEX_PARTIALLY_SPARSE : INDEX_EXPANDED;
	free(istate->cache);
	istate->cache = full->cache;
	istate->cache_nr = full->cache_nr;
	istate->cache_alloc = full->cache_alloc;
	istate->fsmonitor_has_run_once = 0;
	ewah_free(istate->fsmonitor_dirty);
	istate->fsmonitor_dirty = NULL;
	FREE_AND_NULL(istate->fsmonitor_last_update);

	strbuf_release(&base);
	free(full);

	/* Clear and recompute the cache-tree */
	cache_tree_free(&istate->cache_tree);
	cache_tree_update(istate, 0);

	trace2_region_leave("index", tr_region, istate->repo);
}

void ensure_full_index(struct index_state *istate)
{
	if (!istate)
		BUG("ensure_full_index() must get an index!");
	expand_index(istate, NULL);
}

void ensure_correct_sparsity(struct index_state *istate)
{
	/*
	 * If the index can be sparse, make it sparse. Otherwise,
	 * ensure the index is full.
	 */
	if (is_sparse_index_allowed(istate, 0))
		convert_to_sparse(istate, 0);
	else
		ensure_full_index(istate);
}

struct path_found_data {
	/**
	 * The path stored in 'dir', if non-empty, corresponds to the most-
	 * recent path that we checked where:
	 *
	 *   1. The path should be a directory, according to the index.
	 *   2. The path does not exist.
	 *   3. The parent path _does_ exist. (This may be the root of the
	 *      working directory.)
	 */
	struct strbuf dir;
	size_t lstat_count;
};

#define PATH_FOUND_DATA_INIT { \
	.dir = STRBUF_INIT \
}

static void clear_path_found_data(struct path_found_data *data)
{
	strbuf_release(&data->dir);
}

/**
 * Return the length of the longest common substring that ends in a
 * slash ('/') to indicate the longest common parent directory. Returns
 * zero if no common directory exists.
 */
static size_t max_common_dir_prefix(const char *path1, const char *path2)
{
	size_t common_prefix = 0;
	for (size_t i = 0; path1[i] && path2[i]; i++) {
		if (path1[i] != path2[i])
			break;

		/*
		 * If they agree at a directory separator, then add one
		 * to make sure it is included in the common prefix string.
		 */
		if (path1[i] == '/')
			common_prefix = i + 1;
	}

	return common_prefix;
}

static int path_found(const char *path, struct path_found_data *data)
{
	struct stat st;
	size_t common_prefix;

	/*
	 * If data->dir is non-empty, then it contains a path that doesn't
	 * exist, including an ending slash ('/'). If it is a prefix of 'path',
	 * then we can return 0.
	 */
	if (data->dir.len && !memcmp(path, data->dir.buf, data->dir.len))
		return 0;

	/*
	 * Otherwise, we must check if the current path exists. If it does, then
	 * return 1. The cached directory will be skipped until we come across
	 * a missing path again.
	 */
	data->lstat_count++;
	if (!lstat(path, &st))
		return 1;

	/*
	 * At this point, we know that 'path' doesn't exist, and we know that
	 * the parent directory of 'data->dir' does exist. Let's set 'data->dir'
	 * to be the top-most non-existing directory of 'path'. If the first
	 * parent of 'path' exists, then we will act as though 'path'
	 * corresponds to a directory (by adding a slash).
	 */
	common_prefix = max_common_dir_prefix(path, data->dir.buf);

	/*
	 * At this point, 'path' and 'data->dir' have a common existing parent
	 * directory given by path[0..common_prefix] (which could have length 0).
	 * We "grow" the data->dir buffer by checking for existing directories
	 * along 'path'.
	 */

	strbuf_setlen(&data->dir, common_prefix);
	while (1) {
		/* Find the next directory in 'path'. */
		const char *rest = path + data->dir.len;
		const char *next_slash = strchr(rest, '/');

		/*
		 * If there are no more slashes, then 'path' doesn't contain a
		 * non-existent _parent_ directory. Set 'data->dir' to be equal
		 * to 'path' plus an additional slash, so it can be used for
		 * caching in the future. The filename of 'path' is considered
		 * a non-existent directory.
		 *
		 * Note: if "{path}/" exists as a directory, then it will never
		 * appear as a prefix of other callers to this method, assuming
		 * the context from the clear_skip_worktree... methods. If this
		 * method is reused, then this must be reconsidered.
		 */
		if (!next_slash) {
			strbuf_addstr(&data->dir, rest);
			strbuf_addch(&data->dir, '/');
			break;
		}

		/*
		 * Now that we have a slash, let's grow 'data->dir' to include
		 * this slash, then test if we should stop.
		 */
		strbuf_add(&data->dir, rest, next_slash - rest + 1);

		/* If the parent dir doesn't exist, then stop here. */
		data->lstat_count++;
		if (lstat(data->dir.buf, &st))
			return 0;
	}

	/*
	 * At this point, 'data->dir' is equal to 'path' plus a slash character,
	 * and the parent directory of 'path' definitely exists. Moreover, we
	 * know that 'path' doesn't exist, or we would have returned 1 earlier.
	 */
	return 0;
}

static int clear_skip_worktree_from_present_files_sparse(struct index_state *istate)
{
	struct path_found_data data = PATH_FOUND_DATA_INIT;

	int path_count = 0;
	int to_restart = 0;

	trace2_region_enter("index", "clear_skip_worktree_from_present_files_sparse",
			    istate->repo);
	for (int i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];

		if (ce_skip_worktree(ce)) {
			path_count++;
			if (path_found(ce->name, &data)) {
				if (S_ISSPARSEDIR(ce->ce_mode)) {
					to_restart = 1;
					break;
				}
				ce->ce_flags &= ~CE_SKIP_WORKTREE;
			}
		}
	}

	trace2_data_intmax("index", istate->repo,
			   "sparse_path_count", path_count);
	trace2_data_intmax("index", istate->repo,
			   "sparse_lstat_count", data.lstat_count);
	trace2_region_leave("index", "clear_skip_worktree_from_present_files_sparse",
			    istate->repo);
	clear_path_found_data(&data);
	return to_restart;
}

static void clear_skip_worktree_from_present_files_full(struct index_state *istate)
{
	struct path_found_data data = PATH_FOUND_DATA_INIT;

	int path_count = 0;

	trace2_region_enter("index", "clear_skip_worktree_from_present_files_full",
			    istate->repo);
	for (int i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];

		if (S_ISSPARSEDIR(ce->ce_mode))
			BUG("ensure-full-index did not fully flatten?");

		if (ce_skip_worktree(ce)) {
			path_count++;
			if (path_found(ce->name, &data))
				ce->ce_flags &= ~CE_SKIP_WORKTREE;
		}
	}

	trace2_data_intmax("index", istate->repo,
			   "full_path_count", path_count);
	trace2_data_intmax("index", istate->repo,
			   "full_lstat_count", data.lstat_count);
	trace2_region_leave("index", "clear_skip_worktree_from_present_files_full",
			    istate->repo);
	clear_path_found_data(&data);
}

void clear_skip_worktree_from_present_files(struct index_state *istate)
{
	if (!core_apply_sparse_checkout ||
	    sparse_expect_files_outside_of_patterns)
		return;

	if (clear_skip_worktree_from_present_files_sparse(istate)) {
		ensure_full_index(istate);
		clear_skip_worktree_from_present_files_full(istate);
	}
}

/*
 * This static global helps avoid infinite recursion between
 * expand_to_path() and index_file_exists().
 */
static int in_expand_to_path = 0;

void expand_to_path(struct index_state *istate,
		    const char *path, size_t pathlen, int icase)
{
	struct strbuf path_mutable = STRBUF_INIT;
	size_t substr_len;

	/* prevent extra recursion */
	if (in_expand_to_path)
		return;

	if (!istate->sparse_index)
		return;

	in_expand_to_path = 1;

	/*
	 * We only need to actually expand a region if the
	 * following are both true:
	 *
	 * 1. 'path' is not already in the index.
	 * 2. Some parent directory of 'path' is a sparse directory.
	 */

	if (index_file_exists(istate, path, pathlen, icase))
		goto cleanup;

	strbuf_add(&path_mutable, path, pathlen);
	strbuf_addch(&path_mutable, '/');

	/* Check the name hash for all parent directories */
	substr_len = 0;
	while (substr_len < pathlen) {
		char temp;
		char *replace = strchr(path_mutable.buf + substr_len, '/');

		if (!replace)
			break;

		/* replace the character _after_ the slash */
		replace++;
		temp = *replace;
		*replace = '\0';
		substr_len = replace - path_mutable.buf;
		if (index_file_exists(istate, path_mutable.buf,
				      substr_len, icase)) {
			/*
			 * We found a parent directory in the name-hash
			 * hashtable, because only sparse directory entries
			 * have a trailing '/' character.  Since "path" wasn't
			 * in the index, perhaps it exists within this
			 * sparse-directory.  Expand accordingly.
			 */
			ensure_full_index(istate);
			break;
		}

		*replace = temp;
	}

cleanup:
	strbuf_release(&path_mutable);
	in_expand_to_path = 0;
}
