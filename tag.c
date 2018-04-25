#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "tree.h"
#include "blob.h"
#include "gpg-interface.h"

const char *tag_type = "tag";

static int run_gpg_verify(const char *buf, unsigned long size, unsigned flags)
{
	struct signature_check sigc;
	size_t payload_size;
	int ret;

	memset(&sigc, 0, sizeof(sigc));

	payload_size = parse_signature(buf, size);

	if (size == payload_size) {
		if (flags & GPG_VERIFY_VERBOSE)
			write_in_full(1, buf, payload_size);
		return error("no signature found");
	}

	ret = check_signature(buf, payload_size, buf + payload_size,
				size - payload_size, &sigc);

	if (!(flags & GPG_VERIFY_OMIT_STATUS))
		print_signature_buffer(&sigc, flags);

	signature_check_clear(&sigc);
	return ret;
}

int gpg_verify_tag(const struct object_id *oid, const char *name_to_report,
		unsigned flags)
{
	enum object_type type;
	char *buf;
	unsigned long size;
	int ret;

	type = oid_object_info(the_repository, oid, NULL);
	if (type != OBJ_TAG)
		return error("%s: cannot verify a non-tag object of type %s.",
				name_to_report ?
				name_to_report :
				find_unique_abbrev(oid, DEFAULT_ABBREV),
				type_name(type));

	buf = read_object_file(oid, &type, &size);
	if (!buf)
		return error("%s: unable to read file.",
				name_to_report ?
				name_to_report :
				find_unique_abbrev(oid, DEFAULT_ABBREV));

	ret = run_gpg_verify(buf, size, flags);

	free(buf);
	return ret;
}

struct object *deref_tag(struct object *o, const char *warn, int warnlen)
{
	while (o && o->type == OBJ_TAG)
		if (((struct tag *)o)->tagged)
			o = parse_object(&((struct tag *)o)->tagged->oid);
		else
			o = NULL;
	if (!o && warn) {
		if (!warnlen)
			warnlen = strlen(warn);
		error("missing object referenced by '%.*s'", warnlen, warn);
	}
	return o;
}

struct object *deref_tag_noverify(struct object *o)
{
	while (o && o->type == OBJ_TAG) {
		o = parse_object(&o->oid);
		if (o && o->type == OBJ_TAG && ((struct tag *)o)->tagged)
			o = ((struct tag *)o)->tagged;
		else
			o = NULL;
	}
	return o;
}

struct tag *lookup_tag(const struct object_id *oid)
{
	struct object *obj = lookup_object(oid->hash);
	if (!obj)
		return create_object(oid->hash, alloc_tag_node());
	return object_as_type(obj, OBJ_TAG, 0);
}

static timestamp_t parse_tag_date(const char *buf, const char *tail)
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
	/* dateptr < buf && buf[-1] == '\n', so parsing will stop at buf-1 */
	return parse_timestamp(dateptr, NULL, 10);
}

int parse_tag_buffer(struct tag *item, const void *data, unsigned long size)
{
	struct object_id oid;
	char type[20];
	const char *bufptr = data;
	const char *tail = bufptr + size;
	const char *nl;

	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;

	if (size < GIT_SHA1_HEXSZ + 24)
		return -1;
	if (memcmp("object ", bufptr, 7) || parse_oid_hex(bufptr + 7, &oid, &bufptr) || *bufptr++ != '\n')
		return -1;

	if (!starts_with(bufptr, "type "))
		return -1;
	bufptr += 5;
	nl = memchr(bufptr, '\n', tail - bufptr);
	if (!nl || sizeof(type) <= (nl - bufptr))
		return -1;
	memcpy(type, bufptr, nl - bufptr);
	type[nl - bufptr] = '\0';
	bufptr = nl + 1;

	if (!strcmp(type, blob_type)) {
		item->tagged = (struct object *)lookup_blob(&oid);
	} else if (!strcmp(type, tree_type)) {
		item->tagged = (struct object *)lookup_tree(&oid);
	} else if (!strcmp(type, commit_type)) {
		item->tagged = (struct object *)lookup_commit(&oid);
	} else if (!strcmp(type, tag_type)) {
		item->tagged = (struct object *)lookup_tag(&oid);
	} else {
		error("Unknown type %s", type);
		item->tagged = NULL;
	}

	if (bufptr + 4 < tail && starts_with(bufptr, "tag "))
		; 		/* good */
	else
		return -1;
	bufptr += 4;
	nl = memchr(bufptr, '\n', tail - bufptr);
	if (!nl)
		return -1;
	item->tag = xmemdupz(bufptr, nl - bufptr);
	bufptr = nl + 1;

	if (bufptr + 7 < tail && starts_with(bufptr, "tagger "))
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
	data = read_object_file(&item->object.oid, &type, &size);
	if (!data)
		return error("Could not read %s",
			     oid_to_hex(&item->object.oid));
	if (type != OBJ_TAG) {
		free(data);
		return error("Object %s not a tag",
			     oid_to_hex(&item->object.oid));
	}
	ret = parse_tag_buffer(item, data, size);
	free(data);
	return ret;
}
