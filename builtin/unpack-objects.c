#include "builtin.h"
#include "cache.h"
#include "object.h"
#include "delta.h"
#include "pack.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"
#include "tree-walk.h"
#include "progress.h"
#include "decorate.h"
#include "packv4-parse.h"
#include "fsck.h"

static int dry_run, quiet, recover, has_errors, strict;
static const char unpack_usage[] = "git unpack-objects [-n] [-q] [-r] [--strict] < pack-file";

/* We always read in 4kB chunks. */
static unsigned char buffer[4096];
static unsigned int offset, len;
static off_t consumed_bytes;
static git_SHA_CTX ctx;

static int packv4;
static unsigned nr_objects;
static unsigned char *sha1_table;
static struct packv4_dict *name_dict, *path_dict;

/*
 * When running under --strict mode, objects whose reachability are
 * suspect are kept in core without getting written in the object
 * store.
 */
struct obj_buffer {
	char *buffer;
	unsigned long size;
};

static struct decoration obj_decorate;

static struct obj_buffer *lookup_object_buffer(struct object *base)
{
	return lookup_decoration(&obj_decorate, base);
}

static void add_object_buffer(struct object *object, char *buffer, unsigned long size)
{
	struct obj_buffer *obj;
	obj = xcalloc(1, sizeof(struct obj_buffer));
	obj->buffer = buffer;
	obj->size = size;
	if (add_decoration(&obj_decorate, object, obj))
		die("object %s tried to add buffer twice!", sha1_to_hex(object->sha1));
}

static struct strbuf back_buffer = STRBUF_INIT;
static int save_to_back_buffer;

/*
 * Make sure at least "min" bytes are available in the buffer, and
 * return the pointer to the buffer.
 */
static void *fill(int min)
{
	if (min <= len)
		return buffer + offset;
	if (min > sizeof(buffer))
		die("cannot fill %d bytes", min);
	if (offset) {
		git_SHA1_Update(&ctx, buffer, offset);
		if (save_to_back_buffer)
			strbuf_add(&back_buffer, buffer, offset);
		memmove(buffer, buffer + offset, len);
		offset = 0;
	}
	do {
		ssize_t ret = xread(0, buffer + len, sizeof(buffer) - len);
		if (ret <= 0) {
			if (!ret)
				die("early EOF");
			die_errno("read error on input");
		}
		len += ret;
	} while (len < min);
	return buffer;
}

static void copy_back_buffer(int set)
{
	if (offset) {
		git_SHA1_Update(&ctx, buffer, offset);
		if (save_to_back_buffer)
			strbuf_add(&back_buffer, buffer, offset);
		memmove(buffer, buffer + offset, len);
		offset = 0;
	}
	save_to_back_buffer = set;
}

static void use(int bytes)
{
	if (bytes > len)
		die("used more bytes than were available");
	len -= bytes;
	offset += bytes;

	/* make sure off_t is sufficiently large not to wrap */
	if (signed_add_overflows(consumed_bytes, bytes))
		die("pack too large for current definition of off_t");
	consumed_bytes += bytes;
}

static inline void *fill_and_use(int bytes)
{
	void *p = fill(bytes);
	use(bytes);
	return p;
}

static uintmax_t read_varint(void)
{
	unsigned char c = *(char*)fill_and_use(1);
	uintmax_t val = c & 127;
	while (c & 128) {
		val += 1;
		if (!val || MSB(val, 7))
			die("offset overflow in read_varint at %lu",
			    (unsigned long)consumed_bytes);
		c = *(char*)fill_and_use(1);
		val = (val << 7) + (c & 127);
	}
	return val;
}

static const unsigned char *read_sha1ref(void)
{
	unsigned int index = read_varint();
	if (!index) {
		static unsigned char sha1[20];
		hashcpy(sha1, fill_and_use(20));
		return sha1;
	}
	index--;
	if (index >= nr_objects)
		die("bad index in read_sha1ref at %lu",
		    (unsigned long)consumed_bytes);
	return sha1_table + index * 20;
}

static const unsigned char *read_dictref(struct packv4_dict *dict)
{
	unsigned int index = read_varint();
	if (index >= dict->nb_entries)
		die("bad index in read_dictref at %lu",
		    (unsigned long)consumed_bytes);
	return  dict->data + dict->offsets[index];
}

static void *get_data(unsigned long size)
{
	git_zstream stream;
	void *buf = xmalloc(size);

	memset(&stream, 0, sizeof(stream));

	stream.next_out = buf;
	stream.avail_out = size;
	stream.next_in = fill(1);
	stream.avail_in = len;
	git_inflate_init(&stream);

	for (;;) {
		int ret = git_inflate(&stream, 0);
		use(len - stream.avail_in);
		if (stream.total_out == size && ret == Z_STREAM_END)
			break;
		if (ret != Z_OK) {
			error("inflate returned %d", ret);
			free(buf);
			buf = NULL;
			if (!recover)
				exit(1);
			has_errors = 1;
			break;
		}
		stream.next_in = fill(1);
		stream.avail_in = len;
	}
	git_inflate_end(&stream);
	return buf;
}

struct delta_info {
	unsigned char base_sha1[20];
	unsigned nr;
	off_t base_offset;
	unsigned long size;
	void *delta;
	struct delta_info *next;
};

static struct delta_info *delta_list;

static void add_delta_to_list(unsigned nr, unsigned const char *base_sha1,
			      off_t base_offset,
			      void *delta, unsigned long size)
{
	struct delta_info *info = xmalloc(sizeof(*info));

	hashcpy(info->base_sha1, base_sha1);
	info->base_offset = base_offset;
	info->size = size;
	info->delta = delta;
	info->nr = nr;
	info->next = delta_list;
	delta_list = info;
}

struct obj_info {
	off_t offset;
	unsigned char sha1[20];
	struct object *obj;
};

#define FLAG_OPEN (1u<<20)
#define FLAG_WRITTEN (1u<<21)

static struct obj_info *obj_list;

/*
 * Called only from check_object() after it verified this object
 * is Ok.
 */
static void write_cached_object(struct object *obj)
{
	unsigned char sha1[20];
	struct obj_buffer *obj_buf = lookup_object_buffer(obj);
	if (write_sha1_file(obj_buf->buffer, obj_buf->size, typename(obj->type), sha1) < 0)
		die("failed to write object %s", sha1_to_hex(obj->sha1));
	obj->flags |= FLAG_WRITTEN;
}

/*
 * At the very end of the processing, write_rest() scans the objects
 * that have reachability requirements and calls this function.
 * Verify its reachability and validity recursively and write it out.
 */
static int check_object(struct object *obj, int type, void *data)
{
	if (!obj)
		return 1;

	if (obj->flags & FLAG_WRITTEN)
		return 0;

	if (type != OBJ_ANY && obj->type != type)
		die("object type mismatch");

	if (!(obj->flags & FLAG_OPEN)) {
		unsigned long size;
		int type = sha1_object_info(obj->sha1, &size);
		if (type != obj->type || type <= 0)
			die("object %s of unexpected type", sha1_to_hex(obj->sha1));
		obj->flags |= FLAG_WRITTEN;
		return 0;
	}

	if (fsck_object(obj, 1, fsck_error_function))
		die("Error in object");
	if (fsck_walk(obj, check_object, NULL))
		die("Error on reachable objects of %s", sha1_to_hex(obj->sha1));
	write_cached_object(obj);
	return 0;
}

static void write_rest(void)
{
	unsigned i;
	for (i = 0; i < nr_objects; i++) {
		if (obj_list[i].obj)
			check_object(obj_list[i].obj, OBJ_ANY, NULL);
	}
}

static void added_object(unsigned nr, enum object_type type,
			 void *data, unsigned long size);

/*
 * Write out nr-th object from the list, now we know the contents
 * of it.  Under --strict, this buffers structured objects in-core,
 * to be checked at the end.
 */
static void write_object(unsigned nr, enum object_type type,
			 void *buf, unsigned long size)
{
	if (!strict) {
		if (write_sha1_file(buf, size, typename(type), obj_list[nr].sha1) < 0)
			die("failed to write object");
		added_object(nr, type, buf, size);
		free(buf);
		obj_list[nr].obj = NULL;
	} else if (type == OBJ_BLOB) {
		struct blob *blob;
		if (write_sha1_file(buf, size, typename(type), obj_list[nr].sha1) < 0)
			die("failed to write object");
		added_object(nr, type, buf, size);
		free(buf);

		blob = lookup_blob(obj_list[nr].sha1);
		if (blob)
			blob->object.flags |= FLAG_WRITTEN;
		else
			die("invalid blob object");
		obj_list[nr].obj = NULL;
	} else {
		struct object *obj;
		int eaten;
		hash_sha1_file(buf, size, typename(type), obj_list[nr].sha1);
		added_object(nr, type, buf, size);
		obj = parse_object_buffer(obj_list[nr].sha1, type, size, buf, &eaten);
		if (!obj)
			die("invalid %s", typename(type));
		add_object_buffer(obj, buf, size);
		obj->flags |= FLAG_OPEN;
		obj_list[nr].obj = obj;
	}
}

static void resolve_delta(unsigned nr, enum object_type type,
			  void *base, unsigned long base_size,
			  void *delta, unsigned long delta_size)
{
	void *result;
	unsigned long result_size;

	result = patch_delta(base, base_size,
			     delta, delta_size,
			     &result_size);
	if (!result)
		die("failed to apply delta");
	free(delta);
	write_object(nr, type, result, result_size);
}

/*
 * We now know the contents of an object (which is nr-th in the pack);
 * resolve all the deltified objects that are based on it.
 */
static void added_object(unsigned nr, enum object_type type,
			 void *data, unsigned long size)
{
	struct delta_info **p = &delta_list;
	struct delta_info *info;

	while ((info = *p) != NULL) {
		if (!hashcmp(info->base_sha1, obj_list[nr].sha1) ||
		    info->base_offset == obj_list[nr].offset) {
			*p = info->next;
			p = &delta_list;
			resolve_delta(info->nr, type, data, size,
				      info->delta, info->size);
			free(info);
			continue;
		}
		p = &info->next;
	}
}

static void unpack_non_delta_entry(enum object_type type, unsigned long size,
				   unsigned nr)
{
	void *buf = get_data(size);

	if (!dry_run && buf)
		write_object(nr, type, buf, size);
	else
		free(buf);
}

static int resolve_against_held(unsigned nr, const unsigned char *base,
				void *delta_data, unsigned long delta_size)
{
	struct object *obj;
	struct obj_buffer *obj_buffer;
	obj = lookup_object(base);
	if (!obj)
		return 0;
	obj_buffer = lookup_object_buffer(obj);
	if (!obj_buffer)
		return 0;
	resolve_delta(nr, obj->type, obj_buffer->buffer,
		      obj_buffer->size, delta_data, delta_size);
	return 1;
}

static void unpack_delta_entry(enum object_type type, unsigned long delta_size,
			       unsigned nr)
{
	void *delta_data, *base;
	unsigned long base_size;
	unsigned char base_sha1[20];

	if (type == OBJ_REF_DELTA) {
		if (packv4)
			hashcpy(base_sha1, read_sha1ref());
		else {
			hashcpy(base_sha1, fill(20));
			use(20);
		}
		delta_data = get_data(delta_size);
		if (dry_run || !delta_data) {
			free(delta_data);
			return;
		}
		if (has_sha1_file(base_sha1))
			; /* Ok we have this one */
		else if (resolve_against_held(nr, base_sha1,
					      delta_data, delta_size))
			return; /* we are done */
		else {
			/* cannot resolve yet --- queue it */
			hashcpy(obj_list[nr].sha1, null_sha1);
			add_delta_to_list(nr, base_sha1, 0, delta_data, delta_size);
			return;
		}
	} else {
		unsigned base_found = 0;
		unsigned char *pack, c;
		off_t base_offset;
		unsigned lo, mid, hi;

		pack = fill(1);
		c = *pack;
		use(1);
		base_offset = c & 127;
		while (c & 128) {
			base_offset += 1;
			if (!base_offset || MSB(base_offset, 7))
				die("offset value overflow for delta base object");
			pack = fill(1);
			c = *pack;
			use(1);
			base_offset = (base_offset << 7) + (c & 127);
		}
		base_offset = obj_list[nr].offset - base_offset;
		if (base_offset <= 0 || base_offset >= obj_list[nr].offset)
			die("offset value out of bound for delta base object");

		delta_data = get_data(delta_size);
		if (dry_run || !delta_data) {
			free(delta_data);
			return;
		}
		lo = 0;
		hi = nr;
		while (lo < hi) {
			mid = (lo + hi)/2;
			if (base_offset < obj_list[mid].offset) {
				hi = mid;
			} else if (base_offset > obj_list[mid].offset) {
				lo = mid + 1;
			} else {
				hashcpy(base_sha1, obj_list[mid].sha1);
				base_found = !is_null_sha1(base_sha1);
				break;
			}
		}
		if (!base_found) {
			/*
			 * The delta base object is itself a delta that
			 * has not been resolved yet.
			 */
			hashcpy(obj_list[nr].sha1, null_sha1);
			add_delta_to_list(nr, null_sha1, base_offset, delta_data, delta_size);
			return;
		}
	}

	if (resolve_against_held(nr, base_sha1, delta_data, delta_size))
		return;

	base = read_sha1_file(base_sha1, &type, &base_size);
	if (!base) {
		error("failed to read delta-pack base object %s",
		      sha1_to_hex(base_sha1));
		if (!recover)
			exit(1);
		has_errors = 1;
		return;
	}
	resolve_delta(nr, type, base, base_size, delta_data, delta_size);
	free(base);
}

static void unpack_commit_v4(unsigned long size, unsigned long nr)
{
	unsigned int nb_parents;
	const unsigned char *committer, *author, *ident;
	unsigned long author_time, committer_time;
	int16_t committer_tz, author_tz;
	struct strbuf dst;
	char *remaining;

	strbuf_init(&dst, size);

	strbuf_addf(&dst, "tree %s\n", sha1_to_hex(read_sha1ref()));
	nb_parents = read_varint();
	while (nb_parents--)
		strbuf_addf(&dst, "parent %s\n", sha1_to_hex(read_sha1ref()));

	committer_time = read_varint();
	ident = read_dictref(name_dict);
	committer_tz = (ident[0] << 8) | ident[1];
	committer = ident + 2;

	author_time = read_varint();
	ident = read_dictref(name_dict);
	author_tz = (ident[0] << 8) | ident[1];
	author = ident + 2;

	if (author_time & 1)
		author_time = committer_time + (author_time >> 1);
	else
		author_time = committer_time - (author_time >> 1);

	strbuf_addf(&dst,
		    "author %s %lu %+05d\n"
		    "committer %s %lu %+05d\n",
		    author, author_time, author_tz,
		    committer, committer_time, committer_tz);

	if (dst.len > size)
		die("bad commit");

	remaining = get_data(size - dst.len);
	strbuf_add(&dst, remaining, size - dst.len);
	if (!dry_run)
		write_object(nr, OBJ_COMMIT, dst.buf, dst.len);
	else
		strbuf_release(&dst);
}

static void read_typesize_v2(enum object_type *type, unsigned long *size)
{
	unsigned char c = *(char*)fill_and_use(1);
	unsigned shift;

	*type = (c >> 4) & 7;
	*size = (c & 15);
	shift = 4;
	while (c & 128) {
		c = *(char*)fill_and_use(1);
		*size += (c & 0x7f) << shift;
		shift += 7;
	}
}

static int unpack_one(unsigned nr)
{
	unsigned long size;
	enum object_type type;

	obj_list[nr].offset = consumed_bytes;

	if (packv4 && *(char*)fill(1) == 0) {
		use(1);
		return -1;
	}
	if (packv4) {
		uintmax_t val = read_varint();
		type = val & 15;
		size = val >> 4;
	} else
		read_typesize_v2(&type, &size);

	switch (type) {
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		unpack_non_delta_entry(type, size, nr);
		break;
	case OBJ_REF_DELTA:
	case OBJ_OFS_DELTA:
		unpack_delta_entry(type, size, nr);
		break;
	case OBJ_PV4_COMMIT:
		unpack_commit_v4(size, nr);
		break;
	default:
		error("bad object type %d", type);
		has_errors = 1;
		if (recover)
			break;
		exit(1);
	}
	return 0;
}

static struct packv4_dict *read_dict(void)
{
	unsigned long size;
	unsigned char *data;
	struct packv4_dict *dict;

	size = read_varint();
	data = get_data(size);
	dict = pv4_create_dict(data, size);
	if (!dict)
		die("unable to parse dictionary");
	return dict;
}

static void unpack_all(void)
{
	int i;
	struct progress *progress = NULL;
	struct pack_header *hdr = fill(sizeof(struct pack_header));

	nr_objects = ntohl(hdr->hdr_entries);

	if (ntohl(hdr->hdr_signature) != PACK_SIGNATURE)
		die("bad pack file");
	if (!pack_version_ok(hdr->hdr_version))
		die("unknown pack file version %"PRIu32,
			ntohl(hdr->hdr_version));
	packv4 = ntohl(hdr->hdr_version) == 4;
	use(sizeof(struct pack_header));

	if (packv4) {
		sha1_table = xmalloc(20 * nr_objects);
		for (i = 0; i < nr_objects; i++) {
			unsigned char *p = sha1_table + i * 20;
			hashcpy(p, fill_and_use(20));
		}
		name_dict = read_dict();
		path_dict = read_dict();
	}

	if (!quiet)
		progress = start_progress("Unpacking objects", nr_objects);
	obj_list = xcalloc(nr_objects, sizeof(*obj_list));
	for (i = 0; i < nr_objects; i++) {
		if (unpack_one(i))
			break;
		display_progress(progress, i + 1);
	}
	stop_progress(&progress);

	if (delta_list)
		die("unresolved deltas left after unpacking");
}

int cmd_unpack_objects(int argc, const char **argv, const char *prefix)
{
	int i;
	unsigned char sha1[20];

	read_replace_refs = 0;

	git_config(git_default_config, NULL);

	quiet = !isatty(2);

	for (i = 1 ; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp(arg, "-n")) {
				dry_run = 1;
				continue;
			}
			if (!strcmp(arg, "-q")) {
				quiet = 1;
				continue;
			}
			if (!strcmp(arg, "-r")) {
				recover = 1;
				continue;
			}
			if (!strcmp(arg, "--strict")) {
				strict = 1;
				continue;
			}
			if (!prefixcmp(arg, "--pack_header=")) {
				struct pack_header *hdr;
				char *c;

				hdr = (struct pack_header *)buffer;
				hdr->hdr_signature = htonl(PACK_SIGNATURE);
				hdr->hdr_version = htonl(strtoul(arg + 14, &c, 10));
				if (*c != ',')
					die("bad %s", arg);
				hdr->hdr_entries = htonl(strtoul(c + 1, &c, 10));
				if (*c)
					die("bad %s", arg);
				len = sizeof(*hdr);
				continue;
			}
			usage(unpack_usage);
		}

		/* We don't take any non-flag arguments now.. Maybe some day */
		usage(unpack_usage);
	}
	git_SHA1_Init(&ctx);
	unpack_all();
	git_SHA1_Update(&ctx, buffer, offset);
	git_SHA1_Final(sha1, &ctx);
	if (strict)
		write_rest();
	if (hashcmp(fill(20), sha1))
		die("final sha1 did not match");
	use(20);

	/* Write the last part of the buffer to stdout */
	while (len) {
		int ret = xwrite(1, buffer + offset, len);
		if (ret <= 0)
			break;
		len -= ret;
		offset += ret;
	}

	/* All done */
	return has_errors;
}
