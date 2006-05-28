#include "cache.h"
#include "tree.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree-walk.h"
#include <stdlib.h>

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
	memcpy(ce->sha1, sha1, 20);
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
	struct tree_entry_list *list;
	if (parse_tree(tree))
		return -1;
	list = tree->entries;
	while (list) {
		struct tree_entry_list *current = list;
		list = list->next;
		if (!match_tree_entry(base, baselen, current->name,
				      current->mode, match))
			continue;

		switch (fn(current->sha1, base, baselen,
			   current->name, current->mode, stage)) {
		case 0:
			continue;
		case READ_TREE_RECURSIVE:
			break;;
		default:
			return -1;
		}
		if (current->directory) {
			int retval;
			int pathlen = strlen(current->name);
			char *newbase;

			newbase = xmalloc(baselen + 1 + pathlen);
			memcpy(newbase, base, baselen);
			memcpy(newbase + baselen, current->name, pathlen);
			newbase[baselen + pathlen] = '/';
			retval = read_tree_recursive(lookup_tree(current->sha1),
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
	if (!obj) {
		struct tree *ret = xcalloc(1, sizeof(struct tree));
		created_object(sha1, &ret->object);
		ret->object.type = tree_type;
		return ret;
	}
	if (!obj->type)
		obj->type = tree_type;
	if (obj->type != tree_type) {
		error("Object %s is a %s, not a tree", 
		      sha1_to_hex(sha1), obj->type);
		return NULL;
	}
	return (struct tree *) obj;
}

int parse_tree_buffer(struct tree *item, void *buffer, unsigned long size)
{
	struct tree_desc desc;
	struct tree_entry_list **list_p;
	int n_refs = 0;

	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	item->buffer = buffer;
	item->size = size;

	desc.buf = buffer;
	desc.size = size;

	list_p = &item->entries;
	while (desc.size) {
		unsigned mode;
		const char *path;
		const unsigned char *sha1;
		struct tree_entry_list *entry;

		sha1 = tree_entry_extract(&desc, &path, &mode);

		entry = xmalloc(sizeof(struct tree_entry_list));
		entry->name = path;
		entry->sha1 = sha1;
		entry->mode = mode;
		entry->directory = S_ISDIR(mode) != 0;
		entry->executable = (mode & S_IXUSR) != 0;
		entry->symlink = S_ISLNK(mode) != 0;
		entry->zeropad = *(const char *)(desc.buf) == '0';
		entry->next = NULL;

		update_tree_entry(&desc);
		n_refs++;
		*list_p = entry;
		list_p = &entry->next;
	}

	if (track_object_refs) {
		struct tree_entry_list *entry;
		unsigned i = 0;
		struct object_refs *refs = alloc_object_refs(n_refs);
		for (entry = item->entries; entry; entry = entry->next) {
			struct object *obj;

			if (entry->directory)
				obj = &lookup_tree(entry->sha1)->object;
			else
				obj = &lookup_blob(entry->sha1)->object;
			refs->ref[i++] = obj;
		}

		set_object_refs(&item->object, refs);
	}

	return 0;
}

int parse_tree(struct tree *item)
{
	 char type[20];
	 void *buffer;
	 unsigned long size;

	if (item->object.parsed)
		return 0;
	buffer = read_sha1_file(item->object.sha1, type, &size);
	if (!buffer)
		return error("Could not read %s",
			     sha1_to_hex(item->object.sha1));
	if (strcmp(type, tree_type)) {
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
		if (obj->type == tree_type)
			return (struct tree *) obj;
		else if (obj->type == commit_type)
			obj = &(((struct commit *) obj)->tree->object);
		else if (obj->type == tag_type)
			obj = ((struct tag *) obj)->tagged;
		else
			return NULL;
		if (!obj->parsed)
			parse_object(obj->sha1);
	} while (1);
}
