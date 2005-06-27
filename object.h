#ifndef OBJECT_H
#define OBJECT_H

struct object_list {
	struct object *item;
	struct object_list *next;
	const char *name;
};

struct object {
	unsigned parsed : 1;
	unsigned used : 1;
	unsigned int flags;
	unsigned char sha1[20];
	const char *type;
	struct object_list *refs;
	void *util;
};

extern int nr_objs;
extern struct object **objs;

/** Internal only **/
struct object *lookup_object(const unsigned char *sha1);

/** Returns the object, having looked it up as being the given type. **/
struct object *lookup_object_type(const unsigned char *sha1, const char *type);

void created_object(const unsigned char *sha1, struct object *obj);

/** Returns the object, having parsed it to find out what it is. **/
struct object *parse_object(const unsigned char *sha1);

void add_ref(struct object *refer, struct object *target);

void mark_reachable(struct object *obj, unsigned int mask);

#endif /* OBJECT_H */
