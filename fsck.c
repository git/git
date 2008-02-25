#include "cache.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "tree-walk.h"
#include "commit.h"
#include "tag.h"
#include "fsck.h"

static int fsck_walk_tree(struct tree *tree, fsck_walk_func walk, void *data)
{
	struct tree_desc desc;
	struct name_entry entry;
	int res = 0;

	if (parse_tree(tree))
		return -1;

	init_tree_desc(&desc, tree->buffer, tree->size);
	while (tree_entry(&desc, &entry)) {
		int result;

		if (S_ISGITLINK(entry.mode))
			continue;
		if (S_ISDIR(entry.mode))
			result = walk(&lookup_tree(entry.sha1)->object, OBJ_TREE, data);
		else if (S_ISREG(entry.mode) || S_ISLNK(entry.mode))
			result = walk(&lookup_blob(entry.sha1)->object, OBJ_BLOB, data);
		else {
			result = error("in tree %s: entry %s has bad mode %.6o\n",
					sha1_to_hex(tree->object.sha1), entry.path, entry.mode);
		}
		if (result < 0)
			return result;
		if (!res)
			res = result;
	}
	return res;
}

static int fsck_walk_commit(struct commit *commit, fsck_walk_func walk, void *data)
{
	struct commit_list *parents;
	int res;
	int result;

	if (parse_commit(commit))
		return -1;

	result = walk((struct object *)commit->tree, OBJ_TREE, data);
	if (result < 0)
		return result;
	res = result;

	parents = commit->parents;
	while (parents) {
		result = walk((struct object *)parents->item, OBJ_COMMIT, data);
		if (result < 0)
			return result;
		if (!res)
			res = result;
		parents = parents->next;
	}
	return res;
}

static int fsck_walk_tag(struct tag *tag, fsck_walk_func walk, void *data)
{
	if (parse_tag(tag))
		return -1;
	return walk(tag->tagged, OBJ_ANY, data);
}

int fsck_walk(struct object *obj, fsck_walk_func walk, void *data)
{
	if (!obj)
		return -1;
	switch (obj->type) {
	case OBJ_BLOB:
		return 0;
	case OBJ_TREE:
		return fsck_walk_tree((struct tree *)obj, walk, data);
	case OBJ_COMMIT:
		return fsck_walk_commit((struct commit *)obj, walk, data);
	case OBJ_TAG:
		return fsck_walk_tag((struct tag *)obj, walk, data);
	default:
		error("Unknown object type for %s", sha1_to_hex(obj->sha1));
		return -1;
	}
}
