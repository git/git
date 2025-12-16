/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef STREAMING_H
#define STREAMING_H 1

#include "object.h"

struct object_database;
struct odb_read_stream;
struct stream_filter;

typedef int (*odb_read_stream_close_fn)(struct odb_read_stream *);
typedef ssize_t (*odb_read_stream_read_fn)(struct odb_read_stream *, char *, size_t);

/*
 * A stream that can be used to read an object from the object database without
 * loading all of it into memory.
 */
struct odb_read_stream {
	odb_read_stream_close_fn close;
	odb_read_stream_read_fn read;
	enum object_type type;
	unsigned long size; /* inflated size of full object */
};

/*
 * Create a new object stream for the given object database. An optional filter
 * can be used to transform the object's content.
 *
 * Returns the stream on success, a `NULL` pointer otherwise.
 */
struct odb_read_stream *odb_read_stream_open(struct object_database *odb,
					     const struct object_id *oid,
					     struct stream_filter *filter);

/*
 * Close the given read stream and release all resources associated with it.
 * Returns 0 on success, a negative error code otherwise.
 */
int odb_read_stream_close(struct odb_read_stream *stream);

/*
 * Read data from the stream into the buffer. Returns 0 on EOF and the number
 * of bytes read on success. Returns a negative error code in case reading from
 * the stream fails.
 */
ssize_t odb_read_stream_read(struct odb_read_stream *stream, void *buf, size_t len);

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
