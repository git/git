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

	desc.buf = tree->buffer;
	desc.size = tree->size;

	while (tree_entry(&desc, &entry)) {
		if (S_ISDIR(entry.mode))
			process_tree(revs,
				     lookup_tree(entry.sha1),
				     p, &me, entry.path);
		else
			process_blob(revs,
				     lookup_blob(entry.sha1),
				     p, &me, entry.path);
	}
	free(tree->buffer);
	tree->buffer = NULL;
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
