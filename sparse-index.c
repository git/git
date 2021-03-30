#include "cache.h"
#include "repository.h"
#include "sparse-index.h"
#include "tree.h"
#include "pathspec.h"
#include "trace2.h"
#include "cache-tree.h"
#include "config.h"
#include "dir.h"
#include "fsmonitor.h"

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
	enum pattern_match_result match;
	int dtype;
	struct strbuf child_path = STRBUF_INIT;
	struct pattern_list *pl = istate->sparse_checkout_patterns;

	/*
	 * Is the current path outside of the sparse cone?
	 * Then check if the region can be replaced by a sparse
	 * directory entry (everything is sparse and merged).
	 */
	match = path_matches_pattern_list(ct_path, ct_pathlen,
					  NULL, &dtype, pl, istate);
	if (match != NOT_MATCHED)
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

static int set_index_sparse_config(struct repository *repo, int enable)
{
	int res;
	char *config_path = repo_git_path(repo, "config.worktree");
	res = git_config_set_in_file_gently(config_path,
					    "index.sparse",
					    enable ? "true" : NULL);
	free(config_path);

	prepare_repo_settings(repo);
	repo->settings.sparse_index = 1;
	return res;
}

int set_sparse_index_config(struct repository *repo, int enable)
{
	int res = set_index_sparse_config(repo, enable);

	prepare_repo_settings(repo);
	repo->settings.sparse_index = enable;
	return res;
}

int convert_to_sparse(struct index_state *istate)
{
	int test_env;
	if (istate->split_index || istate->sparse_index ||
	    !core_apply_sparse_checkout || !core_sparse_checkout_cone)
		return 0;

	if (!istate->repo)
		istate->repo = the_repository;

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

	if (!istate->sparse_checkout_patterns) {
		istate->sparse_checkout_patterns = xcalloc(1, sizeof(struct pattern_list));
		if (get_sparse_checkout_patterns(istate->sparse_checkout_patterns) < 0)
			return 0;
	}

	if (!istate->sparse_checkout_patterns->use_cone_patterns) {
		warning(_("attempting to use sparse-index without cone mode"));
		return -1;
	}

	if (cache_tree_update(istate, 0)) {
		warning(_("unable to update cache-tree, staying full"));
		return -1;
	}

	remove_fsmonitor(istate);

	trace2_region_enter("index", "convert_to_sparse", istate->repo);
	istate->cache_nr = convert_to_sparse_rec(istate,
						 0, 0, istate->cache_nr,
						 "", 0, istate->cache_tree);

	/* Clear and recompute the cache-tree */
	cache_tree_free(&istate->cache_tree);
	cache_tree_update(istate, 0);

	istate->sparse_index = 1;
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
	struct index_state *istate = (struct index_state *)context;
	struct cache_entry *ce;
	size_t len = base->len;

	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	strbuf_addstr(base, path);

	ce = make_cache_entry(istate, mode, oid, base->buf, 0, 0);
	ce->ce_flags |= CE_SKIP_WORKTREE;
	set_index_entry(istate, istate->cache_nr++, ce);

	strbuf_setlen(base, len);
	return 0;
}

void ensure_full_index(struct index_state *istate)
{
	int i;
	struct index_state *full;
	struct strbuf base = STRBUF_INIT;

	if (!istate || !istate->sparse_index)
		return;

	if (!istate->repo)
		istate->repo = the_repository;

	trace2_region_enter("index", "ensure_full_index", istate->repo);

	/* initialize basics of new index */
	full = xcalloc(1, sizeof(struct index_state));
	memcpy(full, istate, sizeof(struct index_state));

	/* then change the necessary things */
	full->sparse_index = 0;
	full->cache_alloc = (3 * istate->cache_alloc) / 2;
	full->cache_nr = 0;
	ALLOC_ARRAY(full->cache, full->cache_alloc);

	for (i = 0; i < istate->cache_nr; i++) {
		struct cache_entry *ce = istate->cache[i];
		struct tree *tree;
		struct pathspec ps;

		if (!S_ISSPARSEDIR(ce->ce_mode)) {
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

		read_tree_at(istate->repo, tree, &base, &ps,
			     add_path_to_index, full);

		/* free directory entries. full entries are re-used */
		discard_cache_entry(ce);
	}

	/* Copy back into original index. */
	memcpy(&istate->name_hash, &full->name_hash, sizeof(full->name_hash));
	istate->sparse_index = 0;
	free(istate->cache);
	istate->cache = full->cache;
	istate->cache_nr = full->cache_nr;
	istate->cache_alloc = full->cache_alloc;

	strbuf_release(&base);
	free(full);

	/* Clear and recompute the cache-tree */
	cache_tree_free(&istate->cache_tree);
	cache_tree_update(istate, 0);

	trace2_region_leave("index", "ensure_full_index", istate->repo);
}
