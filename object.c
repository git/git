#include "cache.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "tag.h"

struct object **objs;
static int nr_objs;
int obj_allocs;

const char *type_names[] = {
	"none", "blob", "tree", "commit", "bad"
};

static int hashtable_index(const unsigned char *sha1)
{
	unsigned int i;
	memcpy(&i, sha1, sizeof(unsigned int));
	return (int)(i % obj_allocs);
}

static int find_object(const unsigned char *sha1)
{
	int i;

	if (!objs)
		return -1;

	i = hashtable_index(sha1);
	while (objs[i]) {
		if (memcmp(sha1, objs[i]->sha1, 20) == 0)
			return i;
		i++;
		if (i == obj_allocs)
			i = 0;
	}
	return -1 - i;
}

struct object *lookup_object(const unsigned char *sha1)
{
	int pos = find_object(sha1);
	if (pos >= 0)
		return objs[pos];
	return NULL;
}

void created_object(const unsigned char *sha1, struct object *obj)
{
	int pos;

	obj->parsed = 0;
	memcpy(obj->sha1, sha1, 20);
	obj->type = TYPE_NONE;
	obj->used = 0;

	if (obj_allocs - 1 <= nr_objs * 2) {
		int i, count = obj_allocs;
		obj_allocs = (obj_allocs < 32 ? 32 : 2 * obj_allocs);
		objs = xrealloc(objs, obj_allocs * sizeof(struct object *));
		memset(objs + count, 0, (obj_allocs - count)
				* sizeof(struct object *));
		for (i = 0; i < obj_allocs; i++)
			if (objs[i]) {
				int j = find_object(objs[i]->sha1);
				if (j != i) {
					j = -1 - j;
					objs[j] = objs[i];
					objs[i] = NULL;
				}
			}
	}

	pos = find_object(sha1);
	if (pos >= 0)
		die("Inserting %s twice\n", sha1_to_hex(sha1));
	pos = -pos-1;

	objs[pos] = obj;
	nr_objs++;
}

struct object *lookup_object_type(const unsigned char *sha1, const char *type)
{
	if (!type) {
		return lookup_unknown_object(sha1);
	} else if (!strcmp(type, blob_type)) {
		return &lookup_blob(sha1)->object;
	} else if (!strcmp(type, tree_type)) {
		return &lookup_tree(sha1)->object;
	} else if (!strcmp(type, commit_type)) {
		return &lookup_commit(sha1)->object;
	} else if (!strcmp(type, tag_type)) {
		return &lookup_tag(sha1)->object;
	} else {
		error("Unknown type %s", type);
		return NULL;
	}
}

union any_object {
	struct object object;
	struct commit commit;
	struct tree tree;
	struct blob blob;
	struct tag tag;
};

struct object *lookup_unknown_object(const unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj) {
		union any_object *ret = xcalloc(1, sizeof(*ret));
		created_object(sha1, &ret->object);
		ret->object.type = TYPE_NONE;
		return &ret->object;
	}
	return obj;
}

struct object *parse_object(const unsigned char *sha1)
{
	unsigned long size;
	char type[20];
	void *buffer = read_sha1_file(sha1, type, &size);
	if (buffer) {
		struct object *obj;
		if (check_sha1_signature(sha1, buffer, size, type) < 0)
			printf("sha1 mismatch %s\n", sha1_to_hex(sha1));
		if (!strcmp(type, blob_type)) {
			struct blob *blob = lookup_blob(sha1);
			parse_blob_buffer(blob, buffer, size);
			obj = &blob->object;
		} else if (!strcmp(type, tree_type)) {
			struct tree *tree = lookup_tree(sha1);
			obj = &tree->object;
			if (!tree->object.parsed) {
				parse_tree_buffer(tree, buffer, size);
				buffer = NULL;
			}
		} else if (!strcmp(type, commit_type)) {
			struct commit *commit = lookup_commit(sha1);
			parse_commit_buffer(commit, buffer, size);
			if (!commit->buffer) {
				commit->buffer = buffer;
				buffer = NULL;
			}
			obj = &commit->object;
		} else if (!strcmp(type, tag_type)) {
			struct tag *tag = lookup_tag(sha1);
			parse_tag_buffer(tag, buffer, size);
			obj = &tag->object;
		} else {
			obj = NULL;
		}
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

void object_list_append(struct object *item,
			struct object_list **list_p)
{
	while (*list_p) {
		list_p = &((*list_p)->next);
	}
	*list_p = xmalloc(sizeof(struct object_list));
	(*list_p)->next = NULL;
	(*list_p)->item = item;
}

unsigned object_list_length(struct object_list *list)
{
	unsigned ret = 0;
	while (list) {
		list = list->next;
		ret++;
	}
	return ret;
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
	array->nr = ++nr;
}
