/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef STREAMING_H
#define STREAMING_H 1

#include "object.h"

/* opaque */
struct odb_read_stream;
struct stream_filter;

struct odb_read_stream *open_istream(struct repository *, const struct object_id *,
				     enum object_type *, unsigned long *,
				     struct stream_filter *);
int close_istream(struct odb_read_stream *);
ssize_t read_istream(struct odb_read_stream *, void *, size_t);

int stream_blob_to_fd(int fd, const struct object_id *, struct stream_filter *, int can_seek);

#endif /* STREAMING_H */
