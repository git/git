#include "cache.h"
#include "blob.h"
#include "repository.h"
#include "alloc.h"

const char *blob_type = "blob";

struct blob *lookup_blob(struct repository *r, const struct object_id *oid)
{
	struct object *obj = lookup_object(r, oid->hash);
	if (!obj)
		return create_object(r, oid->hash,
				     alloc_blob_node(r));
	return object_as_type(r, obj, OBJ_BLOB, 0);
}

int parse_blob_buffer(struct blob *item, void *buffer, unsigned long size)
{
	item->object.parsed = 1;
	return 0;
}
