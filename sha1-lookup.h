#ifndef SHA1_LOOKUP_H
#define SHA1_LOOKUP_H

extern int sha1_entry_pos(const void *table,
			  size_t elem_size,
			  size_t key_offset,
			  unsigned lo, unsigned hi, unsigned nr,
			  const unsigned char *key);
#endif
