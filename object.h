#ifndef OBJECT_H
#define OBJECT_H

struct object_list {
	struct object *item;
	struct object_list *next;
	const char *name;
};

struct object_refs {
	unsigned count;
	struct object *ref[FLEX_ARRAY]; /* more */
};

#define TYPE_BITS   3
#define FLAG_BITS  27

#define TYPE_NONE   0
#define TYPE_BLOB   1
#define TYPE_TREE   2
#define TYPE_COMMIT 3
#define TYPE_TAG    4
#define TYPE_BAD    5

struct object {
	unsigned parsed : 1;
	unsigned used : 1;
	unsigned type : TYPE_BITS;
	unsigned flags : FLAG_BITS;
	unsigned char sha1[20];
	struct object_refs *refs;
	void *util;
};

extern int track_object_refs;
extern int obj_allocs;
extern struct object **objs;
extern const char *type_names[];

static inline const char *typename(unsigned int type)
{
	return type_names[type > TYPE_TAG ? TYPE_BAD : type];
}

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
