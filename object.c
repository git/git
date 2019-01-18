#include "cache.h"
#include "object.h"
#include "replace-object.h"
#include "object-store.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "tag.h"
#include "alloc.h"
#include "object-store.h"
#include "packfile.h"
#include "commit-graph.h"

unsigned int get_max_object_index(void)
{
	return the_repository->parsed_objects->obj_hash_size;
}

struct object *get_indexed_object(unsigned int idx)
{
	return the_repository->parsed_objects->obj_hash[idx];
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
		if (!strncmp(str, object_type_strings[i], len) &&
		    object_type_strings[i][len] == '\0')
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
static unsigned int hash_obj(const unsigned char *sha1, unsigned int n)
{
	return sha1hash(sha1) & (n - 1);
}

/*
 * Insert obj into the hash table hash, which has length size (which
 * must be a power of 2).  On collisions, simply overflow to the next
 * empty bucket.
 */
static void insert_obj_hash(struct object *obj, struct object **hash, unsigned int size)
{
	unsigned int j = hash_obj(obj->oid.hash, size);

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
struct object *lookup_object(struct repository *r, const unsigned char *sha1)
{
	unsigned int i, first;
	struct object *obj;

	if (!r->parsed_objects->obj_hash)
		return NULL;

	first = i = hash_obj(sha1, r->parsed_objects->obj_hash_size);
	while ((obj = r->parsed_objects->obj_hash[i]) != NULL) {
		if (hasheq(sha1, obj->oid.hash))
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

	new_hash = xcalloc(new_hash_size, sizeof(struct object *));
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

void *create_object(struct repository *r, const unsigned char *sha1, void *o)
{
	struct object *obj = o;

	obj->parsed = 0;
	obj->flags = 0;
	hashcpy(obj->oid.hash, sha1);

	if (r->parsed_objects->obj_hash_size - 1 <= r->parsed_objects->nr_objs * 2)
		grow_object_hash(r);

	insert_obj_hash(obj, r->parsed_objects->obj_hash,
			r->parsed_objects->obj_hash_size);
	r->parsed_objects->nr_objs++;
	return obj;
}

void *object_as_type(struct repository *r, struct object *obj, enum object_type type, int quiet)
{
	if (obj->type == type)
		return obj;
	else if (obj->type == OBJ_NONE) {
		if (type == OBJ_COMMIT)
			((struct commit *)obj)->index = alloc_commit_index(r);
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

struct object *lookup_unknown_object(const unsigned char *sha1)
{
	struct object *obj = lookup_object(the_repository, sha1);
	if (!obj)
		obj = create_object(the_repository, sha1,
				    alloc_object_node(the_repository));
	return obj;
}

struct object *parse_object_buffer(struct repository *r, const struct object_id *oid, enum object_type type, unsigned long size, void *buffer, int *eaten_p)
{
	struct object *obj;
	*eaten_p = 0;

	obj = NULL;
	if (type == OBJ_BLOB) {
		struct blob *blob = lookup_blob(r, oid);
		if (blob) {
			if (parse_blob_buffer(blob, buffer, size))
				return NULL;
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
			if (!get_cached_commit_buffer(r, commit, NULL)) {
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

struct object *parse_object_or_die(const struct object_id *oid,
				   const char *name)
{
	struct object *o = parse_object(the_repository, oid);
	if (o)
		return o;

	die(_("unable to parse object: %s"), name ? name : oid_to_hex(oid));
}

struct object *parse_object(struct repository *r, const struct object_id *oid)
{
	unsigned long size;
	enum object_type type;
	int eaten;
	const struct object_id *repl = lookup_replace_object(r, oid);
	void *buffer;
	struct object *obj;

	obj = lookup_object(r, oid->hash);
	if (obj && obj->parsed)
		return obj;

	if ((obj && obj->type == OBJ_BLOB && has_object_file(oid)) ||
	    (!obj && has_object_file(oid) &&
	     oid_object_info(r, oid, NULL) == OBJ_BLOB)) {
		if (check_object_signature(repl, NULL, 0, NULL) < 0) {
			error(_("sha1 mismatch %s"), oid_to_hex(oid));
			return NULL;
		}
		parse_blob_buffer(lookup_blob(r, oid), NULL, 0);
		return lookup_object(r, oid->hash);
	}

	buffer = read_object_file(oid, &type, &size);
	if (buffer) {
		if (check_object_signature(repl, buffer, size, type_name(type)) < 0) {
			free(buffer);
			error(_("sha1 mismatch %s"), oid_to_hex(repl));
			return NULL;
		}

		obj = parse_object_buffer(r, oid, type, size,
					  buffer, &eaten);
		if (!eaten)
			free(buffer);
		return obj;
	}
	return NULL;
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

/*
 * A zero-length string to which object_array_entry::name can be
 * initialized without requiring a malloc/free.
 */
static char object_array_slopbuf[1];

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

/*
 * Return true iff array already contains an entry with name.
 */
static int contains_name(struct object_array *array, const char *name)
{
	unsigned nr = array->nr, i;
	struct object_array_entry *object = array->objects;

	for (i = 0; i < nr; i++, object++)
		if (!strcmp(object->name, name))
			return 1;
	return 0;
}

void object_array_remove_duplicates(struct object_array *array)
{
	unsigned nr = array->nr, src;
	struct object_array_entry *objects = array->objects;

	array->nr = 0;
	for (src = 0; src < nr; src++) {
		if (!contains_name(array, objects[src].name)) {
			if (src != array->nr)
				objects[array->nr] = objects[src];
			array->nr++;
		} else {
			object_array_release_entry(&objects[src]);
		}
	}
}

void clear_object_flags(unsigned flags)
{
	int i;

	for (i=0; i < the_repository->parsed_objects->obj_hash_size; i++) {
		struct object *obj = the_repository->parsed_objects->obj_hash[i];
		if (obj)
			obj->flags &= ~flags;
	}
}

void clear_commit_marks_all(unsigned int flags)
{
	int i;

	for (i = 0; i < the_repository->parsed_objects->obj_hash_size; i++) {
		struct object *obj = the_repository->parsed_objects->obj_hash[i];
		if (obj && obj->type == OBJ_COMMIT)
			obj->flags &= ~flags;
	}
}

struct parsed_object_pool *parsed_object_pool_new(void)
{
	struct parsed_object_pool *o = xmalloc(sizeof(*o));
	memset(o, 0, sizeof(*o));

	o->blob_state = allocate_alloc_state();
	o->tree_state = allocate_alloc_state();
	o->commit_state = allocate_alloc_state();
	o->tag_state = allocate_alloc_state();
	o->object_state = allocate_alloc_state();

	o->is_shallow = -1;
	o->shallow_stat = xcalloc(1, sizeof(*o->shallow_stat));

	o->buffer_slab = allocate_commit_buffer_slab();

	return o;
}

struct raw_object_store *raw_object_store_new(void)
{
	struct raw_object_store *o = xmalloc(sizeof(*o));

	memset(o, 0, sizeof(*o));
	INIT_LIST_HEAD(&o->packed_git_mru);
	return o;
}

static void free_object_directory(struct object_directory *odb)
{
	free(odb->path);
	odb_clear_loose_cache(odb);
	free(odb);
}

static void free_object_directories(struct raw_object_store *o)
{
	while (o->odb) {
		struct object_directory *next;

		next = o->odb->next;
		free_object_directory(o->odb);
		o->odb = next;
	}
}

void raw_object_store_clear(struct raw_object_store *o)
{
	FREE_AND_NULL(o->alternate_db);

	oidmap_free(o->replace_map, 1);
	FREE_AND_NULL(o->replace_map);

	free_commit_graph(o->commit_graph);
	o->commit_graph = NULL;
	o->commit_graph_attempted = 0;

	free_object_directories(o);
	o->odb_tail = NULL;
	o->loaded_alternates = 0;

	INIT_LIST_HEAD(&o->packed_git_mru);
	close_all_packs(o);
	o->packed_git = NULL;
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
			release_commit_memory((struct commit*)obj);
		else if (obj->type == OBJ_TAG)
			release_tag_memory((struct tag*)obj);
	}

	FREE_AND_NULL(o->obj_hash);
	o->obj_hash_size = 0;

	free_commit_buffer_slab(o->buffer_slab);
	o->buffer_slab = NULL;

	clear_alloc_state(o->blob_state);
	clear_alloc_state(o->tree_state);
	clear_alloc_state(o->commit_state);
	clear_alloc_state(o->tag_state);
	clear_alloc_state(o->object_state);
	FREE_AND_NULL(o->blob_state);
	FREE_AND_NULL(o->tree_state);
	FREE_AND_NULL(o->commit_state);
	FREE_AND_NULL(o->tag_state);
	FREE_AND_NULL(o->object_state);
}
