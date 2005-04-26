#include "object.h"
#include "cache.h"
#include <stdlib.h>
#include <string.h>

struct object **objs;
int nr_objs;
static int obj_allocs;

static int find_object(unsigned char *sha1)
{
	int first = 0, last = nr_objs;

        while (first < last) {
                int next = (first + last) / 2;
                struct object *obj = objs[next];
                int cmp;

                cmp = memcmp(sha1, obj->sha1, 20);
                if (!cmp)
                        return next;
                if (cmp < 0) {
                        last = next;
                        continue;
                }
                first = next+1;
        }
        return -first-1;
}

struct object *lookup_object(unsigned char *sha1)
{
	int pos = find_object(sha1);
	if (pos >= 0)
		return objs[pos];
	return NULL;
}

void created_object(unsigned char *sha1, struct object *obj)
{
	int pos = find_object(sha1);

	obj->parsed = 0;
	memcpy(obj->sha1, sha1, 20);
	obj->type = NULL;
	obj->refs = NULL;
	obj->used = 0;

	if (pos >= 0)
		die("Inserting %s twice\n", sha1_to_hex(sha1));
	pos = -pos-1;

	if (obj_allocs == nr_objs) {
		obj_allocs = alloc_nr(obj_allocs);
		objs = xrealloc(objs, obj_allocs * sizeof(struct object *));
	}

	/* Insert it into the right place */
	memmove(objs + pos + 1, objs + pos, (nr_objs - pos) * 
		sizeof(struct object *));

	objs[pos] = obj;
	nr_objs++;
}

void add_ref(struct object *refer, struct object *target)
{
	struct object_list **pp = &refer->refs;
	struct object_list *p;
	
	while ((p = *pp) != NULL) {
		if (p->item == target)
			return;
		pp = &p->next;
	}

	target->used = 1;
	p = xmalloc(sizeof(*p));
	p->item = target;
	p->next = NULL;
	*pp = p;
}

void mark_reachable(struct object *obj, unsigned int mask)
{
	struct object_list *p = obj->refs;

	/* If we've been here already, don't bother */
	if (obj->flags & mask)
		return;
	obj->flags |= mask;
	while (p) {
		mark_reachable(p->item, mask);
		p = p->next;
	}
}
