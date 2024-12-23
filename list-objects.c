#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "tag.h"
#include "commit.h"
#include "gettext.h"
#include "hex.h"
#include "tree.h"
#include "blob.h"
#include "diff.h"
#include "tree-walk.h"
#include "revision.h"
#include "list-objects.h"
#include "list-objects-filter.h"
#include "list-objects-filter-options.h"
#include "packfile.h"
#include "object-store-ll.h"
#include "trace.h"
#include "environment.h"

struct traversal_context {
	struct rev_info *revs;
	show_object_fn show_object;
	show_commit_fn show_commit;
	void *show_data;
	struct filter *filter;
	int depth;
};

static void show_commit(struct traversal_context *ctx,
			struct commit *commit)
{
	if (!ctx->show_commit)
		return;
	ctx->show_commit(commit, ctx->show_data);
}

static void show_object(struct traversal_context *ctx,
			struct object *object,
			const char *name)
{
	if (!ctx->show_object)
		return;
	if (ctx->revs->unpacked && has_object_pack(ctx->revs->repo,
						   &object->oid))
		return;

	ctx->show_object(object, name, ctx->show_data);
}

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
	    !repo_has_object_file(the_repository, &obj->oid) &&
	    is_promisor_object(ctx->revs->repo, &obj->oid))
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
		show_object(ctx, obj, path->buf);
	strbuf_setlen(path, pathlen);
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

	init_tree_desc(&desc, &tree->object.oid, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		if (match != all_entries_interesting) {
			match = tree_entry_interesting(ctx->revs->repo->index,
						       &entry, base,
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
			ctx->depth++;
			process_tree(ctx, t, base, entry.path);
			ctx->depth--;
		}
		else if (S_ISGITLINK(entry.mode))
			; /* ignore gitlink */
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
	if (revs->include_check_obj &&
	    !revs->include_check_obj(&tree->object, revs->include_check_data))
		return;

	if (ctx->depth > max_allowed_tree_depth)
		die("exceeded maximum allowed tree depth");

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
		    is_promisor_object(revs->repo, &obj->oid))
			return;

		if (!revs->do_not_die_on_missing_objects)
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
		show_object(ctx, obj, base->buf);
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
		show_object(ctx, obj, base->buf);

	strbuf_setlen(base, baselen);
	free_tree_buffer(tree);
}

static void process_tag(struct traversal_context *ctx,
			struct tag *tag,
			const char *name)
{
	enum list_objects_filter_result r;

	r = list_objects_filter__filter_object(ctx->revs->repo, LOFS_TAG,
					       &tag->object, NULL, NULL,
					       ctx->filter);
	if (r & LOFR_MARK_SEEN)
		tag->object.flags |= SEEN;
	if (r & LOFR_DO_SHOW)
		show_object(ctx, &tag->object, name);
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
		mark_tree_uninteresting(revs->repo,
					repo_get_commit_tree(the_repository, parent));
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
		struct tree *tree = repo_get_commit_tree(the_repository,
							 parent);

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

	if (sparse) {
		struct oidset set;
		oidset_init(&set, 16);

		for (list = revs->commits; list; list = list->next) {
			struct commit *commit = list->item;
			struct tree *tree = repo_get_commit_tree(the_repository,
								 commit);

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
							repo_get_commit_tree(the_repository, commit));
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
		for (size_t i = 0; i < revs->cmdline.nr; i++) {
			struct object *obj = revs->cmdline.rev[i].item;
			struct commit *commit = (struct commit *)obj;
			if (obj->type != OBJ_COMMIT || !(obj->flags & UNINTERESTING))
				continue;
			mark_tree_uninteresting(revs->repo,
						repo_get_commit_tree(the_repository, commit));
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

static void traverse_non_commits(struct traversal_context *ctx,
				 struct strbuf *base)
{
	assert(base->len == 0);

	for (size_t i = 0; i < ctx->revs->pending.nr; i++) {
		struct object_array_entry *pending = ctx->revs->pending.objects + i;
		struct object *obj = pending->item;
		const char *name = pending->name;
		const char *path = pending->path;
		if (obj->flags & (UNINTERESTING | SEEN))
			continue;
		if (obj->type == OBJ_TAG) {
			process_tag(ctx, (struct tag *)obj, name);
			continue;
		}
		if (!path)
			path = "";
		if (obj->type == OBJ_TREE) {
			ctx->depth = 0;
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
		enum list_objects_filter_result r;

		r = list_objects_filter__filter_object(ctx->revs->repo,
				LOFS_COMMIT, &commit->object,
				NULL, NULL, ctx->filter);

		/*
		 * an uninteresting boundary commit may not have its tree
		 * parsed yet, but we are not going to show them anyway
		 */
		if (!ctx->revs->tree_objects)
			; /* do not bother loading tree */
		else if (ctx->revs->do_not_die_on_missing_objects &&
			 oidset_contains(&ctx->revs->missing_commits, &commit->object.oid))
			;
		else if (repo_get_commit_tree(the_repository, commit)) {
			struct tree *tree = repo_get_commit_tree(the_repository,
								 commit);
			tree->object.flags |= NOT_USER_GIVEN;
			add_pending_tree(ctx->revs, tree);
		} else if (commit->object.parsed) {
			die(_("unable to load root tree for commit %s"),
			      oid_to_hex(&commit->object.oid));
		}

		if (r & LOFR_MARK_SEEN)
			commit->object.flags |= SEEN;
		if (r & LOFR_DO_SHOW)
			show_commit(ctx, commit);

		if (ctx->revs->tree_blobs_in_commit_order)
			/*
			 * NEEDSWORK: Adding the tree and then flushing it here
			 * needs a reallocation for each commit. Can we pass the
			 * tree directory without allocation churn?
			 */
			traverse_non_commits(ctx, &csp);
	}
	traverse_non_commits(ctx, &csp);
	strbuf_release(&csp);
}

void traverse_commit_list_filtered(
	struct rev_info *revs,
	show_commit_fn show_commit,
	show_object_fn show_object,
	void *show_data,
	struct oidset *omitted)
{
	struct traversal_context ctx = {
		.revs = revs,
		.show_object = show_object,
		.show_commit = show_commit,
		.show_data = show_data,
	};

	if (revs->filter.choice)
		ctx.filter = list_objects_filter__init(omitted, &revs->filter);

	do_traverse(&ctx);

	if (ctx.filter)
		list_objects_filter__free(ctx.filter);
}
