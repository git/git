#include "cache.h"
#include "blob.h"
#include "repository.h"
#include "alloc.h"

const char *blob_type = "blob";

struct blob *lookup_blob(const struct object_id *oid)
{
	struct object *obj = lookup_object(oid->hash);
	if (!obj)
		return create_object(the_repository, oid->hash,
				     alloc_blob_node(the_repository));
	return object_as_type(obj, OBJ_BLOB, 0);
}

int parse_blob_buffer(struct blob *item, void *buffer, unsigned long size)
{
	item->object.parsed = 1;
	return 0;
}
