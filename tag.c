#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"

const char *tag_type = "tag";

struct object *deref_tag(struct object *o, const char *warn, int warnlen)
{
	while (o && o->type == OBJ_TAG)
		if (((struct tag *)o)->tagged)
			o = parse_object(((struct tag *)o)->tagged->sha1);
		else
			o = NULL;
	if (!o && warn) {
		if (!warnlen)
			warnlen = strlen(warn);
		error("missing object referenced by '%.*s'", warnlen, warn);
	}
	return o;
}

struct tag *lookup_tag(const unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj)
		return create_object(sha1, OBJ_TAG, alloc_tag_node());
	if (!obj->type)
		obj->type = OBJ_TAG;
	if (obj->type != OBJ_TAG) {
		error("Object %s is a %s, not a tag",
		      sha1_to_hex(sha1), typename(obj->type));
		return NULL;
	}
	return (struct tag *) obj;
}

static unsigned long parse_tag_date(const char *buf, const char *tail)
{
	const char *dateptr;

	while (buf < tail && *buf++ != '>')
		/* nada */;
	if (buf >= tail)
		return 0;
	dateptr = buf;
	while (buf < tail && *buf++ != '\n')
		/* nada */;
	if (buf >= tail)
		return 0;
	/* dateptr < buf && buf[-1] == '\n', so strtoul will stop at buf-1 */
	return strtoul(dateptr, NULL, 10);
}

int parse_tag_buffer(struct tag *item, void *data, unsigned long size)
{
	unsigned char sha1[20];
	char type[20];
	const char *bufptr = data;
	const char *tail = bufptr + size;
	const char *nl;

	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;

	if (size < 64)
		return -1;
	if (memcmp("object ", bufptr, 7) || get_sha1_hex(bufptr + 7, sha1) || bufptr[47] != '\n')
		return -1;
	bufptr += 48; /* "object " + sha1 + "\n" */

	if (prefixcmp(bufptr, "type "))
		return -1;
	bufptr += 5;
	nl = memchr(bufptr, '\n', tail - bufptr);
	if (!nl || sizeof(type) <= (nl - bufptr))
		return -1;
	strncpy(type, bufptr, nl - bufptr);
	type[nl - bufptr] = '\0';
	bufptr = nl + 1;

	if (!strcmp(type, blob_type)) {
		item->tagged = &lookup_blob(sha1)->object;
	} else if (!strcmp(type, tree_type)) {
		item->tagged = &lookup_tree(sha1)->object;
	} else if (!strcmp(type, commit_type)) {
		item->tagged = &lookup_commit(sha1)->object;
	} else if (!strcmp(type, tag_type)) {
		item->tagged = &lookup_tag(sha1)->object;
	} else {
		error("Unknown type %s", type);
		item->tagged = NULL;
	}

	if (prefixcmp(bufptr, "tag "))
		return -1;
	bufptr += 4;
	nl = memchr(bufptr, '\n', tail - bufptr);
	if (!nl)
		return -1;
	item->tag = xmemdupz(bufptr, nl - bufptr);
	bufptr = nl + 1;

	if (!prefixcmp(bufptr, "tagger "))
		item->date = parse_tag_date(bufptr, tail);
	else
		item->date = 0;

	return 0;
}

int parse_tag(struct tag *item)
{
	enum object_type type;
	void *data;
	unsigned long size;
	int ret;

	if (item->object.parsed)
		return 0;
	data = read_sha1_file(item->object.sha1, &type, &size);
	if (!data)
		return error("Could not read %s",
			     sha1_to_hex(item->object.sha1));
	if (type != OBJ_TAG) {
		free(data);
		return error("Object %s not a tag",
			     sha1_to_hex(item->object.sha1));
	}
	ret = parse_tag_buffer(item, data, size);
	free(data);
	return ret;
}
