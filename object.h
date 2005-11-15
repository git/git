#ifndef OBJECT_H
#define OBJECT_H

struct object_list {
	struct object *item;
	struct object_list *next;
	const char *name;
};

struct object_refs {
	unsigned count;
	struct object *ref[0];
};

struct object {
	unsigned parsed : 1;
	unsigned used : 1;
	unsigned int flags;
	unsigned char sha1[20];
	const char *type;
	struct object_refs *refs;
	void *util;
};

extern int track_object_refs;
extern int nr_objs;
extern struct object **objs;

/** Internal only **/
struct object *lookup_object(const unsigned char *sha1);

/** Returns the object, having looked it up as being the given type. **/
struct object *lookup_object_type(const unsigned char *sha1, const char *type);

void created_object(const unsigned char *sha1, struct object *obj);

/** Returns the object, having parsed it to find out what it is. **/
struct object *parse_object(const unsigned char *sha1);

/** Returns the object, with potentially excess memory allocated. **/
struct object *lookup_unknown_object(const unsigned  char *sha1);

struct object_refs *alloc_object_refs(unsigned count);
void set_object_refs(struct object *obj, struct object_refs *refs);

void mark_reachable(struct object *obj, unsigned int mask);

struct object_list *object_list_insert(struct object *item, 
				       struct object_list **list_p);

void object_list_append(struct object *item,
			struct object_list **list_p);

unsigned object_list_length(struct object_list *list);

int object_list_contains(struct object_list *list, struct object *obj);

#endif /* OBJECT_H */
