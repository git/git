#ifndef DECORATE_H
#define DECORATE_H

struct object_decoration {
	struct object *base;
	void *decoration;
};

struct decoration {
	const char *name;
	unsigned int size, nr;
	struct object_decoration *hash;
};

extern void *add_decoration(struct decoration *n, struct object *obj, void *decoration);
extern void *lookup_decoration(struct decoration *n, struct object *obj);

#endif
