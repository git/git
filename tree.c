#include "tree.h"
#include "blob.h"
#include "cache.h"
#include <stdlib.h>

const char *tree_type = "tree";

struct tree *lookup_tree(unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj) {
		struct tree *ret = malloc(sizeof(struct tree));
		memset(ret, 0, sizeof(struct tree));
		created_object(sha1, &ret->object);
		return ret;
	}
	if (obj->parsed && obj->type != tree_type) {
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
	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	item->object.type = tree_type;
	buffer = bufptr = read_sha1_file(item->object.sha1, type, &size);
	if (!buffer)
		return error("Could not read %s",
			     sha1_to_hex(item->object.sha1));
	if (strcmp(type, tree_type))
		return error("Object %s not a tree",
			     sha1_to_hex(item->object.sha1));
	while (size) {
		struct object *obj;
		int len = 1+strlen(bufptr);
		unsigned char *file_sha1 = bufptr + len;
		char *path = strchr(bufptr, ' ');
		unsigned int mode;
		if (size < len + 20 || !path || 
		    sscanf(bufptr, "%o", &mode) != 1)
			return -1;

		/* Warn about trees that don't do the recursive thing.. */
		if (strchr(path, '/')) {
			item->has_full_path = 1;
		}

		bufptr += len + 20;
		size -= len + 20;

		if (S_ISDIR(mode)) {
			obj = &lookup_tree(file_sha1)->object;
		} else {
			obj = &lookup_blob(file_sha1)->object;
		}
		add_ref(&item->object, obj);
	}
	return 0;
}
