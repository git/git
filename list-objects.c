#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "diff.h"
#include "tree-walk.h"
#include "revision.h"
#include "list-objects.h"
#include "list-objects-filter.h"
#include "list-objects-filter-options.h"
#include "packfile.h"
#include "object-store.h"
#include "trace.h"

struct traversal_context {
	struct rev_info *revs;
	show_object_fn show_object;
	show_commit_fn show_commit;
	void *show_data;
	struct filter *filter;
};

static void process_blob(struct traversal_context *ctx,
			 struct blob *blob,
			 struct strbuf *path,
			 const char *name)
{
	struct object *obj = &blob->object;
	size_t pathlen;
	enum list_objects_filter_result r;

	if (!ctx->revs->blob_objects)
		return;
	if (!obj)
		die("bad blob object");
	if (obj->flags & (UNINTERESTING | SEEN))
		return;

	/*
	 * Pre-filter known-missing objects when explicitly requested.
	 * Otherwise, a missing object error message may be reported
	 * later (depending on other filtering criteria).
	 *
	 * Note that this "--exclude-promisor-objects" pre-filtering
	 * may cause the actual filter to report an incomplete list
	 * of missing objects.
	 */
	if (ctx->revs->exclude_promisor_objects &&
	    !has_object_file(&obj->oid) &&
	    is_promisor_object(&obj->oid))
		return;

	pathlen = path->len;
	strbuf_addstr(path, name);
	r = list_objects_filter__filter_object(ctx->revs->repo,
					       LOFS_BLOB, obj,
					       path->buf, &path->buf[pathlen],
					       ctx->filter);
	if (r & LOFR_MARK_SEEN)
		obj->flags |= SEEN;
	if (r & LOFR_DO_SHOW)
		ctx->show_object(obj, path->buf, ctx->show_data);
	strbuf_setlen(path, pathlen);
}

/*
 * Processing a gitlink entry currently does nothing, since
 * we do not recurse into the subproject.
 *
 * We *could* eventually add a flag that actually does that,
 * which would involve:
 *  - is the subproject actually checked out?
 *  - if so, see if the subproject has already been added
 *    to the alternates list, and add it if not.
 *  - process the commit (or tag) the gitlink points to
 *    recursively.
 *
 * However, it's unclear whether there is really ever any
 * reason to see superprojects and subprojects as such a
 * "unified" object pool (potentially resulting in a totally
 * humongous pack - avoiding which was the whole point of
 * having gitlinks in the first place!).
 *
 * So for now, there is just a note that we *could* follow
 * the link, and how to do it. Whether it necessarily makes
 * any sense what-so-ever to ever do that is another issue.
 */
static void process_gitlink(struct traversal_context *ctx,
			    const unsigned char *sha1,
			    struct strbuf *path,
			    const char *name)
{
	/* Nothing to do */
}

static void process_tree(struct traversal_context *ctx,
			 struct tree *tree,
			 struct strbuf *base,
			 const char *name);

static void process_tree_contents(struct traversal_context *ctx,
				  struct tree *tree,
				  struct strbuf *base)
{
	struct tree_desc desc;
	struct name_entry entry;
	enum interesting match = ctx->revs->diffopt.pathspec.nr == 0 ?
		all_entries_interesting : entry_not_interesting;

	init_tree_desc(&desc, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		if (match != all_entries_interesting) {
			match = tree_entry_interesting(ctx->revs->repo->index,
						       &entry, base, 0,
						       &ctx->revs->diffopt.pathspec);
			if (match == all_entries_not_interesting)
				break;
			if (match == entry_not_interesting)
				continue;
		}

		if (S_ISDIR(entry.mode)) {
			struct tree *t = lookup_tree(ctx->revs->repo, &entry.oid);
			if (!t) {
				die(_("entry '%s' in tree %s has tree mode, "
				      "but is not a tree"),
				    entry.path, oid_to_hex(&tree->object.oid));
			}
			t->object.flags |= NOT_USER_GIVEN;
			process_tree(ctx, t, base, entry.path);
		}
		else if (S_ISGITLINK(entry.mode))
			process_gitlink(ctx, entry.oid.hash,
					base, entry.path);
		else {
			struct blob *b = lookup_blob(ctx->revs->repo, &entry.oid);
			if (!b) {
				die(_("entry '%s' in tree %s has blob mode, "
				      "but is not a blob"),
				    entry.path, oid_to_hex(&tree->object.oid));
			}
			b->object.flags |= NOT_USER_GIVEN;
			process_blob(ctx, b, base, entry.path);
		}
	}
}

static void process_tree(struct traversal_context *ctx,
			 struct tree *tree,
			 struct strbuf *base,
			 const char *name)
{
	struct object *obj = &tree->object;
	struct rev_info *revs = ctx->revs;
	int baselen = base->len;
	enum list_objects_filter_result r;
	int failed_parse;

	if (!revs->tree_objects)
		return;
	if (!obj)
		die("bad tree object");
	if (obj->flags & (UNINTERESTING | SEEN))
		return;

	failed_parse = parse_tree_gently(tree, 1);
	if (failed_parse) {
		if (revs->ignore_missing_links)
			return;

		/*
		 * Pre-filter known-missing tree objects when explicitly
		 * requested.  This may cause the actual filter to report
		 * an incomplete list of missing objects.
		 */
		if (revs->exclude_promisor_objects &&
		    is_promisor_object(&obj->oid))
			return;

		if (!revs->do_not_die_on_missing_tree)
			die("bad tree object %s", oid_to_hex(&obj->oid));
	}

	strbuf_addstr(base, name);
	r = list_objects_filter__filter_object(ctx->revs->repo,
					       LOFS_BEGIN_TREE, obj,
					       base->buf, &base->buf[baselen],
					       ctx->filter);
	if (r & LOFR_MARK_SEEN)
		obj->flags |= SEEN;
	if (r & LOFR_DO_SHOW)
		ctx->show_object(obj, base->buf, ctx->show_data);
	if (base->len)
		strbuf_addch(base, '/');

	if (r & LOFR_SKIP_TREE)
		trace_printf("Skipping contents of tree %s...\n", base->buf);
	else if (!failed_parse)
		process_tree_contents(ctx, tree, base);

	r = list_objects_filter__filter_object(ctx->revs->repo,
					       LOFS_END_TREE, obj,
					       base->buf, &base->buf[baselen],
					       ctx->filter);
	if (r & LOFR_MARK_SEEN)
		obj->flags |= SEEN;
	if (r & LOFR_DO_SHOW)
		ctx->show_object(obj, base->buf, ctx->show_data);

	strbuf_setlen(base, baselen);
	free_tree_buffer(tree);
}

static void mark_edge_parents_uninteresting(struct commit *commit,
					    struct rev_info *revs,
					    show_edge_fn show_edge)
{
	struct commit_list *parents;

	for (parents = commit->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;
		if (!(parent->object.flags & UNINTERESTING))
			continue;
		mark_tree_uninteresting(revs->repo, get_commit_tree(parent));
		if (revs->edge_hint && !(parent->object.flags & SHOWN)) {
			parent->object.flags |= SHOWN;
			show_edge(parent);
		}
	}
}

static void add_edge_parents(struct commit *commit,
			     struct rev_info *revs,
			     show_edge_fn show_edge,
			     struct oidset *set)
{
	struct commit_list *parents;

	for (parents = commit->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;
		struct tree *tree = get_commit_tree(parent);

		if (!tree)
			continue;

		oidset_insert(set, &tree->object.oid);

		if (!(parent->object.flags & UNINTERESTING))
			continue;
		tree->object.flags |= UNINTERESTING;

		if (revs->edge_hint && !(parent->object.flags & SHOWN)) {
			parent->object.flags |= SHOWN;
			show_edge(parent);
		}
	}
}

void mark_edges_uninteresting(struct rev_info *revs,
			      show_edge_fn show_edge,
			      int sparse)
{
	struct commit_list *list;
	int i;

	if (sparse) {
		struct oidset set;
		oidset_init(&set, 16);

		for (list = revs->commits; list; list = list->next) {
			struct commit *commit = list->item;
			struct tree *tree = get_commit_tree(commit);

			if (commit->object.flags & UNINTERESTING)
				tree->object.flags |= UNINTERESTING;

			oidset_insert(&set, &tree->object.oid);
			add_edge_parents(commit, revs, show_edge, &set);
		}

		mark_trees_uninteresting_sparse(revs->repo, &set);
		oidset_clear(&set);
	} else {
		for (list = revs->commits; list; list = list->next) {
			struct commit *commit = list->item;
			if (commit->object.flags & UNINTERESTING) {
				mark_tree_uninteresting(revs->repo,
							get_commit_tree(commit));
				if (revs->edge_hint_aggressive && !(commit->object.flags & SHOWN)) {
					commit->object.flags |= SHOWN;
					show_edge(commit);
				}
				continue;
			}
			mark_edge_parents_uninteresting(commit, revs, show_edge);
		}
	}

	if (revs->edge_hint_aggressive) {
		for (i = 0; i < revs->cmdline.nr; i++) {
			struct object *obj = revs->cmdline.rev[i].item;
			struct commit *commit = (struct commit *)obj;
			if (obj->type != OBJ_COMMIT || !(obj->flags & UNINTERESTING))
				continue;
			mark_tree_uninteresting(revs->repo,
						get_commit_tree(commit));
			if (!(obj->flags & SHOWN)) {
				obj->flags |= SHOWN;
				show_edge(commit);
			}
		}
	}
}

static void add_pending_tree(struct rev_info *revs, struct tree *tree)
{
	add_pending_object(revs, &tree->object, "");
}

static void traverse_trees_and_blobs(struct traversal_context *ctx,
				     struct strbuf *base)
{
	int i;

	assert(base->len == 0);

	for (i = 0; i < ctx->revs->pending.nr; i++) {
		struct object_array_entry *pending = ctx->revs->pending.objects + i;
		struct object *obj = pending->item;
		const char *name = pending->name;
		const char *path = pending->path;
		if (obj->flags & (UNINTERESTING | SEEN))
			continue;
		if (obj->type == OBJ_TAG) {
			obj->flags |= SEEN;
			ctx->show_object(obj, name, ctx->show_data);
			continue;
		}
		if (!path)
			path = "";
		if (obj->type == OBJ_TREE) {
			process_tree(ctx, (struct tree *)obj, base, path);
			continue;
		}
		if (obj->type == OBJ_BLOB) {
			process_blob(ctx, (struct blob *)obj, base, path);
			continue;
		}
		die("unknown pending object %s (%s)",
		    oid_to_hex(&obj->oid), name);
	}
	object_array_clear(&ctx->revs->pending);
}

static void do_traverse(struct traversal_context *ctx)
{
	struct commit *commit;
	struct strbuf csp; /* callee's scratch pad */
	strbuf_init(&csp, PATH_MAX);

	while ((commit = get_revision(ctx->revs)) != NULL) {
		/*
		 * an uninteresting boundary commit may not have its tree
		 * parsed yet, but we are not going to show them anyway
		 */
		if (!ctx->revs->tree_objects)
			; /* do not bother loading tree */
		else if (get_commit_tree(commit)) {
			struct tree *tree = get_commit_tree(commit);
			tree->object.flags |= NOT_USER_GIVEN;
			add_pending_tree(ctx->revs, tree);
		} else if (commit->object.parsed) {
			die(_("unable to load root tree for commit %s"),
			      oid_to_hex(&commit->object.oid));
		}
		ctx->show_commit(commit, ctx->show_data);

		if (ctx->revs->tree_blobs_in_commit_order)
			/*
			 * NEEDSWORK: Adding the tree and then flushing it here
			 * needs a reallocation for each commit. Can we pass the
			 * tree directory without allocation churn?
			 */
			traverse_trees_and_blobs(ctx, &csp);
	}
	traverse_trees_and_blobs(ctx, &csp);
	strbuf_release(&csp);
}

void traverse_commit_list(struct rev_info *revs,
			  show_commit_fn show_commit,
			  show_object_fn show_object,
			  void *show_data)
{
	struct traversal_context ctx;
	ctx.revs = revs;
	ctx.show_commit = show_commit;
	ctx.show_object = show_object;
	ctx.show_data = show_data;
	ctx.filter = NULL;
	do_traverse(&ctx);
}

void traverse_commit_list_filtered(
	struct list_objects_filter_options *filter_options,
	struct rev_info *revs,
	show_commit_fn show_commit,
	show_object_fn show_object,
	void *show_data,
	struct oidset *omitted)
{
	struct traversal_context ctx;

	ctx.revs = revs;
	ctx.show_object = show_object;
	ctx.show_commit = show_commit;
	ctx.show_data = show_data;
	ctx.filter = list_objects_filter__init(omitted, filter_options);
	do_traverse(&ctx);
	list_objects_filter__free(ctx.filter);
}
