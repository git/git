#ifndef OBJECT_H
#define OBJECT_H

struct object_list {
	struct object *item;
	struct object_list *next;
};

struct object_refs {
	unsigned count;
	struct object *base;
	struct object *ref[FLEX_ARRAY]; /* more */
};

struct object_array {
	unsigned int nr;
	unsigned int alloc;
	struct object_array_entry {
		struct object *item;
		const char *name;
	} *objects;
};

#define TYPE_BITS   3
#define FLAG_BITS  27

/*
 * The object type is stored in 3 bits.
 */
struct object {
	unsigned parsed : 1;
	unsigned used : 1;
	unsigned type : TYPE_BITS;
	unsigned flags : FLAG_BITS;
	unsigned char sha1[20];
};

extern int track_object_refs;

extern const char *typename(unsigned int type);
extern int type_from_string(const char *str);

extern unsigned int get_max_object_index(void);
extern struct object *get_indexed_object(unsigned int);
extern struct object_refs *lookup_object_refs(struct object *);

/** Internal only **/
struct object *lookup_object(const unsigned char *sha1);

/** Returns the object, having looked it up as being the given type. **/
struct object *lookup_object_type(const unsigned char *sha1, const char *type);

void created_object(const unsigned char *sha1, struct object *obj);

/** Returns the object, having parsed it to find out what it is. **/
struct object *parse_object(const unsigned char *sha1);

/* Given the result of read_sha1_file(), returns the object after
 * parsing it.  eaten_p indicates if the object has a borrowed copy
 * of buffer and the caller should not free() it.
 */
struct object *parse_object_buffer(const unsigned char *sha1, enum object_type type, unsigned long size, void *buffer, int *eaten_p);

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

/* Object array handling .. */
void add_object_array(struct object *obj, const char *name, struct object_array *array);

#endif /* OBJECT_H */
