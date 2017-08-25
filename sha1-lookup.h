#ifndef SHA1_LOOKUP_H
#define SHA1_LOOKUP_H

typedef const unsigned char *sha1_access_fn(size_t index, void *table);

extern int sha1_pos(const unsigned char *sha1,
		    void *table,
		    size_t nr,
		    sha1_access_fn fn);
#endif
