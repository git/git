/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef STREAMING_H
#define STREAMING_H 1

#include "object.h"

struct object_database;
/* opaque */
struct odb_read_stream;
struct stream_filter;

struct odb_read_stream *open_istream(struct repository *, const struct object_id *,
				     enum object_type *, unsigned long *,
				     struct stream_filter *);
int close_istream(struct odb_read_stream *);
ssize_t read_istream(struct odb_read_stream *, void *, size_t);

/*
 * Look up the object by its ID and write the full contents to the file
 * descriptor. The object must be a blob, or the function will fail. When
 * provided, the filter is used to transform the blob contents.
 *
 * `can_seek` should be set to 1 in case the given file descriptor can be
 * seek(3p)'d on. This is used to support files with holes in case a
 * significant portion of the blob contains NUL bytes.
 *
 * Returns a negative error code on failure, 0 on success.
 */
int odb_stream_blob_to_fd(struct object_database *odb,
			  int fd,
			  const struct object_id *oid,
			  struct stream_filter *filter,
			  int can_seek);

#endif /* STREAMING_H */
