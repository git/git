#ifndef BLOB_H
#define BLOB_H

#include "object.h"

extern const char *blob_type;

struct blob {
	struct object object;
};

struct blob *lookup_blob(unsigned char *sha1);

int parse_blob(struct blob *item);

#endif /* BLOB_H */
