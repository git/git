/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef BULK_CHECKIN_H
#define BULK_CHECKIN_H

#include "cache.h"

int index_bulk_checkin(struct object_id *oid,
		       int fd, size_t size, enum object_type type,
		       const char *path, unsigned flags);

/*
 * Tell the object database to optimize for adding
 * multiple objects. end_odb_transaction must be called
 * to make new objects visible.
 */
void begin_odb_transaction(void);

/*
 * Tell the object database to make any objects from the
 * current transaction visible.
 */
void end_odb_transaction(void);

#endif
