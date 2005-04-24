#include "blob.h"
#include "cache.h"
#include <stdlib.h>

const char *blob_type = "blob";

struct blob *lookup_blob(unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj) {
		struct blob *ret = malloc(sizeof(struct blob));
		memset(ret, 0, sizeof(struct blob));
		created_object(sha1, &ret->object);
		ret->object.type = blob_type;
		return ret;
	}
	if (obj->parsed && obj->type != blob_type) {
		error("Object %s is a %s, not a blob", 
		      sha1_to_hex(sha1), obj->type);
		return NULL;
	}
	return (struct blob *) obj;
}
