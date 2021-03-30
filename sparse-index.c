#include "cache.h"
#include "repository.h"
#include "sparse-index.h"
#include "tree.h"
#include "pathspec.h"
#include "trace2.h"

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

	trace2_region_leave("index", "ensure_full_index", istate->repo);
}
