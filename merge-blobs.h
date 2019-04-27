#ifndef MERGE_BLOBS_H
#define MERGE_BLOBS_H

struct blob;
struct index_state;

extern void *merge_blobs(struct index_state *, const char *,
			 struct blob *, struct blob *,
			 struct blob *, unsigned long *);

#endif /* MERGE_BLOBS_H */
