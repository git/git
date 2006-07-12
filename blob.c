#include "cache.h"
#include "blob.h"
#include <stdlib.h>

const char *blob_type = "blob";

struct blob *lookup_blob(const unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj) {
		struct blob *ret = alloc_blob_node();
		created_object(sha1, &ret->object);
		ret->object.type = OBJ_BLOB;
		return ret;
	}
	if (!obj->type)
		obj->type = OBJ_BLOB;
	if (obj->type != OBJ_BLOB) {
		error("Object %s is a %s, not a blob",
		      sha1_to_hex(sha1), typename(obj->type));
		return NULL;
	}
	return (struct blob *) obj;
}

int parse_blob_buffer(struct blob *item, void *buffer, unsigned long size)
{
	item->object.parsed = 1;
	return 0;
}

int parse_blob(struct blob *item)
{
        char type[20];
        void *buffer;
        unsigned long size;
	int ret;

        if (item->object.parsed)
                return 0;
        buffer = read_sha1_file(item->object.sha1, type, &size);
        if (!buffer)
                return error("Could not read %s",
                             sha1_to_hex(item->object.sha1));
        if (strcmp(type, blob_type))
                return error("Object %s not a blob",
                             sha1_to_hex(item->object.sha1));
	ret = parse_blob_buffer(item, buffer, size);
	free(buffer);
	return ret;
}
