#include "git-compat-util.h"
#include "blob.h"
#include "repository.h"
#include "alloc.h"

const char *blob_type = "blob";

struct blob *lookup_blob_type(struct repository *r,
			      const struct object_id *oid,
			      enum object_type type)
{
	struct object *obj = lookup_object(r, oid);
	if (!obj)
		return create_object(r, oid, alloc_blob_node(r));
	return object_as_type_hint(obj, OBJ_BLOB, type);
}

struct blob *lookup_blob(struct repository *r, const struct object_id *oid)
{
	return lookup_blob_type(r, oid, OBJ_NONE);
}

void parse_blob_buffer(struct blob *item)
{
	item->object.parsed = 1;
}
