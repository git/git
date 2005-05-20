#include "object.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "tag.h"
#include "delta.h"
#include "cache.h"
#include <string.h>

/* the delta object definition (it can alias any other object) */
struct delta {
	union {
		struct object object;
		struct blob blob;
		struct tree tree;
		struct commit commit;
		struct tag tag;
	} u;
};

struct delta *lookup_delta(unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj) {
		struct delta *ret = xmalloc(sizeof(struct delta));
		memset(ret, 0, sizeof(struct delta));
		created_object(sha1, &ret->u.object);
		return ret;
	}
	return (struct delta *) obj;
}

int parse_delta_buffer(struct delta *item, void *buffer, unsigned long size)
{
	struct object *reference;
	struct object_list *p;

	if (item->u.object.delta)
		return 0;
	item->u.object.delta = 1;
	if (size <= 20)
		return -1;
	reference = lookup_object(buffer);
	if (!reference) {
		struct delta *ref = xmalloc(sizeof(struct delta));
		memset(ref, 0, sizeof(struct delta));
		created_object(buffer, &ref->u.object);
		reference = &ref->u.object;
	}

	p = xmalloc(sizeof(*p));
	p->item = &item->u.object;
	p->next = reference->attached_deltas;
	reference->attached_deltas = p;
	return 0;
}

int process_deltas(void *src, unsigned long src_size, const char *src_type,
		   struct object_list *delta_list)
{
	int deepest = 0;
	do {
		struct object *obj = delta_list->item;
		static char type[10];
		void *map, *delta, *buf;
		unsigned long map_size, delta_size, buf_size;
		map = map_sha1_file(obj->sha1, &map_size);
		if (!map)
			continue;
		delta = unpack_sha1_file(map, map_size, type, &delta_size);
		munmap(map, map_size);
		if (!delta)
			continue;
		if (strcmp(type, "delta") || delta_size <= 20) {
			free(delta);
			continue;
		}
		buf = patch_delta(src, src_size,
				  delta+20, delta_size-20,
				  &buf_size);
		free(delta);
		if (!buf)
			continue;
		if (check_sha1_signature(obj->sha1, buf, buf_size, src_type) < 0)
			printf("sha1 mismatch for delta %s\n", sha1_to_hex(obj->sha1));
		if (obj->type && obj->type != src_type) {
			error("got %s when expecting %s for delta %s",
			      src_type, obj->type, sha1_to_hex(obj->sha1));
			free(buf);
			continue;
		}
		obj->type = src_type;
		if (src_type == blob_type) {
			parse_blob_buffer((struct blob *)obj, buf, buf_size);
		} else if (src_type == tree_type) {
			parse_tree_buffer((struct tree *)obj, buf, buf_size);
		} else if (src_type == commit_type) {
			parse_commit_buffer((struct commit *)obj, buf, buf_size);
		} else if (src_type == tag_type) {
			parse_tag_buffer((struct tag *)obj, buf, buf_size);
		} else {
			error("unknown object type %s", src_type);
			free(buf);
			continue;
		}
		if (obj->attached_deltas) {
			int depth = process_deltas(buf, buf_size, src_type,
						   obj->attached_deltas);
			if (deepest < depth)
				deepest = depth;
		}
		free(buf);
	} while ((delta_list = delta_list->next));
	return deepest + 1;
}
