#ifndef MERGE_BLOBS_H
#define MERGE_BLOBS_H

struct blob;
struct index_state;

void *merge_blobs(struct index_state *, const char *,
		  struct blob *, struct blob *,
		  struct blob *, size_t *);

#endif /* MERGE_BLOBS_H */
