/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef BULK_CHECKIN_H
#define BULK_CHECKIN_H

#include "cache.h"

extern int index_bulk_checkin(struct object_id *oid,
			      int fd, size_t size, enum object_type type,
			      const char *path, unsigned flags);

extern void plug_bulk_checkin(void);
extern void unplug_bulk_checkin(void);

#endif
