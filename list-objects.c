#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "diff.h"
#include "tree-walk.h"
#include "revision.h"
#include "list-objects.h"

static void process_blob(struct rev_info *revs,
			 struct blob *blob,
			 struct object_array *p,
			 struct name_path *path,
			 const char *name)
{
	struct object *obj = &blob->object;

	if (!revs->blob_objects)
		return;
	if (obj->flags & (UNINTERESTING | SEEN))
		return;
	obj->flags |= SEEN;
	name = xstrdup(name);
	add_object(obj, p, path, name);
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
			    struct object_array *p,
			    struct name_path *path,
			    const char *name)
{
	/* Nothing to do */
}

static void process_tree(struct rev_info *revs,
			 struct tree *tree,
			 struct object_array *p,
			 struct name_path *path,
			 const char *name)
{
	struct object *obj = &tree->object;
	struct tree_desc desc;
	struct name_entry entry;
	struct name_path me;

	if (!revs->tree_objects)
		return;
	if (obj->flags & (UNINTERESTING | SEEN))
		return;
	if (parse_tree(tree) < 0)
		die("bad tree object %s", sha1_to_hex(obj->sha1));
	obj->flags |= SEEN;
	name = xstrdup(name);
	add_object(obj, p, path, name);
	me.up = path;
	me.elem = name;
	me.elem_len = strlen(name);

	init_tree_desc(&desc, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		if (S_ISDIR(entry.mode))
			process_tree(revs,
				     lookup_tree(entry.sha1),
				     p, &me, entry.path);
		else if (S_ISDIRLNK(entry.mode))
			process_gitlink(revs, entry.sha1,
					p, &me, entry.path);
		else
			process_blob(revs,
				     lookup_blob(entry.sha1),
				     p, &me, entry.path);
	}
	free(tree->buffer);
	tree->buffer = NULL;
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
		mark_tree_uninteresting(parent->tree);
		if (revs->edge_hint && !(parent->object.flags & SHOWN)) {
			parent->object.flags |= SHOWN;
			show_edge(parent);
		}
	}
}

void mark_edges_uninteresting(struct commit_list *list,
			      struct rev_info *revs,
			      show_edge_fn show_edge)
{
	for ( ; list; list = list->next) {
		struct commit *commit = list->item;

		if (commit->object.flags & UNINTERESTING) {
			mark_tree_uninteresting(commit->tree);
			continue;
		}
		mark_edge_parents_uninteresting(commit, revs, show_edge);
	}
}

void traverse_commit_list(struct rev_info *revs,
			  void (*show_commit)(struct commit *),
			  void (*show_object)(struct object_array_entry *))
{
	int i;
	struct commit *commit;
	struct object_array objects = { 0, 0, NULL };

	while ((commit = get_revision(revs)) != NULL) {
		process_tree(revs, commit->tree, &objects, NULL, "");
		show_commit(commit);
	}
	for (i = 0; i < revs->pending.nr; i++) {
		struct object_array_entry *pending = revs->pending.objects + i;
		struct object *obj = pending->item;
		const char *name = pending->name;
		if (obj->flags & (UNINTERESTING | SEEN))
			continue;
		if (obj->type == OBJ_TAG) {
			obj->flags |= SEEN;
			add_object_array(obj, name, &objects);
			continue;
		}
		if (obj->type == OBJ_TREE) {
			process_tree(revs, (struct tree *)obj, &objects,
				     NULL, name);
			continue;
		}
		if (obj->type == OBJ_BLOB) {
			process_blob(revs, (struct blob *)obj, &objects,
				     NULL, name);
			continue;
		}
		die("unknown pending object %s (%s)",
		    sha1_to_hex(obj->sha1), name);
	}
	for (i = 0; i < objects.nr; i++)
		show_object(&objects.objects[i]);
}
