#ifndef SHA1_ARRAY_H
#define SHA1_ARRAY_H

struct sha1_array {
	struct object_id *oid;
	int nr;
	int alloc;
	int sorted;
};

#define SHA1_ARRAY_INIT { NULL, 0, 0, 0 }

void sha1_array_append(struct sha1_array *array, const struct object_id *oid);
int sha1_array_lookup(struct sha1_array *array, const struct object_id *oid);
void sha1_array_clear(struct sha1_array *array);

typedef int (*for_each_oid_fn)(const struct object_id *oid,
			       void *data);
int sha1_array_for_each_unique(struct sha1_array *array,
			       for_each_oid_fn fn,
			       void *data);

#endif /* SHA1_ARRAY_H */
