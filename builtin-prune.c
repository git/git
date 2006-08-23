#include "cache.h"
#include "refs.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "tree-walk.h"
#include "diff.h"
#include "revision.h"
#include "builtin.h"
#include "cache-tree.h"

static const char prune_usage[] = "git-prune [-n]";
static int show_only;
static struct rev_info revs;

static int prune_object(char *path, const char *filename, const unsigned char *sha1)
{
	if (show_only) {
		printf("would prune %s/%s\n", path, filename);
		return 0;
	}
	unlink(mkpath("%s/%s", path, filename));
	rmdir(path);
	return 0;
}

static int prune_dir(int i, char *path)
{
	DIR *dir = opendir(path);
	struct dirent *de;

	if (!dir)
		return 0;

	while ((de = readdir(dir)) != NULL) {
		char name[100];
		unsigned char sha1[20];
		int len = strlen(de->d_name);

		switch (len) {
		case 2:
			if (de->d_name[1] != '.')
				break;
		case 1:
			if (de->d_name[0] != '.')
				break;
			continue;
		case 38:
			sprintf(name, "%02x", i);
			memcpy(name+2, de->d_name, len+1);
			if (get_sha1_hex(name, sha1) < 0)
				break;

			/*
			 * Do we know about this object?
			 * It must have been reachable
			 */
			if (lookup_object(sha1))
				continue;

			prune_object(path, de->d_name, sha1);
			continue;
		}
		fprintf(stderr, "bad sha1 file: %s/%s\n", path, de->d_name);
	}
	closedir(dir);
	return 0;
}

static void prune_object_dir(const char *path)
{
	int i;
	for (i = 0; i < 256; i++) {
		static char dir[4096];
		sprintf(dir, "%s/%02x", path, i);
		prune_dir(i, dir);
	}
}

static void process_blob(struct blob *blob,
			 struct object_array *p,
			 struct name_path *path,
			 const char *name)
{
	struct object *obj = &blob->object;

	if (obj->flags & SEEN)
		return;
	obj->flags |= SEEN;
	/* Nothing to do, really .. The blob lookup was the important part */
}

static void process_tree(struct tree *tree,
			 struct object_array *p,
			 struct name_path *path,
			 const char *name)
{
	struct object *obj = &tree->object;
	struct tree_desc desc;
	struct name_entry entry;
	struct name_path me;

	if (obj->flags & SEEN)
		return;
	obj->flags |= SEEN;
	if (parse_tree(tree) < 0)
		die("bad tree object %s", sha1_to_hex(obj->sha1));
	name = strdup(name);
	add_object(obj, p, path, name);
	me.up = path;
	me.elem = name;
	me.elem_len = strlen(name);

	desc.buf = tree->buffer;
	desc.size = tree->size;

	while (tree_entry(&desc, &entry)) {
		if (S_ISDIR(entry.mode))
			process_tree(lookup_tree(entry.sha1), p, &me, entry.path);
		else
			process_blob(lookup_blob(entry.sha1), p, &me, entry.path);
	}
	free(tree->buffer);
	tree->buffer = NULL;
}

static void process_tag(struct tag *tag, struct object_array *p, const char *name)
{
	struct object *obj = &tag->object;
	struct name_path me;

	if (obj->flags & SEEN)
		return;
	obj->flags |= SEEN;

	me.up = NULL;
	me.elem = "tag:/";
	me.elem_len = 5;

	if (parse_tag(tag) < 0)
		die("bad tag object %s", sha1_to_hex(obj->sha1));
	add_object(tag->tagged, p, NULL, name);
}

static void walk_commit_list(struct rev_info *revs)
{
	int i;
	struct commit *commit;
	struct object_array objects = { 0, 0, NULL };

	/* Walk all commits, process their trees */
	while ((commit = get_revision(revs)) != NULL)
		process_tree(commit->tree, &objects, NULL, "");

	/* Then walk all the pending objects, recursively processing them too */
	for (i = 0; i < revs->pending.nr; i++) {
		struct object_array_entry *pending = revs->pending.objects + i;
		struct object *obj = pending->item;
		const char *name = pending->name;
		if (obj->type == OBJ_TAG) {
			process_tag((struct tag *) obj, &objects, name);
			continue;
		}
		if (obj->type == OBJ_TREE) {
			process_tree((struct tree *)obj, &objects, NULL, name);
			continue;
		}
		if (obj->type == OBJ_BLOB) {
			process_blob((struct blob *)obj, &objects, NULL, name);
			continue;
		}
		die("unknown pending object %s (%s)", sha1_to_hex(obj->sha1), name);
	}
}

static int add_one_ref(const char *path, const unsigned char *sha1)
{
	struct object *object = parse_object(sha1);
	if (!object)
		die("bad object ref: %s:%s", path, sha1_to_hex(sha1));
	add_pending_object(&revs, object, "");
	return 0;
}

static void add_one_tree(const unsigned char *sha1)
{
	struct tree *tree = lookup_tree(sha1);
	add_pending_object(&revs, &tree->object, "");
}

static void add_cache_tree(struct cache_tree *it)
{
	int i;

	if (it->entry_count >= 0)
		add_one_tree(it->sha1);
	for (i = 0; i < it->subtree_nr; i++)
		add_cache_tree(it->down[i]->cache_tree);
}

static void add_cache_refs(void)
{
	int i;

	read_cache();
	for (i = 0; i < active_nr; i++) {
		lookup_blob(active_cache[i]->sha1);
		/*
		 * We could add the blobs to the pending list, but quite
		 * frankly, we don't care. Once we've looked them up, and
		 * added them as objects, we've really done everything
		 * there is to do for a blob
		 */
	}
	if (active_cache_tree)
		add_cache_tree(active_cache_tree);
}

int cmd_prune(int argc, const char **argv, const char *prefix)
{
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "-n")) {
			show_only = 1;
			continue;
		}
		usage(prune_usage);
	}

	/*
	 * Set up revision parsing, and mark us as being interested
	 * in all object types, not just commits.
	 */
	init_revisions(&revs, prefix);
	revs.tag_objects = 1;
	revs.blob_objects = 1;
	revs.tree_objects = 1;

	/* Add all external refs */
	for_each_ref(add_one_ref);

	/* Add all refs from the index file */
	add_cache_refs();

	/*
	 * Set up the revision walk - this will move all commits
	 * from the pending list to the commit walking list.
	 */
	prepare_revision_walk(&revs);

	walk_commit_list(&revs);

	prune_object_dir(get_object_directory());

	return 0;
}
