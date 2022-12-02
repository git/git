#ifndef BLOB_H
#define BLOB_H

#include "object.h"

extern const char *blob_type;

struct blob {
	struct object object;
};

struct blob *lookup_blob(struct repository *r, const struct object_id *oid);
struct blob *lookup_blob_type(struct repository *r,
			      const struct object_id *oid,
			      enum object_type type);

int parse_blob_buffer(struct blob *item, void *buffer, unsigned long size);

/**
 * Blobs do not contain references to other objects and do not have
 * structured data that needs parsing. However, code may use the
 * "parsed" bit in the struct object for a blob to determine whether
 * its content has been found to actually be available, so
 * parse_blob_buffer() is used (by object.c) to flag that the object
 * has been read successfully from the database.
 **/

#endif /* BLOB_H */
