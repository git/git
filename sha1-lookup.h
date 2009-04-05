#ifndef SHA1_LOOKUP_H
#define SHA1_LOOKUP_H

typedef const unsigned char *sha1_access_fn(size_t index, void *table);

extern int sha1_pos(const unsigned char *sha1,
		    void *table,
		    size_t nr,
		    sha1_access_fn fn);

extern int sha1_entry_pos(const void *table,
			  size_t elem_size,
			  size_t key_offset,
			  unsigned lo, unsigned hi, unsigned nr,
			  const unsigned char *key);
#endif
