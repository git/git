/*
 * Copyright (c) 2011, Google Inc.
 */
#ifndef STREAMING_H
#define STREAMING_H 1
#include "cache.h"

/* opaque */
struct git_istream;

extern struct git_istream *open_istream(const unsigned char *, enum object_type *, unsigned long *, struct stream_filter *);
extern int close_istream(struct git_istream *);
extern ssize_t read_istream(struct git_istream *, char *, size_t);

#endif /* STREAMING_H */
