#ifndef TAG_H
#define TAG_H

#include "object.h"

extern const char *tag_type;

struct tag {
	struct object object;
	struct object *tagged;
	char *tag;
	timestamp_t date;
};
struct tag *lookup_tag(struct repository *r, const struct object_id *oid);
int parse_tag_buffer(struct repository *r, struct tag *item, const void *data, unsigned long size);
int parse_tag(struct tag *item);
void release_tag_memory(struct tag *t);
struct object *deref_tag(struct repository *r, struct object *, const char *, int);
struct object *deref_tag_noverify(struct object *);
int gpg_verify_tag(const struct object_id *oid,
		   const char *name_to_report, unsigned flags);

#endif /* TAG_H */
