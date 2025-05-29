#ifndef OBJECT_H
#define OBJECT_H

#include "hash.h"

struct buffer_slab;
struct repository;

struct parsed_object_pool {
	struct repository *repo;
	struct object **obj_hash;
	int nr_objs, obj_hash_size;

	/* TODO: migrate alloc_states to mem-pool? */
	struct alloc_state *blob_state;
	struct alloc_state *tree_state;
	struct alloc_state *commit_state;
	struct alloc_state *tag_state;
	struct alloc_state *object_state;

	/* parent substitutions from .git/info/grafts and .git/shallow */
	struct commit_graft **grafts;
	int grafts_alloc, grafts_nr;

	int is_shallow;
	struct stat_validity *shallow_stat;
	char *alternate_shallow_file;

	int commit_graft_prepared;
	int substituted_parent;

	struct buffer_slab *buffer_slab;
};

struct parsed_object_pool *parsed_object_pool_new(struct repository *repo);
void parsed_object_pool_clear(struct parsed_object_pool *o);
void parsed_object_pool_reset_commit_grafts(struct parsed_object_pool *o);

struct object_list {
	struct object *item;
	struct object_list *next;
};

struct object_array {
	unsigned int nr;
	unsigned int alloc;
	struct object_array_entry {
		struct object *item;
		/*
		 * name or NULL.  If non-NULL, the memory pointed to
		 * is owned by this object *except* if it points at
		 * object_array_slopbuf, which is a static copy of the
		 * empty string.
		 */
		char *name;
		char *path;
		unsigned mode;
	} *objects;
};

#define OBJECT_ARRAY_INIT { 0 }

void object_array_init(struct object_array *array);

/*
 * object flag allocation:
 * revision.h:               0---------10         15               23------27
 * fetch-pack.c:             01    67
 * negotiator/default.c:       2--5
 * walker.c:                 0-2
 * upload-pack.c:                4       11-----14  16-----19
 * builtin/blame.c:                        12-13
 * bisect.c:                                        16
 * bundle.c:                                        16
 * http-push.c:                          11-----14
 * commit-graph.c:                                15
 * commit-reach.c:                                  16-----19
 * sha1-name.c:                                              20
 * list-objects-filter.c:                                      21
 * bloom.c:                                                    2122
 * builtin/fsck.c:           0--3
 * builtin/gc.c:             0
 * builtin/index-pack.c:                                     2021
 * reflog.c:                           10--12
 * builtin/show-branch.c:    0-------------------------------------------26
 * builtin/unpack-objects.c:                                 2021
 * pack-bitmap.h:                                              2122
 */
#define FLAG_BITS  28

#define TYPE_BITS 3

/*
 * Values in this enum (except those outside the 3 bit range) are part
 * of pack file format. See gitformat-pack(5) for more information.
 */
enum object_type {
	OBJ_BAD = -1,
	OBJ_NONE = 0,
	OBJ_COMMIT = 1,
	OBJ_TREE = 2,
	OBJ_BLOB = 3,
	OBJ_TAG = 4,
	/* 5 for future expansion */
	OBJ_OFS_DELTA = 6,
	OBJ_REF_DELTA = 7,
	OBJ_ANY,
	OBJ_MAX
};

/* unknown mode (impossible combination S_IFIFO|S_IFCHR) */
#define S_IFINVALID     0030000

/*
 * A "directory link" is a link to another git directory.
 *
 * The value 0160000 is not normally a valid mode, and
 * also just happens to be S_IFDIR + S_IFLNK
 */
#define S_IFGITLINK	0160000
#define S_ISGITLINK(m)	(((m) & S_IFMT) == S_IFGITLINK)

#define S_ISSPARSEDIR(m) ((m) == S_IFDIR)

static inline enum object_type object_type(unsigned int mode)
{
	return S_ISDIR(mode) ? OBJ_TREE :
		S_ISGITLINK(mode) ? OBJ_COMMIT :
		OBJ_BLOB;
}

#define ce_permissions(mode) (((mode) & 0100) ? 0755 : 0644)
static inline unsigned int create_ce_mode(unsigned int mode)
{
	if (S_ISLNK(mode))
		return S_IFLNK;
	if (S_ISSPARSEDIR(mode))
		return S_IFDIR;
	if (S_ISDIR(mode) || S_ISGITLINK(mode))
		return S_IFGITLINK;
	return S_IFREG | ce_permissions(mode);
}

static inline unsigned int canon_mode(unsigned int mode)
{
	if (S_ISREG(mode))
		return S_IFREG | ce_permissions(mode);
	if (S_ISLNK(mode))
		return S_IFLNK;
	if (S_ISDIR(mode))
		return S_IFDIR;
	return S_IFGITLINK;
}

/*
 * The object type is stored in 3 bits.
 */
struct object {
	unsigned parsed : 1;
	unsigned type : TYPE_BITS;
	unsigned flags : FLAG_BITS;
	struct object_id oid;
};

const char *type_name(unsigned int type);
int type_from_string_gently(const char *str, ssize_t, int gentle);
#define type_from_string(str) type_from_string_gently(str, -1, 0)

/*
 * Return the current number of buckets in the object hashmap.
 */
unsigned int get_max_object_index(const struct repository *repo);

/*
 * Return the object from the specified bucket in the object hashmap.
 */
struct object *get_indexed_object(const struct repository *repo,
				       unsigned int);

/*
 * This can be used to see if we have heard of the object before, but
 * it can return "yes we have, and here is a half-initialised object"
 * for an object that we haven't loaded/parsed yet.
 *
 * When parsing a commit to create an in-core commit object, its
 * parents list holds commit objects that represent its parents, but
 * they are expected to be lazily initialized and do not know what
 * their trees or parents are yet.  When this function returns such a
 * half-initialised objects, the caller is expected to initialize them
 * by calling parse_object() on them.
 */
struct object *lookup_object(struct repository *r, const struct object_id *oid);

void *create_object(struct repository *r, const struct object_id *oid, void *obj);

void *object_as_type(struct object *obj, enum object_type type, int quiet);


static inline const char *parse_mode(const char *str, uint16_t *modep)
{
	unsigned char c;
	unsigned int mode = 0;

	if (*str == ' ')
		return NULL;

	while ((c = *str++) != ' ') {
		if (c < '0' || c > '7')
			return NULL;
		mode = (mode << 3) + (c - '0');
	}
	*modep = mode;
	return str;
}

/*
 * Returns the object, having parsed it to find out what it is.
 *
 * Returns NULL if the object is missing or corrupt.
 */
enum parse_object_flags {
	PARSE_OBJECT_SKIP_HASH_CHECK = 1 << 0,
	PARSE_OBJECT_DISCARD_TREE = 1 << 1,
};
struct object *parse_object(struct repository *r, const struct object_id *oid);
struct object *parse_object_with_flags(struct repository *r,
				       const struct object_id *oid,
				       enum parse_object_flags flags);

/*
 * Like parse_object, but will die() instead of returning NULL. If the
 * "name" parameter is not NULL, it is included in the error message
 * (otherwise, the hex object ID is given).
 */
struct object *parse_object_or_die(struct repository *repo, const struct object_id *oid,
				   const char *name);

/* Given the result of read_sha1_file(), returns the object after
 * parsing it.  eaten_p indicates if the object has a borrowed copy
 * of buffer and the caller should not free() it.
 */
struct object *parse_object_buffer(struct repository *r, const struct object_id *oid, enum object_type type, unsigned long size, void *buffer, int *eaten_p);

/*
 * Allocate and return an object struct, even if you do not know the type of
 * the object. The returned object may have its "type" field set to a real type
 * (if somebody previously called lookup_blob(), etc), or it may be set to
 * OBJ_NONE. In the latter case, subsequent calls to lookup_blob(), etc, will
 * set the type field as appropriate.
 *
 * Use this when you do not know the expected type of an object and want to
 * avoid parsing it for efficiency reasons. Try to avoid it otherwise; it
 * may allocate excess memory, since the returned object must be as large as
 * the maximum struct of any type.
 */
struct object *lookup_unknown_object(struct repository *r, const struct object_id *oid);

/*
 * Dispatch to the appropriate lookup_blob(), lookup_commit(), etc, based on
 * "type".
 */
struct object *lookup_object_by_type(struct repository *r, const struct object_id *oid,
				     enum object_type type);

enum peel_status {
	/* object was peeled successfully: */
	PEEL_PEELED = 0,

	/*
	 * object cannot be peeled because the named object (or an
	 * object referred to by a tag in the peel chain), does not
	 * exist.
	 */
	PEEL_INVALID = -1,

	/* object cannot be peeled because it is not a tag: */
	PEEL_NON_TAG = -2,

	/* ref_entry contains no peeled value because it is a symref: */
	PEEL_IS_SYMREF = -3,

	/*
	 * ref_entry cannot be peeled because it is broken (i.e., the
	 * symbolic reference cannot even be resolved to an object
	 * name):
	 */
	PEEL_BROKEN = -4
};

/*
 * Peel the named object; i.e., if the object is a tag, resolve the
 * tag recursively until a non-tag is found.  If successful, store the
 * result to oid and return PEEL_PEELED.  If the object is not a tag
 * or is not valid, return PEEL_NON_TAG or PEEL_INVALID, respectively,
 * and leave oid unchanged.
 */
enum peel_status peel_object(struct repository *r,
			     const struct object_id *name, struct object_id *oid);

struct object_list *object_list_insert(struct object *item,
				       struct object_list **list_p);

int object_list_contains(struct object_list *list, struct object *obj);

void object_list_free(struct object_list **list);

/* Object array handling .. */
void add_object_array(struct object *obj, const char *name, struct object_array *array);
void add_object_array_with_path(struct object *obj, const char *name, struct object_array *array, unsigned mode, const char *path);

/*
 * Returns NULL if the array is empty. Otherwise, returns the last object
 * after removing its entry from the array. Other resources associated
 * with that object are left in an unspecified state and should not be
 * examined.
 */
struct object *object_array_pop(struct object_array *array);

typedef int (*object_array_each_func_t)(struct object_array_entry *, void *);

/*
 * Apply want to each entry in array, retaining only the entries for
 * which the function returns true.  Preserve the order of the entries
 * that are retained.
 */
void object_array_filter(struct object_array *array,
			 object_array_each_func_t want, void *cb_data);

/*
 * Remove any objects from the array, freeing all used memory; afterwards
 * the array is ready to store more objects with add_object_array().
 */
void object_array_clear(struct object_array *array);

void clear_object_flags(struct repository *repo, unsigned flags);

/*
 * Clear the specified object flags from all in-core commit objects from
 * the specified repository.
 */
void repo_clear_commit_marks(struct repository *r, unsigned int flags);

#endif /* OBJECT_H */
