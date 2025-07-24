#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "object.h"
#include "replace-object.h"
#include "object-file.h"
#include "blob.h"
#include "statinfo.h"
#include "tree.h"
#include "commit.h"
#include "tag.h"
#include "alloc.h"
#include "commit-graph.h"

unsigned int get_max_object_index(const struct repository *repo)
{
	return repo->parsed_objects->obj_hash_size;
}

struct object *get_indexed_object(const struct repository *repo,
				       unsigned int idx)
{
	return repo->parsed_objects->obj_hash[idx];
}

static const char *object_type_strings[] = {
	NULL,		/* OBJ_NONE = 0 */
	"commit",	/* OBJ_COMMIT = 1 */
	"tree",		/* OBJ_TREE = 2 */
	"blob",		/* OBJ_BLOB = 3 */
	"tag",		/* OBJ_TAG = 4 */
};

const char *type_name(unsigned int type)
{
	if (type >= ARRAY_SIZE(object_type_strings))
		return NULL;
	return object_type_strings[type];
}

int type_from_string_gently(const char *str, ssize_t len, int gentle)
{
	int i;

	if (len < 0)
		len = strlen(str);

	for (i = 1; i < ARRAY_SIZE(object_type_strings); i++)
		if (!xstrncmpz(object_type_strings[i], str, len))
			return i;

	if (gentle)
		return -1;

	die(_("invalid object type \"%s\""), str);
}

/*
 * Return a numerical hash value between 0 and n-1 for the object with
 * the specified sha1.  n must be a power of 2.  Please note that the
 * return value is *not* consistent across computer architectures.
 */
static unsigned int hash_obj(const struct object_id *oid, unsigned int n)
{
	return oidhash(oid) & (n - 1);
}

/*
 * Insert obj into the hash table hash, which has length size (which
 * must be a power of 2).  On collisions, simply overflow to the next
 * empty bucket.
 */
static void insert_obj_hash(struct object *obj, struct object **hash, unsigned int size)
{
	unsigned int j = hash_obj(&obj->oid, size);

	while (hash[j]) {
		j++;
		if (j >= size)
			j = 0;
	}
	hash[j] = obj;
}

/*
 * Look up the record for the given sha1 in the hash map stored in
 * obj_hash.  Return NULL if it was not found.
 */
struct object *lookup_object(struct repository *r, const struct object_id *oid)
{
	unsigned int i, first;
	struct object *obj;

	if (!r->parsed_objects->obj_hash)
		return NULL;

	first = i = hash_obj(oid, r->parsed_objects->obj_hash_size);
	while ((obj = r->parsed_objects->obj_hash[i]) != NULL) {
		if (oideq(oid, &obj->oid))
			break;
		i++;
		if (i == r->parsed_objects->obj_hash_size)
			i = 0;
	}
	if (obj && i != first) {
		/*
		 * Move object to where we started to look for it so
		 * that we do not need to walk the hash table the next
		 * time we look for it.
		 */
		SWAP(r->parsed_objects->obj_hash[i],
		     r->parsed_objects->obj_hash[first]);
	}
	return obj;
}

/*
 * Increase the size of the hash map stored in obj_hash to the next
 * power of 2 (but at least 32).  Copy the existing values to the new
 * hash map.
 */
static void grow_object_hash(struct repository *r)
{
	int i;
	/*
	 * Note that this size must always be power-of-2 to match hash_obj
	 * above.
	 */
	int new_hash_size = r->parsed_objects->obj_hash_size < 32 ? 32 : 2 * r->parsed_objects->obj_hash_size;
	struct object **new_hash;

	CALLOC_ARRAY(new_hash, new_hash_size);
	for (i = 0; i < r->parsed_objects->obj_hash_size; i++) {
		struct object *obj = r->parsed_objects->obj_hash[i];

		if (!obj)
			continue;
		insert_obj_hash(obj, new_hash, new_hash_size);
	}
	free(r->parsed_objects->obj_hash);
	r->parsed_objects->obj_hash = new_hash;
	r->parsed_objects->obj_hash_size = new_hash_size;
}

void *create_object(struct repository *r, const struct object_id *oid, void *o)
{
	struct object *obj = o;

	obj->parsed = 0;
	obj->flags = 0;
	oidcpy(&obj->oid, oid);

	if (r->parsed_objects->obj_hash_size - 1 <= r->parsed_objects->nr_objs * 2)
		grow_object_hash(r);

	insert_obj_hash(obj, r->parsed_objects->obj_hash,
			r->parsed_objects->obj_hash_size);
	r->parsed_objects->nr_objs++;
	return obj;
}

void *object_as_type(struct object *obj, enum object_type type, int quiet)
{
	if (obj->type == type)
		return obj;
	else if (obj->type == OBJ_NONE) {
		if (type == OBJ_COMMIT)
			init_commit_node((struct commit *) obj);
		else
			obj->type = type;
		return obj;
	}
	else {
		if (!quiet)
			error(_("object %s is a %s, not a %s"),
			      oid_to_hex(&obj->oid),
			      type_name(obj->type), type_name(type));
		return NULL;
	}
}

struct object *lookup_unknown_object(struct repository *r, const struct object_id *oid)
{
	struct object *obj = lookup_object(r, oid);
	if (!obj)
		obj = create_object(r, oid, alloc_object_node(r));
	return obj;
}

struct object *lookup_object_by_type(struct repository *r,
			    const struct object_id *oid,
			    enum object_type type)
{
	switch (type) {
	case OBJ_COMMIT:
		return (struct object *)lookup_commit(r, oid);
	case OBJ_TREE:
		return (struct object *)lookup_tree(r, oid);
	case OBJ_TAG:
		return (struct object *)lookup_tag(r, oid);
	case OBJ_BLOB:
		return (struct object *)lookup_blob(r, oid);
	default:
		BUG("unknown object type %d", type);
	}
}

enum peel_status peel_object(struct repository *r,
			     const struct object_id *name,
			     struct object_id *oid)
{
	struct object *o = lookup_unknown_object(r, name);

	if (o->type == OBJ_NONE) {
		int type = odb_read_object_info(r->objects, name, NULL);
		if (type < 0 || !object_as_type(o, type, 0))
			return PEEL_INVALID;
	}

	if (o->type != OBJ_TAG)
		return PEEL_NON_TAG;

	o = deref_tag_noverify(r, o);
	if (!o)
		return PEEL_INVALID;

	oidcpy(oid, &o->oid);
	return PEEL_PEELED;
}

struct object *parse_object_buffer(struct repository *r, const struct object_id *oid, enum object_type type, unsigned long size, void *buffer, int *eaten_p)
{
	struct object *obj;
	*eaten_p = 0;

	obj = NULL;
	if (type == OBJ_BLOB) {
		struct blob *blob = lookup_blob(r, oid);
		if (blob) {
			parse_blob_buffer(blob);
			obj = &blob->object;
		}
	} else if (type == OBJ_TREE) {
		struct tree *tree = lookup_tree(r, oid);
		if (tree) {
			obj = &tree->object;
			if (!tree->buffer)
				tree->object.parsed = 0;
			if (!tree->object.parsed) {
				if (parse_tree_buffer(tree, buffer, size))
					return NULL;
				*eaten_p = 1;
			}
		}
	} else if (type == OBJ_COMMIT) {
		struct commit *commit = lookup_commit(r, oid);
		if (commit) {
			if (parse_commit_buffer(r, commit, buffer, size, 1))
				return NULL;
			if (save_commit_buffer &&
			    !get_cached_commit_buffer(r, commit, NULL)) {
				set_commit_buffer(r, commit, buffer, size);
				*eaten_p = 1;
			}
			obj = &commit->object;
		}
	} else if (type == OBJ_TAG) {
		struct tag *tag = lookup_tag(r, oid);
		if (tag) {
			if (parse_tag_buffer(r, tag, buffer, size))
			       return NULL;
			obj = &tag->object;
		}
	} else {
		warning(_("object %s has unknown type id %d"), oid_to_hex(oid), type);
		obj = NULL;
	}
	return obj;
}

struct object *parse_object_or_die(struct repository *repo,
				   const struct object_id *oid,
				   const char *name)
{
	struct object *o = parse_object(repo, oid);
	if (o)
		return o;

	die(_("unable to parse object: %s"), name ? name : oid_to_hex(oid));
}

struct object *parse_object_with_flags(struct repository *r,
				       const struct object_id *oid,
				       enum parse_object_flags flags)
{
	int skip_hash = !!(flags & PARSE_OBJECT_SKIP_HASH_CHECK);
	int discard_tree = !!(flags & PARSE_OBJECT_DISCARD_TREE);
	unsigned long size;
	enum object_type type;
	int eaten;
	const struct object_id *repl = lookup_replace_object(r, oid);
	void *buffer;
	struct object *obj;

	obj = lookup_object(r, oid);
	if (obj && obj->parsed)
		return obj;

	if (skip_hash) {
		struct commit *commit = lookup_commit_in_graph(r, repl);
		if (commit)
			return &commit->object;
	}

	if ((!obj || obj->type == OBJ_BLOB) &&
	    odb_read_object_info(r->objects, oid, NULL) == OBJ_BLOB) {
		if (!skip_hash && stream_object_signature(r, repl) < 0) {
			error(_("hash mismatch %s"), oid_to_hex(oid));
			return NULL;
		}
		parse_blob_buffer(lookup_blob(r, oid));
		return lookup_object(r, oid);
	}

	/*
	 * If the caller does not care about the tree buffer and does not
	 * care about checking the hash, we can simply verify that we
	 * have the on-disk object with the correct type.
	 */
	if (skip_hash && discard_tree &&
	    (!obj || obj->type == OBJ_TREE) &&
	    odb_read_object_info(r->objects, oid, NULL) == OBJ_TREE) {
		return &lookup_tree(r, oid)->object;
	}

	buffer = odb_read_object(r->objects, oid, &type, &size);
	if (buffer) {
		if (!skip_hash &&
		    check_object_signature(r, repl, buffer, size, type) < 0) {
			free(buffer);
			error(_("hash mismatch %s"), oid_to_hex(repl));
			return NULL;
		}

		obj = parse_object_buffer(r, oid, type, size,
					  buffer, &eaten);
		if (!eaten)
			free(buffer);
		if (discard_tree && type == OBJ_TREE)
			free_tree_buffer((struct tree *)obj);
		return obj;
	}
	return NULL;
}

struct object *parse_object(struct repository *r, const struct object_id *oid)
{
	return parse_object_with_flags(r, oid, 0);
}

struct object_list *object_list_insert(struct object *item,
				       struct object_list **list_p)
{
	struct object_list *new_list = xmalloc(sizeof(struct object_list));
	new_list->item = item;
	new_list->next = *list_p;
	*list_p = new_list;
	return new_list;
}

int object_list_contains(struct object_list *list, struct object *obj)
{
	while (list) {
		if (list->item == obj)
			return 1;
		list = list->next;
	}
	return 0;
}

void object_list_free(struct object_list **list)
{
	while (*list) {
		struct object_list *p = *list;
		*list = p->next;
		free(p);
	}
}

/*
 * A zero-length string to which object_array_entry::name can be
 * initialized without requiring a malloc/free.
 */
static char object_array_slopbuf[1];

void object_array_init(struct object_array *array)
{
	struct object_array blank = OBJECT_ARRAY_INIT;
	memcpy(array, &blank, sizeof(*array));
}

void add_object_array_with_path(struct object *obj, const char *name,
				struct object_array *array,
				unsigned mode, const char *path)
{
	unsigned nr = array->nr;
	unsigned alloc = array->alloc;
	struct object_array_entry *objects = array->objects;
	struct object_array_entry *entry;

	if (nr >= alloc) {
		alloc = (alloc + 32) * 2;
		REALLOC_ARRAY(objects, alloc);
		array->alloc = alloc;
		array->objects = objects;
	}
	entry = &objects[nr];
	entry->item = obj;
	if (!name)
		entry->name = NULL;
	else if (!*name)
		/* Use our own empty string instead of allocating one: */
		entry->name = object_array_slopbuf;
	else
		entry->name = xstrdup(name);
	entry->mode = mode;
	if (path)
		entry->path = xstrdup(path);
	else
		entry->path = NULL;
	array->nr = ++nr;
}

void add_object_array(struct object *obj, const char *name, struct object_array *array)
{
	add_object_array_with_path(obj, name, array, S_IFINVALID, NULL);
}

/*
 * Free all memory associated with an entry; the result is
 * in an unspecified state and should not be examined.
 */
static void object_array_release_entry(struct object_array_entry *ent)
{
	if (ent->name != object_array_slopbuf)
		free(ent->name);
	free(ent->path);
}

struct object *object_array_pop(struct object_array *array)
{
	struct object *ret;

	if (!array->nr)
		return NULL;

	ret = array->objects[array->nr - 1].item;
	object_array_release_entry(&array->objects[array->nr - 1]);
	array->nr--;
	return ret;
}

void object_array_filter(struct object_array *array,
			 object_array_each_func_t want, void *cb_data)
{
	unsigned nr = array->nr, src, dst;
	struct object_array_entry *objects = array->objects;

	for (src = dst = 0; src < nr; src++) {
		if (want(&objects[src], cb_data)) {
			if (src != dst)
				objects[dst] = objects[src];
			dst++;
		} else {
			object_array_release_entry(&objects[src]);
		}
	}
	array->nr = dst;
}

void object_array_clear(struct object_array *array)
{
	int i;
	for (i = 0; i < array->nr; i++)
		object_array_release_entry(&array->objects[i]);
	FREE_AND_NULL(array->objects);
	array->nr = array->alloc = 0;
}

void clear_object_flags(struct repository *repo, unsigned flags)
{
	int i;

	for (i = 0; i < repo->parsed_objects->obj_hash_size; i++) {
		struct object *obj = repo->parsed_objects->obj_hash[i];
		if (obj)
			obj->flags &= ~flags;
	}
}

void repo_clear_commit_marks(struct repository *r, unsigned int flags)
{
	int i;

	for (i = 0; i < r->parsed_objects->obj_hash_size; i++) {
		struct object *obj = r->parsed_objects->obj_hash[i];
		if (obj && obj->type == OBJ_COMMIT)
			obj->flags &= ~flags;
	}
}

struct parsed_object_pool *parsed_object_pool_new(struct repository *repo)
{
	struct parsed_object_pool *o = xmalloc(sizeof(*o));
	memset(o, 0, sizeof(*o));

	o->repo = repo;
	o->blob_state = allocate_alloc_state();
	o->tree_state = allocate_alloc_state();
	o->commit_state = allocate_alloc_state();
	o->tag_state = allocate_alloc_state();
	o->object_state = allocate_alloc_state();

	o->is_shallow = -1;
	CALLOC_ARRAY(o->shallow_stat, 1);

	o->buffer_slab = allocate_commit_buffer_slab();

	return o;
}

void parsed_object_pool_reset_commit_grafts(struct parsed_object_pool *o)
{
	for (int i = 0; i < o->grafts_nr; i++) {
		unparse_commit(o->repo, &o->grafts[i]->oid);
		free(o->grafts[i]);
	}
	o->grafts_nr = 0;
	o->commit_graft_prepared = 0;
}

void parsed_object_pool_clear(struct parsed_object_pool *o)
{
	/*
	 * As objects are allocated in slabs (see alloc.c), we do
	 * not need to free each object, but each slab instead.
	 *
	 * Before doing so, we need to free any additional memory
	 * the objects may hold.
	 */
	unsigned i;

	for (i = 0; i < o->obj_hash_size; i++) {
		struct object *obj = o->obj_hash[i];

		if (!obj)
			continue;

		if (obj->type == OBJ_TREE)
			free_tree_buffer((struct tree*)obj);
		else if (obj->type == OBJ_COMMIT)
			release_commit_memory(o, (struct commit*)obj);
		else if (obj->type == OBJ_TAG)
			release_tag_memory((struct tag*)obj);
	}

	FREE_AND_NULL(o->obj_hash);
	o->obj_hash_size = 0;

	free_commit_buffer_slab(o->buffer_slab);
	o->buffer_slab = NULL;

	parsed_object_pool_reset_commit_grafts(o);
	clear_alloc_state(o->blob_state);
	clear_alloc_state(o->tree_state);
	clear_alloc_state(o->commit_state);
	clear_alloc_state(o->tag_state);
	clear_alloc_state(o->object_state);
	stat_validity_clear(o->shallow_stat);
	FREE_AND_NULL(o->blob_state);
	FREE_AND_NULL(o->tree_state);
	FREE_AND_NULL(o->commit_state);
	FREE_AND_NULL(o->tag_state);
	FREE_AND_NULL(o->object_state);
	FREE_AND_NULL(o->shallow_stat);
}
