#include "tree.h"
#include "blob.h"
#include "cache.h"
#include <stdlib.h>

const char *tree_type = "tree";

static int read_one_entry(unsigned char *sha1, const char *base, int baselen, const char *pathname, unsigned mode, int stage)
{
	int len = strlen(pathname);
	unsigned int size = cache_entry_size(baselen + len);
	struct cache_entry *ce = malloc(size);

	memset(ce, 0, size);

	ce->ce_mode = create_ce_mode(mode);
	ce->ce_flags = create_ce_flags(baselen + len, stage);
	memcpy(ce->name, base, baselen);
	memcpy(ce->name + baselen, pathname, len+1);
	memcpy(ce->sha1, sha1, 20);
	return add_cache_entry(ce, 1);
}

static int read_tree_recursive(void *buffer, unsigned long size,
			       const char *base, int baselen, int stage)
{
	while (size) {
		int len = strlen(buffer)+1;
		unsigned char *sha1 = buffer + len;
		char *path = strchr(buffer, ' ')+1;
		unsigned int mode;

		if (size < len + 20 || sscanf(buffer, "%o", &mode) != 1)
			return -1;

		buffer = sha1 + 20;
		size -= len + 20;

		if (S_ISDIR(mode)) {
			int retval;
			int pathlen = strlen(path);
			char *newbase = malloc(baselen + 1 + pathlen);
			void *eltbuf;
			char elttype[20];
			unsigned long eltsize;

			eltbuf = read_sha1_file(sha1, elttype, &eltsize);
			if (!eltbuf || strcmp(elttype, "tree"))
				return -1;
			memcpy(newbase, base, baselen);
			memcpy(newbase + baselen, path, pathlen);
			newbase[baselen + pathlen] = '/';
			retval = read_tree_recursive(eltbuf, eltsize,
						     newbase,
						     baselen + pathlen + 1, stage);
			free(eltbuf);
			free(newbase);
			if (retval)
				return -1;
			continue;
		}
		if (read_one_entry(sha1, base, baselen, path, mode, stage) < 0)
			return -1;
	}
	return 0;
}

int read_tree(void *buffer, unsigned long size, int stage)
{
	return read_tree_recursive(buffer, size, "", 0, stage);
}

struct tree *lookup_tree(unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj) {
		struct tree *ret = malloc(sizeof(struct tree));
		memset(ret, 0, sizeof(struct tree));
		created_object(sha1, &ret->object);
		ret->object.type = tree_type;
		return ret;
	}
	if (obj->type != tree_type) {
		error("Object %s is a %s, not a tree", 
		      sha1_to_hex(sha1), obj->type);
		return NULL;
	}
	return (struct tree *) obj;
}

int parse_tree(struct tree *item)
{
	char type[20];
	void *buffer, *bufptr;
	unsigned long size;
	struct tree_entry_list **list_p;
	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	buffer = bufptr = read_sha1_file(item->object.sha1, type, &size);
	if (!buffer)
		return error("Could not read %s",
			     sha1_to_hex(item->object.sha1));
	if (strcmp(type, tree_type))
		return error("Object %s not a tree",
			     sha1_to_hex(item->object.sha1));
	list_p = &item->entries;
	while (size) {
		struct object *obj;
		struct tree_entry_list *entry;
		int len = 1+strlen(bufptr);
		unsigned char *file_sha1 = bufptr + len;
		char *path = strchr(bufptr, ' ');
		unsigned int mode;
		if (size < len + 20 || !path || 
		    sscanf(bufptr, "%o", &mode) != 1)
			return -1;

		entry = malloc(sizeof(struct tree_entry_list));
		entry->name = strdup(path + 1);
		entry->directory = S_ISDIR(mode);
		entry->executable = mode & S_IXUSR;
		entry->next = NULL;

		/* Warn about trees that don't do the recursive thing.. */
		if (strchr(path, '/')) {
			item->has_full_path = 1;
		}

		bufptr += len + 20;
		size -= len + 20;

		if (entry->directory) {
			entry->item.tree = lookup_tree(file_sha1);
			obj = &entry->item.tree->object;
		} else {
			entry->item.blob = lookup_blob(file_sha1);
			obj = &entry->item.blob->object;
		}
		if (obj)
			add_ref(&item->object, obj);

		*list_p = entry;
		list_p = &entry->next;
	}
	return 0;
}
