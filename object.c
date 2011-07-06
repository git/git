#include "cache.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "tag.h"

static struct object **obj_hash;
static int nr_objs, obj_hash_size;

unsigned int get_max_object_index(void)
{
	return obj_hash_size;
}

struct object *get_indexed_object(unsigned int idx)
{
	return obj_hash[idx];
}

static const char *object_type_strings[] = {
	NULL,		/* OBJ_NONE = 0 */
	"commit",	/* OBJ_COMMIT = 1 */
	"tree",		/* OBJ_TREE = 2 */
	"blob",		/* OBJ_BLOB = 3 */
	"tag",		/* OBJ_TAG = 4 */
};

const char *typename(unsigned int type)
{
	if (type >= ARRAY_SIZE(object_type_strings))
		return NULL;
	return object_type_strings[type];
}

int type_from_string(const char *str)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(object_type_strings); i++)
		if (!strcmp(str, object_type_strings[i]))
			return i;
	die("invalid object type \"%s\"", str);
}

static unsigned int hash_obj(struct object *obj, unsigned int n)
{
	unsigned int hash;
	memcpy(&hash, obj->sha1, sizeof(unsigned int));
	return hash % n;
}

static void insert_obj_hash(struct object *obj, struct object **hash, unsigned int size)
{
	unsigned int j = hash_obj(obj, size);

	while (hash[j]) {
		j++;
		if (j >= size)
			j = 0;
	}
	hash[j] = obj;
}

static unsigned int hashtable_index(const unsigned char *sha1)
{
	unsigned int i;
	memcpy(&i, sha1, sizeof(unsigned int));
	return i % obj_hash_size;
}

struct object *lookup_object(const unsigned char *sha1)
{
	unsigned int i;
	struct object *obj;

	if (!obj_hash)
		return NULL;

	i = hashtable_index(sha1);
	while ((obj = obj_hash[i]) != NULL) {
		if (!hashcmp(sha1, obj->sha1))
			break;
		i++;
		if (i == obj_hash_size)
			i = 0;
	}
	return obj;
}

static void grow_object_hash(void)
{
	int i;
	int new_hash_size = obj_hash_size < 32 ? 32 : 2 * obj_hash_size;
	struct object **new_hash;

	new_hash = xcalloc(new_hash_size, sizeof(struct object *));
	for (i = 0; i < obj_hash_size; i++) {
		struct object *obj = obj_hash[i];
		if (!obj)
			continue;
		insert_obj_hash(obj, new_hash, new_hash_size);
	}
	free(obj_hash);
	obj_hash = new_hash;
	obj_hash_size = new_hash_size;
}

void *create_object(const unsigned char *sha1, int type, void *o)
{
	struct object *obj = o;

	obj->parsed = 0;
	obj->used = 0;
	obj->type = type;
	obj->flags = 0;
	hashcpy(obj->sha1, sha1);

	if (obj_hash_size - 1 <= nr_objs * 2)
		grow_object_hash();

	insert_obj_hash(obj, obj_hash, obj_hash_size);
	nr_objs++;
	return obj;
}

struct object *lookup_unknown_object(const unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj)
		obj = create_object(sha1, OBJ_NONE, alloc_object_node());
	return obj;
}

struct object *parse_object_buffer(const unsigned char *sha1, enum object_type type, unsigned long size, void *buffer, int *eaten_p)
{
	struct object *obj;
	int eaten = 0;

	obj = NULL;
	if (type == OBJ_BLOB) {
		struct blob *blob = lookup_blob(sha1);
		if (blob) {
			if (parse_blob_buffer(blob, buffer, size))
				return NULL;
			obj = &blob->object;
		}
	} else if (type == OBJ_TREE) {
		struct tree *tree = lookup_tree(sha1);
		if (tree) {
			obj = &tree->object;
			if (!tree->object.parsed) {
				if (parse_tree_buffer(tree, buffer, size))
					return NULL;
				eaten = 1;
			}
		}
	} else if (type == OBJ_COMMIT) {
		struct commit *commit = lookup_commit(sha1);
		if (commit) {
			if (parse_commit_buffer(commit, buffer, size))
				return NULL;
			if (!commit->buffer) {
				commit->buffer = buffer;
				eaten = 1;
			}
			obj = &commit->object;
		}
	} else if (type == OBJ_TAG) {
		struct tag *tag = lookup_tag(sha1);
		if (tag) {
			if (parse_tag_buffer(tag, buffer, size))
			       return NULL;
			obj = &tag->object;
		}
	} else {
		warning("object %s has unknown type id %d\n", sha1_to_hex(sha1), type);
		obj = NULL;
	}
	if (obj && obj->type == OBJ_NONE)
		obj->type = type;
	*eaten_p = eaten;
	return obj;
}

struct object *parse_object(const unsigned char *sha1)
{
	unsigned long size;
	enum object_type type;
	int eaten;
	const unsigned char *repl = lookup_replace_object(sha1);
	void *buffer = read_sha1_file(sha1, &type, &size);

	if (buffer) {
		struct object *obj;
		if (check_sha1_signature(repl, buffer, size, typename(type)) < 0) {
			free(buffer);
			error("sha1 mismatch %s\n", sha1_to_hex(repl));
			return NULL;
		}

		obj = parse_object_buffer(sha1, type, size, buffer, &eaten);
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

void add_object_array(struct object *obj, const char *name, struct object_array *array)
{
	add_object_array_with_mode(obj, name, array, S_IFINVALID);
}

void add_object_array_with_mode(struct object *obj, const char *name, struct object_array *array, unsigned mode)
{
	unsigned nr = array->nr;
	unsigned alloc = array->alloc;
	struct object_array_entry *objects = array->objects;

	if (nr >= alloc) {
		alloc = (alloc + 32) * 2;
		objects = xrealloc(objects, alloc * sizeof(*objects));
		array->alloc = alloc;
		array->objects = objects;
	}
	objects[nr].item = obj;
	objects[nr].name = name;
	objects[nr].mode = mode;
	array->nr = ++nr;
}

void object_array_remove_duplicates(struct object_array *array)
{
	unsigned int ref, src, dst;
	struct object_array_entry *objects = array->objects;

	for (ref = 0; ref + 1 < array->nr; ref++) {
		for (src = ref + 1, dst = src;
		     src < array->nr;
		     src++) {
			if (!strcmp(objects[ref].name, objects[src].name))
				continue;
			if (src != dst)
				objects[dst] = objects[src];
			dst++;
		}
		array->nr = dst;
	}
}
