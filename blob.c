#include "git-compat-util.h"
#include "blob.h"
#include "alloc.h"

const char *blob_type = "blob";

struct blob *lookup_blob(struct repository *r, const struct object_id *oid)
{
	struct object *obj = lookup_object(r, oid);
	if (!obj)
		return create_object(r, oid, alloc_blob_node(r));
	return object_as_type(obj, OBJ_BLOB, 0);
}

void parse_blob_buffer(struct blob *item)
{
	item->object.parsed = 1;
}
