#ifndef MERGE_BLOBS_H
#define MERGE_BLOBS_H

#include "blob.h"

extern void *merge_blobs(const char *, struct blob *, struct blob *, struct blob *, unsigned long *);

#endif /* MERGE_BLOBS_H */
