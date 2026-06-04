#ifndef NOTES_CACHE_H
#define NOTES_CACHE_H

#include "notes.h"

struct repository;

struct notes_cache {
	struct notes_tree tree;
	char *validity;
};

void notes_cache_init(struct repository *r, struct notes_cache *c,
		      const char *name, const char *validity);
int notes_cache_write(struct notes_cache *c);

char *notes_cache_get(struct notes_cache *c, struct object_id *oid, size_t
		      *outsize);
int notes_cache_put(struct notes_cache *c, struct object_id *oid,
		    const char *data, size_t size);

#endif /* NOTES_CACHE_H */
