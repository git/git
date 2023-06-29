#ifndef OBJECT_STORE_H
#define OBJECT_STORE_H

#include "khash.h"
#include "dir.h"
#include "object-store-ll.h"

KHASH_INIT(odb_path_map, const char * /* key: odb_path */,
	struct object_directory *, 1, fspathhash, fspatheq)

#endif /* OBJECT_STORE_H */
