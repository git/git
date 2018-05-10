#ifndef SHA1_ARRAY_H
#define SHA1_ARRAY_H

struct oid_array {
	struct object_id *oid;
	int nr;
	int alloc;
	int sorted;
};

#define OID_ARRAY_INIT { NULL, 0, 0, 0 }

void oid_array_append(struct oid_array *array, const struct object_id *oid);
int oid_array_lookup(struct oid_array *array, const struct object_id *oid);
void oid_array_clear(struct oid_array *array);

typedef int (*for_each_oid_fn)(const struct object_id *oid,
			       void *data);
int oid_array_for_each(struct oid_array *array,
		       for_each_oid_fn fn,
		       void *data);
int oid_array_for_each_unique(struct oid_array *array,
			      for_each_oid_fn fn,
			      void *data);

#endif /* SHA1_ARRAY_H */
