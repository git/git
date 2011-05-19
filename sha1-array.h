#ifndef SHA1_ARRAY_H
#define SHA1_ARRAY_H

struct sha1_array {
	unsigned char (*sha1)[20];
	int nr;
	int alloc;
	int sorted;
};

#define SHA1_ARRAY_INIT { NULL, 0, 0, 0 }

void sha1_array_append(struct sha1_array *array, const unsigned char *sha1);
void sha1_array_sort(struct sha1_array *array);
int sha1_array_lookup(struct sha1_array *array, const unsigned char *sha1);
void sha1_array_clear(struct sha1_array *array);

#endif /* SHA1_ARRAY_H */
