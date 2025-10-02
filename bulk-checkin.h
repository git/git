/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef BULK_CHECKIN_H
#define BULK_CHECKIN_H

#include "object.h"
#include "odb.h"

struct odb_transaction;

void prepare_loose_object_bulk_checkin(struct odb_transaction *transaction);
void fsync_loose_object_bulk_checkin(struct odb_transaction *transaction,
				     int fd, const char *filename);

/*
 * This writes the specified object to a packfile. Objects written here
 * during the same transaction are written to the same packfile. The
 * packfile is not flushed until the transaction is flushed. The caller
 * is expected to ensure a valid transaction is setup for objects to be
 * recorded to.
 *
 * This also bypasses the usual "convert-to-git" dance, and that is on
 * purpose. We could write a streaming version of the converting
 * functions and insert that before feeding the data to fast-import
 * (or equivalent in-core API described above). However, that is
 * somewhat complicated, as we do not know the size of the filter
 * result, which we need to know beforehand when writing a git object.
 * Since the primary motivation for trying to stream from the working
 * tree file and to avoid mmaping it in core is to deal with large
 * binary blobs, they generally do not want to get any conversion, and
 * callers should avoid this code path when filters are requested.
 */
int index_blob_bulk_checkin(struct odb_transaction *transaction,
			    struct object_id *oid, int fd, size_t size,
			    const char *path, unsigned flags);

/*
 * Tell the object database to optimize for adding
 * multiple objects. end_odb_transaction must be called
 * to make new objects visible. Transactions can be nested,
 * and objects are only visible after the outermost transaction
 * is complete or the transaction is flushed.
 */
struct odb_transaction *begin_odb_transaction(struct object_database *odb);

/*
 * Make any objects that are currently part of a pending object
 * database transaction visible. It is valid to call this function
 * even if no transaction is active.
 */
void flush_odb_transaction(struct odb_transaction *transaction);

/*
 * Tell the object database to make any objects from the
 * current transaction visible if this is the final nested
 * transaction.
 */
void end_odb_transaction(struct odb_transaction *transaction);

#endif
