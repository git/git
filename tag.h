#ifndef TAG_H
#define TAG_H

#include "object.h"

extern const char *tag_type;

struct tag {
	struct object object;
	struct object *tagged;
	char *tag;
	char *signature; /* not actually implemented */
};

#endif /* TAG_H */
