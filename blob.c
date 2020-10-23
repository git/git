#include "cache.h" // Standard Libraries Included.
#include "blob.h"    
#include "repository.h" 
#include "alloc.h"       

const char *blob_type = "blob"; // Constant Defined

struct blob *lookup_blob(struct repository *r, const struct object_id *oid) //Structure is defined for lookup 
{
	struct object *obj = lookup_object(r, oid);
	if (!obj)
		return create_object(r, oid, alloc_blob_node(r));
	return object_as_type(obj, OBJ_BLOB, 0);
}

int parse_blob_buffer(struct blob *item, void *buffer, unsigned long size) // Function is Defined
{
	item->object.parsed = 1;
	return 0;
}
