/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef STREAMING_H
#define STREAMING_H 1
#include "cache.h"

/* opaque */
struct git_istream;

extern struct git_istream *open_istream(const struct object_id *, enum object_type *, unsigned long *, struct stream_filter *);
extern int close_istream(struct git_istream *);
extern ssize_t read_istream(struct git_istream *, void *, size_t);

extern int stream_blob_to_fd(int fd, const struct object_id *, struct stream_filter *, int can_seek);

#endif /* STREAMING_H */
