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

static void process_blob(struct rev_info *revs,
			 struct blob *blob,
			 show_object_fn show,
			 struct strbuf *path,
			 const char *name,
			 void *cb_data,
			 filter_object_fn filter_fn,
			 void *filter_data)
{
	struct object *obj = &blob->object;
	size_t pathlen;
	enum list_objects_filter_result r = LOFR_MARK_SEEN | LOFR_DO_SHOW;

	if (!revs->blob_objects)
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
	if (revs->exclude_promisor_objects &&
	    !has_object_file(&obj->oid) &&
	    is_promisor_object(&obj->oid))
		return;

	pathlen = path->len;
	strbuf_addstr(path, name);
	if (filter_fn)
		r = filter_fn(LOFS_BLOB, obj,
			      path->buf, &path->buf[pathlen],
			      filter_data);
	if (r & LOFR_MARK_SEEN)
		obj->flags |= SEEN;
	if (r & LOFR_DO_SHOW)
		show(obj, path->buf, cb_data);
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
static void process_gitlink(struct rev_info *revs,
			    const unsigned char *sha1,
			    show_object_fn show,
			    struct strbuf *path,
			    const char *name,
			    void *cb_data)
{
	/* Nothing to do */
}

static void process_tree(struct rev_info *revs,
			 struct tree *tree,
			 show_object_fn show,
			 struct strbuf *base,
			 const char *name,
			 void *cb_data,
			 filter_object_fn filter_fn,
			 void *filter_data)
{
	struct object *obj = &tree->object;
	struct tree_desc desc;
	struct name_entry entry;
	enum interesting match = revs->diffopt.pathspec.nr == 0 ?
		all_entries_interesting: entry_not_interesting;
	int baselen = base->len;
	enum list_objects_filter_result r = LOFR_MARK_SEEN | LOFR_DO_SHOW;
	int gently = revs->ignore_missing_links ||
		     revs->exclude_promisor_objects;

	if (!revs->tree_objects)
		return;
	if (!obj)
		die("bad tree object");
	if (obj->flags & (UNINTERESTING | SEEN))
		return;
	if (parse_tree_gently(tree, gently) < 0) {
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

		die("bad tree object %s", oid_to_hex(&obj->oid));
	}

	strbuf_addstr(base, name);
	if (filter_fn)
		r = filter_fn(LOFS_BEGIN_TREE, obj,
			      base->buf, &base->buf[baselen],
			      filter_data);
	if (r & LOFR_MARK_SEEN)
		obj->flags |= SEEN;
	if (r & LOFR_DO_SHOW)
		show(obj, base->buf, cb_data);
	if (base->len)
		strbuf_addch(base, '/');

	init_tree_desc(&desc, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		if (match != all_entries_interesting) {
			match = tree_entry_interesting(&entry, base, 0,
						       &revs->diffopt.pathspec);
			if (match == all_entries_not_interesting)
				break;
			if (match == entry_not_interesting)
				continue;
		}

		if (S_ISDIR(entry.mode))
			process_tree(revs,
				     lookup_tree(entry.oid),
				     show, base, entry.path,
				     cb_data, filter_fn, filter_data);
		else if (S_ISGITLINK(entry.mode))
			process_gitlink(revs, entry.oid->hash,
					show, base, entry.path,
					cb_data);
		else
			process_blob(revs,
				     lookup_blob(entry.oid),
				     show, base, entry.path,
				     cb_data, filter_fn, filter_data);
	}

	if (filter_fn) {
		r = filter_fn(LOFS_END_TREE, obj,
			      base->buf, &base->buf[baselen],
			      filter_data);
		if (r & LOFR_MARK_SEEN)
			obj->flags |= SEEN;
		if (r & LOFR_DO_SHOW)
			show(obj, base->buf, cb_data);
	}

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
		mark_tree_uninteresting(get_commit_tree(parent));
		if (revs->edge_hint && !(parent->object.flags & SHOWN)) {
			parent->object.flags |= SHOWN;
			show_edge(parent);
		}
	}
}

void mark_edges_uninteresting(struct rev_info *revs, show_edge_fn show_edge)
{
	struct commit_list *list;
	int i;

	for (list = revs->commits; list; list = list->next) {
		struct commit *commit = list->item;

		if (commit->object.flags & UNINTERESTING) {
			mark_tree_uninteresting(get_commit_tree(commit));
			if (revs->edge_hint_aggressive && !(commit->object.flags & SHOWN)) {
				commit->object.flags |= SHOWN;
				show_edge(commit);
			}
			continue;
		}
		mark_edge_parents_uninteresting(commit, revs, show_edge);
	}
	if (revs->edge_hint_aggressive) {
		for (i = 0; i < revs->cmdline.nr; i++) {
			struct object *obj = revs->cmdline.rev[i].item;
			struct commit *commit = (struct commit *)obj;
			if (obj->type != OBJ_COMMIT || !(obj->flags & UNINTERESTING))
				continue;
			mark_tree_uninteresting(get_commit_tree(commit));
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

static void traverse_trees_and_blobs(struct rev_info *revs,
				     struct strbuf *base,
				     show_object_fn show_object,
				     void *show_data,
				     filter_object_fn filter_fn,
				     void *filter_data)
{
	int i;

	assert(base->len == 0);

	for (i = 0; i < revs->pending.nr; i++) {
		struct object_array_entry *pending = revs->pending.objects + i;
		struct object *obj = pending->item;
		const char *name = pending->name;
		const char *path = pending->path;
		if (obj->flags & (UNINTERESTING | SEEN))
			continue;
		if (obj->type == OBJ_TAG) {
			obj->flags |= SEEN;
			show_object(obj, name, show_data);
			continue;
		}
		if (!path)
			path = "";
		if (obj->type == OBJ_TREE) {
			process_tree(revs, (struct tree *)obj, show_object,
				     base, path, show_data,
				     filter_fn, filter_data);
			continue;
		}
		if (obj->type == OBJ_BLOB) {
			process_blob(revs, (struct blob *)obj, show_object,
				     base, path, show_data,
				     filter_fn, filter_data);
			continue;
		}
		die("unknown pending object %s (%s)",
		    oid_to_hex(&obj->oid), name);
	}
	object_array_clear(&revs->pending);
}

static void do_traverse(struct rev_info *revs,
			show_commit_fn show_commit,
			show_object_fn show_object,
			void *show_data,
			filter_object_fn filter_fn,
			void *filter_data)
{
	struct commit *commit;
	struct strbuf csp; /* callee's scratch pad */
	strbuf_init(&csp, PATH_MAX);

	while ((commit = get_revision(revs)) != NULL) {
		/*
		 * an uninteresting boundary commit may not have its tree
		 * parsed yet, but we are not going to show them anyway
		 */
		if (get_commit_tree(commit))
			add_pending_tree(revs, get_commit_tree(commit));
		show_commit(commit, show_data);

		if (revs->tree_blobs_in_commit_order)
			/*
			 * NEEDSWORK: Adding the tree and then flushing it here
			 * needs a reallocation for each commit. Can we pass the
			 * tree directory without allocation churn?
			 */
			traverse_trees_and_blobs(revs, &csp,
						 show_object, show_data,
						 filter_fn, filter_data);
	}
	traverse_trees_and_blobs(revs, &csp,
				 show_object, show_data,
				 filter_fn, filter_data);
	strbuf_release(&csp);
}

void traverse_commit_list(struct rev_info *revs,
			  show_commit_fn show_commit,
			  show_object_fn show_object,
			  void *show_data)
{
	do_traverse(revs, show_commit, show_object, show_data, NULL, NULL);
}

void traverse_commit_list_filtered(
	struct list_objects_filter_options *filter_options,
	struct rev_info *revs,
	show_commit_fn show_commit,
	show_object_fn show_object,
	void *show_data,
	struct oidset *omitted)
{
	filter_object_fn filter_fn = NULL;
	filter_free_fn filter_free_fn = NULL;
	void *filter_data = NULL;

	filter_data = list_objects_filter__init(omitted, filter_options,
						&filter_fn, &filter_free_fn);
	do_traverse(revs, show_commit, show_object, show_data,
		    filter_fn, filter_data);
	if (filter_data && filter_free_fn)
		filter_free_fn(filter_data);
}
