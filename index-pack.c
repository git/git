#include "cache.h"
#include "delta.h"
#include "pack.h"
#include "csum-file.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"

static const char index_pack_usage[] =
"git-index-pack [-o index-file] pack-file";

struct object_entry
{
	unsigned long offset;
	unsigned long size;
	unsigned int hdr_size;
	enum object_type type;
	enum object_type real_type;
	unsigned char sha1[20];
};

union delta_base {
	unsigned char sha1[20];
	unsigned long offset;
};

/*
 * Even if sizeof(union delta_base) == 24 on 64-bit archs, we really want
 * to memcmp() only the first 20 bytes.
 */
#define UNION_BASE_SZ	20

struct delta_entry
{
	struct object_entry *obj;
	union delta_base base;
};

static const char *pack_name;
static struct object_entry *objects;
static struct delta_entry *deltas;
static int nr_objects;
static int nr_deltas;

/* We always read in 4kB chunks. */
static unsigned char input_buffer[4096];
static unsigned long input_offset, input_len, consumed_bytes;
static SHA_CTX input_ctx;
static int input_fd;

/*
 * Make sure at least "min" bytes are available in the buffer, and
 * return the pointer to the buffer.
 */
static void * fill(int min)
{
	if (min <= input_len)
		return input_buffer + input_offset;
	if (min > sizeof(input_buffer))
		die("cannot fill %d bytes", min);
	if (input_offset) {
		SHA1_Update(&input_ctx, input_buffer, input_offset);
		memcpy(input_buffer, input_buffer + input_offset, input_len);
		input_offset = 0;
	}
	do {
		int ret = xread(input_fd, input_buffer + input_len,
				sizeof(input_buffer) - input_len);
		if (ret <= 0) {
			if (!ret)
				die("early EOF");
			die("read error on input: %s", strerror(errno));
		}
		input_len += ret;
	} while (input_len < min);
	return input_buffer;
}

static void use(int bytes)
{
	if (bytes > input_len)
		die("used more bytes than were available");
	input_len -= bytes;
	input_offset += bytes;
	consumed_bytes += bytes;
}

static void open_pack_file(void)
{
	input_fd = open(pack_name, O_RDONLY);
	if (input_fd < 0)
		die("cannot open packfile '%s': %s", pack_name,
		    strerror(errno));
	SHA1_Init(&input_ctx);
}

static void parse_pack_header(void)
{
	struct pack_header *hdr = fill(sizeof(struct pack_header));

	/* Header consistency check */
	if (hdr->hdr_signature != htonl(PACK_SIGNATURE))
		die("packfile '%s' signature mismatch", pack_name);
	if (!pack_version_ok(hdr->hdr_version))
		die("packfile '%s' version %d unsupported",
		    pack_name, ntohl(hdr->hdr_version));

	nr_objects = ntohl(hdr->hdr_entries);
	use(sizeof(struct pack_header));
	/*fprintf(stderr, "Indexing %d objects\n", nr_objects);*/
}

static void bad_object(unsigned long offset, const char *format,
		       ...) NORETURN __attribute__((format (printf, 2, 3)));

static void bad_object(unsigned long offset, const char *format, ...)
{
	va_list params;
	char buf[1024];

	va_start(params, format);
	vsnprintf(buf, sizeof(buf), format, params);
	va_end(params);
	die("packfile '%s': bad object at offset %lu: %s",
	    pack_name, offset, buf);
}

static void *unpack_entry_data(unsigned long offset, unsigned long size)
{
	z_stream stream;
	void *buf = xmalloc(size);

	memset(&stream, 0, sizeof(stream));
	stream.next_out = buf;
	stream.avail_out = size;
	stream.next_in = fill(1);
	stream.avail_in = input_len;
	inflateInit(&stream);

	for (;;) {
		int ret = inflate(&stream, 0);
		use(input_len - stream.avail_in);
		if (stream.total_out == size && ret == Z_STREAM_END)
			break;
		if (ret != Z_OK)
			bad_object(offset, "inflate returned %d", ret);
		stream.next_in = fill(1);
		stream.avail_in = input_len;
	}
	inflateEnd(&stream);
	return buf;
}

static void *unpack_raw_entry(struct object_entry *obj, union delta_base *delta_base)
{
	unsigned char *p, c;
	unsigned long size, base_offset;
	unsigned shift;

	obj->offset = consumed_bytes;

	p = fill(1);
	c = *p;
	use(1);
	obj->type = (c >> 4) & 7;
	size = (c & 15);
	shift = 4;
	while (c & 0x80) {
		p = fill(1);
		c = *p;
		use(1);
		size += (c & 0x7fUL) << shift;
		shift += 7;
	}
	obj->size = size;

	switch (obj->type) {
	case OBJ_REF_DELTA:
		hashcpy(delta_base->sha1, fill(20));
		use(20);
		break;
	case OBJ_OFS_DELTA:
		memset(delta_base, 0, sizeof(*delta_base));
		p = fill(1);
		c = *p;
		use(1);
		base_offset = c & 127;
		while (c & 128) {
			base_offset += 1;
			if (!base_offset || base_offset & ~(~0UL >> 7))
				bad_object(obj->offset, "offset value overflow for delta base object");
			p = fill(1);
			c = *p;
			use(1);
			base_offset = (base_offset << 7) + (c & 127);
		}
		delta_base->offset = obj->offset - base_offset;
		if (delta_base->offset >= obj->offset)
			bad_object(obj->offset, "delta base offset is out of bound");
		break;
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		break;
	default:
		bad_object(obj->offset, "bad object type %d", obj->type);
	}
	obj->hdr_size = consumed_bytes - obj->offset;

	return unpack_entry_data(obj->offset, obj->size);
}

static void * get_data_from_pack(struct object_entry *obj)
{
	unsigned long from = obj[0].offset + obj[0].hdr_size;
	unsigned long len = obj[1].offset - from;
	unsigned pg_offset = from % getpagesize();
	unsigned char *map, *data;
	z_stream stream;
	int st;

	map = mmap(NULL, len + pg_offset, PROT_READ, MAP_PRIVATE,
		   input_fd, from - pg_offset);
	if (map == MAP_FAILED)
		die("cannot mmap packfile '%s': %s", pack_name, strerror(errno));
	data = xmalloc(obj->size);
	memset(&stream, 0, sizeof(stream));
	stream.next_out = data;
	stream.avail_out = obj->size;
	stream.next_in = map + pg_offset;
	stream.avail_in = len;
	inflateInit(&stream);
	while ((st = inflate(&stream, Z_FINISH)) == Z_OK);
	inflateEnd(&stream);
	if (st != Z_STREAM_END || stream.total_out != obj->size)
		die("serious inflate inconsistency");
	munmap(map, len + pg_offset);
	return data;
}

static int find_delta(const union delta_base *base)
{
	int first = 0, last = nr_deltas;

        while (first < last) {
                int next = (first + last) / 2;
                struct delta_entry *delta = &deltas[next];
                int cmp;

                cmp = memcmp(base, &delta->base, UNION_BASE_SZ);
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

static int find_delta_childs(const union delta_base *base,
			     int *first_index, int *last_index)
{
	int first = find_delta(base);
	int last = first;
	int end = nr_deltas - 1;

	if (first < 0)
		return -1;
	while (first > 0 && !memcmp(&deltas[first - 1].base, base, UNION_BASE_SZ))
		--first;
	while (last < end && !memcmp(&deltas[last + 1].base, base, UNION_BASE_SZ))
		++last;
	*first_index = first;
	*last_index = last;
	return 0;
}

static void sha1_object(const void *data, unsigned long size,
			enum object_type type, unsigned char *sha1)
{
	SHA_CTX ctx;
	char header[50];
	int header_size;
	const char *type_str;

	switch (type) {
	case OBJ_COMMIT: type_str = commit_type; break;
	case OBJ_TREE:   type_str = tree_type; break;
	case OBJ_BLOB:   type_str = blob_type; break;
	case OBJ_TAG:    type_str = tag_type; break;
	default:
		die("bad type %d", type);
	}

	header_size = sprintf(header, "%s %lu", type_str, size) + 1;

	SHA1_Init(&ctx);
	SHA1_Update(&ctx, header, header_size);
	SHA1_Update(&ctx, data, size);
	SHA1_Final(sha1, &ctx);
}

static void resolve_delta(struct delta_entry *delta, void *base_data,
			  unsigned long base_size, enum object_type type)
{
	struct object_entry *obj = delta->obj;
	void *delta_data;
	unsigned long delta_size;
	void *result;
	unsigned long result_size;
	union delta_base delta_base;
	int j, first, last;

	obj->real_type = type;
	delta_data = get_data_from_pack(obj);
	delta_size = obj->size;
	result = patch_delta(base_data, base_size, delta_data, delta_size,
			     &result_size);
	free(delta_data);
	if (!result)
		bad_object(obj->offset, "failed to apply delta");
	sha1_object(result, result_size, type, obj->sha1);

	hashcpy(delta_base.sha1, obj->sha1);
	if (!find_delta_childs(&delta_base, &first, &last)) {
		for (j = first; j <= last; j++)
			if (deltas[j].obj->type == OBJ_REF_DELTA)
				resolve_delta(&deltas[j], result, result_size, type);
	}

	memset(&delta_base, 0, sizeof(delta_base));
	delta_base.offset = obj->offset;
	if (!find_delta_childs(&delta_base, &first, &last)) {
		for (j = first; j <= last; j++)
			if (deltas[j].obj->type == OBJ_OFS_DELTA)
				resolve_delta(&deltas[j], result, result_size, type);
	}

	free(result);
}

static int compare_delta_entry(const void *a, const void *b)
{
	const struct delta_entry *delta_a = a;
	const struct delta_entry *delta_b = b;
	return memcmp(&delta_a->base, &delta_b->base, UNION_BASE_SZ);
}

/* Parse all objects and return the pack content SHA1 hash */
static void parse_pack_objects(unsigned char *sha1)
{
	int i;
	struct delta_entry *delta = deltas;
	void *data;
	struct stat st;

	/*
	 * First pass:
	 * - find locations of all objects;
	 * - calculate SHA1 of all non-delta objects;
	 * - remember base SHA1 for all deltas.
	 */
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];
		data = unpack_raw_entry(obj, &delta->base);
		obj->real_type = obj->type;
		if (obj->type == OBJ_REF_DELTA || obj->type == OBJ_OFS_DELTA) {
			nr_deltas++;
			delta->obj = obj;
			delta++;
		} else
			sha1_object(data, obj->size, obj->type, obj->sha1);
		free(data);
	}
	objects[i].offset = consumed_bytes;

	/* Check pack integrity */
	SHA1_Update(&input_ctx, input_buffer, input_offset);
	SHA1_Final(sha1, &input_ctx);
	if (hashcmp(fill(20), sha1))
		die("packfile '%s' SHA1 mismatch", pack_name);
	use(20);

	/* If input_fd is a file, we should have reached its end now. */
	if (fstat(input_fd, &st))
		die("cannot fstat packfile '%s': %s", pack_name, strerror(errno));
	if (S_ISREG(st.st_mode) && st.st_size != consumed_bytes)
		die("packfile '%s' has junk at the end", pack_name);

	/* Sort deltas by base SHA1/offset for fast searching */
	qsort(deltas, nr_deltas, sizeof(struct delta_entry),
	      compare_delta_entry);

	/*
	 * Second pass:
	 * - for all non-delta objects, look if it is used as a base for
	 *   deltas;
	 * - if used as a base, uncompress the object and apply all deltas,
	 *   recursively checking if the resulting object is used as a base
	 *   for some more deltas.
	 */
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];
		union delta_base base;
		int j, ref, ref_first, ref_last, ofs, ofs_first, ofs_last;

		if (obj->type == OBJ_REF_DELTA || obj->type == OBJ_OFS_DELTA)
			continue;
		hashcpy(base.sha1, obj->sha1);
		ref = !find_delta_childs(&base, &ref_first, &ref_last);
		memset(&base, 0, sizeof(base));
		base.offset = obj->offset;
		ofs = !find_delta_childs(&base, &ofs_first, &ofs_last);
		if (!ref && !ofs)
			continue;
		data = get_data_from_pack(obj);
		if (ref)
			for (j = ref_first; j <= ref_last; j++)
				if (deltas[j].obj->type == OBJ_REF_DELTA)
					resolve_delta(&deltas[j], data,
						      obj->size, obj->type);
		if (ofs)
			for (j = ofs_first; j <= ofs_last; j++)
				if (deltas[j].obj->type == OBJ_OFS_DELTA)
					resolve_delta(&deltas[j], data,
						      obj->size, obj->type);
		free(data);
	}

	/* Check for unresolved deltas */
	for (i = 0; i < nr_deltas; i++) {
		if (deltas[i].obj->real_type == OBJ_REF_DELTA ||
		    deltas[i].obj->real_type == OBJ_OFS_DELTA)
			die("packfile '%s' has unresolved deltas",  pack_name);
	}
}

static int sha1_compare(const void *_a, const void *_b)
{
	struct object_entry *a = *(struct object_entry **)_a;
	struct object_entry *b = *(struct object_entry **)_b;
	return hashcmp(a->sha1, b->sha1);
}

/*
 * On entry *sha1 contains the pack content SHA1 hash, on exit it is
 * the SHA1 hash of sorted object names.
 */
static void write_index_file(const char *index_name, unsigned char *sha1)
{
	struct sha1file *f;
	struct object_entry **sorted_by_sha, **list, **last;
	unsigned int array[256];
	int i;
	SHA_CTX ctx;

	if (nr_objects) {
		sorted_by_sha =
			xcalloc(nr_objects, sizeof(struct object_entry *));
		list = sorted_by_sha;
		last = sorted_by_sha + nr_objects;
		for (i = 0; i < nr_objects; ++i)
			sorted_by_sha[i] = &objects[i];
		qsort(sorted_by_sha, nr_objects, sizeof(sorted_by_sha[0]),
		      sha1_compare);

	}
	else
		sorted_by_sha = list = last = NULL;

	unlink(index_name);
	f = sha1create("%s", index_name);

	/*
	 * Write the first-level table (the list is sorted,
	 * but we use a 256-entry lookup to be able to avoid
	 * having to do eight extra binary search iterations).
	 */
	for (i = 0; i < 256; i++) {
		struct object_entry **next = list;
		while (next < last) {
			struct object_entry *obj = *next;
			if (obj->sha1[0] != i)
				break;
			next++;
		}
		array[i] = htonl(next - sorted_by_sha);
		list = next;
	}
	sha1write(f, array, 256 * sizeof(int));

	/* recompute the SHA1 hash of sorted object names.
	 * currently pack-objects does not do this, but that
	 * can be fixed.
	 */
	SHA1_Init(&ctx);
	/*
	 * Write the actual SHA1 entries..
	 */
	list = sorted_by_sha;
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = *list++;
		unsigned int offset = htonl(obj->offset);
		sha1write(f, &offset, 4);
		sha1write(f, obj->sha1, 20);
		SHA1_Update(&ctx, obj->sha1, 20);
	}
	sha1write(f, sha1, 20);
	sha1close(f, NULL, 1);
	free(sorted_by_sha);
	SHA1_Final(sha1, &ctx);
}

int main(int argc, char **argv)
{
	int i;
	char *index_name = NULL;
	char *index_name_buf = NULL;
	unsigned char sha1[20];

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp(arg, "-o")) {
				if (index_name || (i+1) >= argc)
					usage(index_pack_usage);
				index_name = argv[++i];
			} else
				usage(index_pack_usage);
			continue;
		}

		if (pack_name)
			usage(index_pack_usage);
		pack_name = arg;
	}

	if (!pack_name)
		usage(index_pack_usage);
	if (!index_name) {
		int len = strlen(pack_name);
		if (!has_extension(pack_name, ".pack"))
			die("packfile name '%s' does not end with '.pack'",
			    pack_name);
		index_name_buf = xmalloc(len);
		memcpy(index_name_buf, pack_name, len - 5);
		strcpy(index_name_buf + len - 5, ".idx");
		index_name = index_name_buf;
	}

	open_pack_file();
	parse_pack_header();
	objects = xcalloc(nr_objects + 1, sizeof(struct object_entry));
	deltas = xcalloc(nr_objects, sizeof(struct delta_entry));
	parse_pack_objects(sha1);
	free(deltas);
	write_index_file(index_name, sha1);
	free(objects);
	free(index_name_buf);

	printf("%s\n", sha1_to_hex(sha1));

	return 0;
}
