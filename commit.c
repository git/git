#include "commit.h"
#include "cache.h"
#include <string.h>

const char *commit_type = "commit";

struct commit *lookup_commit(unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj) {
		struct commit *ret = malloc(sizeof(struct commit));
		memset(ret, 0, sizeof(struct commit));
		created_object(sha1, &ret->object);
		return ret;
	}
	if (obj->parsed && obj->type != commit_type) {
		error("Object %s is a %s, not a commit", 
		      sha1_to_hex(sha1), obj->type);
		return NULL;
	}
	return (struct commit *) obj;
}

static unsigned long parse_commit_date(const char *buf)
{
	unsigned long date;

	if (memcmp(buf, "author", 6))
		return 0;
	while (*buf++ != '\n')
		/* nada */;
	if (memcmp(buf, "committer", 9))
		return 0;
	while (*buf++ != '>')
		/* nada */;
	date = strtoul(buf, NULL, 10);
	if (date == ULONG_MAX)
		date = 0;
	return date;
}

int parse_commit(struct commit *item)
{
	char type[20];
	void * buffer, *bufptr;
	unsigned long size;
	unsigned char parent[20];
	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	buffer = bufptr = read_sha1_file(item->object.sha1, type, &size);
	if (!buffer)
		return error("Could not read %s",
			     sha1_to_hex(item->object.sha1));
	if (strcmp(type, commit_type))
		return error("Object %s not a commit",
			     sha1_to_hex(item->object.sha1));
	item->object.type = commit_type;
	get_sha1_hex(bufptr + 5, parent);
	item->tree = lookup_tree(parent);
	add_ref(&item->object, &item->tree->object);
	bufptr += 46; /* "tree " + "hex sha1" + "\n" */
	while (!memcmp(bufptr, "parent ", 7) &&
	       !get_sha1_hex(bufptr + 7, parent)) {
		struct commit_list *new_parent = 
			malloc(sizeof(struct commit_list));
		new_parent->next = item->parents;
		new_parent->item = lookup_commit(parent);
		add_ref(&item->object, &new_parent->item->object);
		item->parents = new_parent;
		bufptr += 48;
	}
	item->date = parse_commit_date(bufptr);
	free(buffer);
	return 0;
}

void free_commit_list(struct commit_list *list)
{
	while (list) {
		struct commit_list *temp = list;
		list = temp->next;
		free(temp);
	}
}
