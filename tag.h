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
#define lookup_tag(r, o) lookup_tag_##r(o)
extern struct tag *lookup_tag_the_repository(const struct object_id *oid);
#define parse_tag_buffer(r, i, d, s) parse_tag_buffer_##r(i, d, s)
extern int parse_tag_buffer_the_repository(struct tag *item, const void *data, unsigned long size);
extern int parse_tag(struct tag *item);
extern void release_tag_memory(struct tag *t);
#define deref_tag(r, o, w, l) deref_tag_##r(o, w, l)
extern struct object *deref_tag_the_repository(struct object *, const char *, int);
extern struct object *deref_tag_noverify(struct object *);
extern int gpg_verify_tag(const struct object_id *oid,
		const char *name_to_report, unsigned flags);

#endif /* TAG_H */
