#include "builtin.h"
#include "bulk-checkin.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "git-zlib.h"
#include "hex.h"
#include "object-store-ll.h"
#include "object.h"
#include "delta.h"
#include "pack.h"
#include "blob.h"
#include "replace-object.h"
#include "strbuf.h"
#include "progress.h"
#include "decorate.h"
#include "fsck.h"

static int dry_run, quiet, recover, has_errors, strict;
static const char unpack_usage[] = "git unpack-objects [-n] [-q] [-r] [--strict]";

/* We always read in 4kB chunks. */
static unsigned char buffer[4096];
static unsigned int offset, len;
static off_t consumed_bytes;
static off_t max_input_size;
static git_hash_ctx ctx;
static struct fsck_options fsck_options = FSCK_OPTIONS_STRICT;
static struct progress *progress;

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
	CALLOC_ARRAY(obj, 1);
	obj->buffer = buffer;
	obj->size = size;
	if (add_decoration(&obj_decorate, object, obj))
		die("object %s tried to add buffer twice!", oid_to_hex(&object->oid));
}

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
		the_hash_algo->update_fn(&ctx, buffer, offset);
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
	if (max_input_size && consumed_bytes > max_input_size)
		die(_("pack exceeds maximum allowed size"));
	display_throughput(progress, consumed_bytes);
}

/*
 * Decompress zstream from the standard input into a newly
 * allocated buffer of specified size and return the buffer.
 * The caller is responsible to free the returned buffer.
 *
 * But for dry_run mode, "get_data()" is only used to check the
 * integrity of data, and the returned buffer is not used at all.
 * Therefore, in dry_run mode, "get_data()" will release the small
 * allocated buffer which is reused to hold temporary zstream output
 * and return NULL instead of returning garbage data.
 */
static void *get_data(unsigned long size)
{
	git_zstream stream;
	unsigned long bufsize = dry_run && size > 8192 ? 8192 : size;
	void *buf = xmallocz(bufsize);

	memset(&stream, 0, sizeof(stream));

	stream.next_out = buf;
	stream.avail_out = bufsize;
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
			FREE_AND_NULL(buf);
			if (!recover)
				exit(1);
			has_errors = 1;
			break;
		}
		stream.next_in = fill(1);
		stream.avail_in = len;
		if (dry_run) {
			/* reuse the buffer in dry_run mode */
			stream.next_out = buf;
			stream.avail_out = bufsize > size - stream.total_out ?
						   size - stream.total_out :
						   bufsize;
		}
	}
	git_inflate_end(&stream);
	if (dry_run)
		FREE_AND_NULL(buf);
	return buf;
}

struct delta_info {
	struct object_id base_oid;
	unsigned nr;
	off_t base_offset;
	unsigned long size;
	void *delta;
	struct delta_info *next;
};

static struct delta_info *delta_list;

static void add_delta_to_list(unsigned nr, const struct object_id *base_oid,
			      off_t base_offset,
			      void *delta, unsigned long size)
{
	struct delta_info *info = xmalloc(sizeof(*info));

	oidcpy(&info->base_oid, base_oid);
	info->base_offset = base_offset;
	info->size = size;
	info->delta = delta;
	info->nr = nr;
	info->next = delta_list;
	delta_list = info;
}

struct obj_info {
	off_t offset;
	struct object_id oid;
	struct object *obj;
};

/* Remember to update object flag allocation in object.h */
#define FLAG_OPEN (1u<<20)
#define FLAG_WRITTEN (1u<<21)

static struct obj_info *obj_list;
static unsigned nr_objects;

/*
 * Called only from check_object() after it verified this object
 * is Ok.
 */
static void write_cached_object(struct object *obj, struct obj_buffer *obj_buf)
{
	struct object_id oid;

	if (write_object_file(obj_buf->buffer, obj_buf->size,
			      obj->type, &oid) < 0)
		die("failed to write object %s", oid_to_hex(&obj->oid));
	obj->flags |= FLAG_WRITTEN;
}

/*
 * At the very end of the processing, write_rest() scans the objects
 * that have reachability requirements and calls this function.
 * Verify its reachability and validity recursively and write it out.
 */
static int check_object(struct object *obj, enum object_type type,
			void *data UNUSED,
			struct fsck_options *options UNUSED)
{
	struct obj_buffer *obj_buf;

	if (!obj)
		return 1;

	if (obj->flags & FLAG_WRITTEN)
		return 0;

	if (type != OBJ_ANY && obj->type != type)
		die("object type mismatch");

	if (!(obj->flags & FLAG_OPEN)) {
		unsigned long size;
		int type = oid_object_info(the_repository, &obj->oid, &size);
		if (type != obj->type || type <= 0)
			die("object of unexpected type");
		obj->flags |= FLAG_WRITTEN;
		return 0;
	}

	obj_buf = lookup_object_buffer(obj);
	if (!obj_buf)
		die("Whoops! Cannot find object '%s'", oid_to_hex(&obj->oid));
	if (fsck_object(obj, obj_buf->buffer, obj_buf->size, &fsck_options))
		die("fsck error in packed object");
	fsck_options.walk = check_object;
	if (fsck_walk(obj, NULL, &fsck_options))
		die("Error on reachable objects of %s", oid_to_hex(&obj->oid));
	write_cached_object(obj, obj_buf);
	return 0;
}

static void write_rest(void)
{
	unsigned i;
	for (i = 0; i < nr_objects; i++) {
		if (obj_list[i].obj)
			check_object(obj_list[i].obj, OBJ_ANY, NULL, NULL);
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
		if (write_object_file(buf, size, type,
				      &obj_list[nr].oid) < 0)
			die("failed to write object");
		added_object(nr, type, buf, size);
		free(buf);
		obj_list[nr].obj = NULL;
	} else if (type == OBJ_BLOB) {
		struct blob *blob;
		if (write_object_file(buf, size, type,
				      &obj_list[nr].oid) < 0)
			die("failed to write object");
		added_object(nr, type, buf, size);
		free(buf);

		blob = lookup_blob(the_repository, &obj_list[nr].oid);
		if (blob)
			blob->object.flags |= FLAG_WRITTEN;
		else
			die("invalid blob object");
		obj_list[nr].obj = NULL;
	} else {
		struct object *obj;
		int eaten;
		hash_object_file(the_hash_algo, buf, size, type,
				 &obj_list[nr].oid);
		added_object(nr, type, buf, size);
		obj = parse_object_buffer(the_repository, &obj_list[nr].oid,
					  type, size, buf,
					  &eaten);
		if (!obj)
			die("invalid %s", type_name(type));
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
		if (oideq(&info->base_oid, &obj_list[nr].oid) ||
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

	if (buf)
		write_object(nr, type, buf, size);
}

struct input_zstream_data {
	git_zstream *zstream;
	unsigned char buf[8192];
	int status;
};

static const void *feed_input_zstream(struct input_stream *in_stream,
				      unsigned long *readlen)
{
	struct input_zstream_data *data = in_stream->data;
	git_zstream *zstream = data->zstream;
	void *in = fill(1);

	if (in_stream->is_finished) {
		*readlen = 0;
		return NULL;
	}

	zstream->next_out = data->buf;
	zstream->avail_out = sizeof(data->buf);
	zstream->next_in = in;
	zstream->avail_in = len;

	data->status = git_inflate(zstream, 0);

	in_stream->is_finished = data->status != Z_OK;
	use(len - zstream->avail_in);
	*readlen = sizeof(data->buf) - zstream->avail_out;

	return data->buf;
}

static void stream_blob(unsigned long size, unsigned nr)
{
	git_zstream zstream = { 0 };
	struct input_zstream_data data = { 0 };
	struct input_stream in_stream = {
		.read = feed_input_zstream,
		.data = &data,
	};
	struct obj_info *info = &obj_list[nr];

	data.zstream = &zstream;
	git_inflate_init(&zstream);

	if (stream_loose_object(&in_stream, size, &info->oid))
		die(_("failed to write object in stream"));

	if (data.status != Z_STREAM_END)
		die(_("inflate returned (%d)"), data.status);
	git_inflate_end(&zstream);

	if (strict) {
		struct blob *blob = lookup_blob(the_repository, &info->oid);

		if (!blob)
			die(_("invalid blob object from stream"));
		blob->object.flags |= FLAG_WRITTEN;
	}
	info->obj = NULL;
}

static int resolve_against_held(unsigned nr, const struct object_id *base,
				void *delta_data, unsigned long delta_size)
{
	struct object *obj;
	struct obj_buffer *obj_buffer;
	obj = lookup_object(the_repository, base);
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
	struct object_id base_oid;

	if (type == OBJ_REF_DELTA) {
		oidread(&base_oid, fill(the_hash_algo->rawsz), the_repository->hash_algo);
		use(the_hash_algo->rawsz);
		delta_data = get_data(delta_size);
		if (!delta_data)
			return;
		if (repo_has_object_file(the_repository, &base_oid))
			; /* Ok we have this one */
		else if (resolve_against_held(nr, &base_oid,
					      delta_data, delta_size))
			return; /* we are done */
		else {
			/* cannot resolve yet --- queue it */
			oidclr(&obj_list[nr].oid, the_repository->hash_algo);
			add_delta_to_list(nr, &base_oid, 0, delta_data, delta_size);
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
		if (!delta_data)
			return;
		lo = 0;
		hi = nr;
		while (lo < hi) {
			mid = lo + (hi - lo) / 2;
			if (base_offset < obj_list[mid].offset) {
				hi = mid;
			} else if (base_offset > obj_list[mid].offset) {
				lo = mid + 1;
			} else {
				oidcpy(&base_oid, &obj_list[mid].oid);
				base_found = !is_null_oid(&base_oid);
				break;
			}
		}
		if (!base_found) {
			/*
			 * The delta base object is itself a delta that
			 * has not been resolved yet.
			 */
			oidclr(&obj_list[nr].oid, the_repository->hash_algo);
			add_delta_to_list(nr, null_oid(), base_offset,
					  delta_data, delta_size);
			return;
		}
	}

	if (resolve_against_held(nr, &base_oid, delta_data, delta_size))
		return;

	base = repo_read_object_file(the_repository, &base_oid, &type,
				     &base_size);
	if (!base) {
		error("failed to read delta-pack base object %s",
		      oid_to_hex(&base_oid));
		if (!recover)
			exit(1);
		has_errors = 1;
		return;
	}
	resolve_delta(nr, type, base, base_size, delta_data, delta_size);
	free(base);
}

static void unpack_one(unsigned nr)
{
	unsigned shift;
	unsigned char *pack;
	unsigned long size, c;
	enum object_type type;

	obj_list[nr].offset = consumed_bytes;

	pack = fill(1);
	c = *pack;
	use(1);
	type = (c >> 4) & 7;
	size = (c & 15);
	shift = 4;
	while (c & 0x80) {
		pack = fill(1);
		c = *pack;
		use(1);
		size += (c & 0x7f) << shift;
		shift += 7;
	}

	switch (type) {
	case OBJ_BLOB:
		if (!dry_run && size > big_file_threshold) {
			stream_blob(size, nr);
			return;
		}
		/* fallthrough */
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_TAG:
		unpack_non_delta_entry(type, size, nr);
		return;
	case OBJ_REF_DELTA:
	case OBJ_OFS_DELTA:
		unpack_delta_entry(type, size, nr);
		return;
	default:
		error("bad object type %d", type);
		has_errors = 1;
		if (recover)
			return;
		exit(1);
	}
}

static void unpack_all(void)
{
	int i;
	struct pack_header *hdr = fill(sizeof(struct pack_header));

	nr_objects = ntohl(hdr->hdr_entries);

	if (ntohl(hdr->hdr_signature) != PACK_SIGNATURE)
		die("bad pack file");
	if (!pack_version_ok(hdr->hdr_version))
		die("unknown pack file version %"PRIu32,
			ntohl(hdr->hdr_version));
	use(sizeof(struct pack_header));

	if (!quiet)
		progress = start_progress(_("Unpacking objects"), nr_objects);
	CALLOC_ARRAY(obj_list, nr_objects);
	begin_odb_transaction();
	for (i = 0; i < nr_objects; i++) {
		unpack_one(i);
		display_progress(progress, i + 1);
	}
	end_odb_transaction();
	stop_progress(&progress);

	if (delta_list)
		die("unresolved deltas left after unpacking");
}

int cmd_unpack_objects(int argc, const char **argv, const char *prefix UNUSED)
{
	int i;
	struct object_id oid;
	git_hash_ctx tmp_ctx;

	disable_replace_refs();

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
			if (skip_prefix(arg, "--strict=", &arg)) {
				strict = 1;
				fsck_set_msg_types(&fsck_options, arg);
				continue;
			}
			if (starts_with(arg, "--pack_header=")) {
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
			if (skip_prefix(arg, "--max-input-size=", &arg)) {
				max_input_size = strtoumax(arg, NULL, 10);
				continue;
			}
			usage(unpack_usage);
		}

		/* We don't take any non-flag arguments now.. Maybe some day */
		usage(unpack_usage);
	}
	the_hash_algo->init_fn(&ctx);
	unpack_all();
	the_hash_algo->update_fn(&ctx, buffer, offset);
	the_hash_algo->init_fn(&tmp_ctx);
	the_hash_algo->clone_fn(&tmp_ctx, &ctx);
	the_hash_algo->final_oid_fn(&oid, &tmp_ctx);
	if (strict) {
		write_rest();
		if (fsck_finish(&fsck_options))
			die(_("fsck error in pack objects"));
	}
	if (!hasheq(fill(the_hash_algo->rawsz), oid.hash,
		    the_repository->hash_algo))
		die("final sha1 did not match");
	use(the_hash_algo->rawsz);

	/* Write the last part of the buffer to stdout */
	write_in_full(1, buffer + offset, len);

	/* All done */
	return has_errors;
}
