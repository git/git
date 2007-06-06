#include "cache.h"
#include "tree.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree-walk.h"

const char *tree_type = "tree";

static int read_one_entry(const unsigned char *sha1, const char *base, int baselen, const char *pathname, unsigned mode, int stage)
{
	int len;
	unsigned int size;
	struct cache_entry *ce;

	if (S_ISDIR(mode))
		return READ_TREE_RECURSIVE;

	len = strlen(pathname);
	size = cache_entry_size(baselen + len);
	ce = xcalloc(1, size);

	ce->ce_mode = create_ce_mode(mode);
	ce->ce_flags = create_ce_flags(baselen + len, stage);
	memcpy(ce->name, base, baselen);
	memcpy(ce->name + baselen, pathname, len+1);
	hashcpy(ce->sha1, sha1);
	return add_cache_entry(ce, ADD_CACHE_OK_TO_ADD|ADD_CACHE_SKIP_DFCHECK);
}

static int match_tree_entry(const char *base, int baselen, const char *path, unsigned int mode, const char **paths)
{
	const char *match;
	int pathlen;

	if (!paths)
		return 1;
	pathlen = strlen(path);
	while ((match = *paths++) != NULL) {
		int matchlen = strlen(match);

		if (baselen >= matchlen) {
			/* If it doesn't match, move along... */
			if (strncmp(base, match, matchlen))
				continue;
			/* The base is a subdirectory of a path which was specified. */
			return 1;
		}

		/* Does the base match? */
		if (strncmp(base, match, baselen))
			continue;

		match += baselen;
		matchlen -= baselen;

		if (pathlen > matchlen)
			continue;

		if (matchlen > pathlen) {
			if (match[pathlen] != '/')
				continue;
			if (!S_ISDIR(mode))
				continue;
		}

		if (strncmp(path, match, pathlen))
			continue;

		return 1;
	}
	return 0;
}

int read_tree_recursive(struct tree *tree,
			const char *base, int baselen,
			int stage, const char **match,
			read_tree_fn_t fn)
{
	struct tree_desc desc;
	struct name_entry entry;

	if (parse_tree(tree))
		return -1;

	init_tree_desc(&desc, tree->buffer, tree->size);

	while (tree_entry(&desc, &entry)) {
		if (!match_tree_entry(base, baselen, entry.path, entry.mode, match))
			continue;

		switch (fn(entry.sha1, base, baselen, entry.path, entry.mode, stage)) {
		case 0:
			continue;
		case READ_TREE_RECURSIVE:
			break;;
		default:
			return -1;
		}
		if (S_ISDIR(entry.mode)) {
			int retval;
			char *newbase;
			unsigned int pathlen = tree_entry_len(entry.path, entry.sha1);

			newbase = xmalloc(baselen + 1 + pathlen);
			memcpy(newbase, base, baselen);
			memcpy(newbase + baselen, entry.path, pathlen);
			newbase[baselen + pathlen] = '/';
			retval = read_tree_recursive(lookup_tree(entry.sha1),
						     newbase,
						     baselen + pathlen + 1,
						     stage, match, fn);
			free(newbase);
			if (retval)
				return -1;
			continue;
		}
	}
	return 0;
}

int read_tree(struct tree *tree, int stage, const char **match)
{
	return read_tree_recursive(tree, "", 0, stage, match, read_one_entry);
}

struct tree *lookup_tree(const unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj)
		return create_object(sha1, OBJ_TREE, alloc_tree_node());
	if (!obj->type)
		obj->type = OBJ_TREE;
	if (obj->type != OBJ_TREE) {
		error("Object %s is a %s, not a tree",
		      sha1_to_hex(sha1), typename(obj->type));
		return NULL;
	}
	return (struct tree *) obj;
}

/*
 * NOTE! Tree refs to external git repositories
 * (ie gitlinks) do not count as real references.
 *
 * You don't have to have those repositories
 * available at all, much less have the objects
 * accessible from the current repository.
 */
static void track_tree_refs(struct tree *item)
{
	int n_refs = 0, i;
	struct object_refs *refs;
	struct tree_desc desc;
	struct name_entry entry;

	/* Count how many entries there are.. */
	init_tree_desc(&desc, item->buffer, item->size);
	while (tree_entry(&desc, &entry)) {
		if (S_ISGITLINK(entry.mode))
			continue;
		n_refs++;
	}

	/* Allocate object refs and walk it again.. */
	i = 0;
	refs = alloc_object_refs(n_refs);
	init_tree_desc(&desc, item->buffer, item->size);
	while (tree_entry(&desc, &entry)) {
		struct object *obj;

		if (S_ISGITLINK(entry.mode))
			continue;
		if (S_ISDIR(entry.mode))
			obj = &lookup_tree(entry.sha1)->object;
		else if (S_ISREG(entry.mode) || S_ISLNK(entry.mode))
			obj = &lookup_blob(entry.sha1)->object;
		else {
			warning("in tree %s: entry %s has bad mode %.6o\n",
			     sha1_to_hex(item->object.sha1), entry.path, entry.mode);
			obj = lookup_unknown_object(entry.sha1);
		}
		refs->ref[i++] = obj;
	}
	set_object_refs(&item->object, refs);
}

int parse_tree_buffer(struct tree *item, void *buffer, unsigned long size)
{
	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	item->buffer = buffer;
	item->size = size;

	if (track_object_refs)
		track_tree_refs(item);
	return 0;
}

int parse_tree(struct tree *item)
{
	 enum object_type type;
	 void *buffer;
	 unsigned long size;

	if (item->object.parsed)
		return 0;
	buffer = read_sha1_file(item->object.sha1, &type, &size);
	if (!buffer)
		return error("Could not read %s",
			     sha1_to_hex(item->object.sha1));
	if (type != OBJ_TREE) {
		free(buffer);
		return error("Object %s not a tree",
			     sha1_to_hex(item->object.sha1));
	}
	return parse_tree_buffer(item, buffer, size);
}

struct tree *parse_tree_indirect(const unsigned char *sha1)
{
	struct object *obj = parse_object(sha1);
	do {
		if (!obj)
			return NULL;
		if (obj->type == OBJ_TREE)
			return (struct tree *) obj;
		else if (obj->type == OBJ_COMMIT)
			obj = &(((struct commit *) obj)->tree->object);
		else if (obj->type == OBJ_TAG)
			obj = ((struct tag *) obj)->tagged;
		else
			return NULL;
		if (!obj->parsed)
			parse_object(obj->sha1);
	} while (1);
}
