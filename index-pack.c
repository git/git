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
	enum object_type type;
	enum object_type real_type;
	unsigned char sha1[20];
};

struct delta_entry
{
	struct object_entry *obj;
	unsigned char base_sha1[20];
};

static const char *pack_name;
static unsigned char *pack_base;
static unsigned long pack_size;
static struct object_entry *objects;
static struct delta_entry *deltas;
static int nr_objects;
static int nr_deltas;

static void open_pack_file(void)
{
	int fd;
	struct stat st;

	fd = open(pack_name, O_RDONLY);
	if (fd < 0)
		die("cannot open packfile '%s': %s", pack_name,
		    strerror(errno));
	if (fstat(fd, &st)) {
		int err = errno;
		close(fd);
		die("cannot fstat packfile '%s': %s", pack_name,
		    strerror(err));
	}
	pack_size = st.st_size;
	pack_base = mmap(NULL, pack_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (pack_base == MAP_FAILED) {
		int err = errno;
		close(fd);
		die("cannot mmap packfile '%s': %s", pack_name,
		    strerror(err));
	}
	close(fd);
}

static void parse_pack_header(void)
{
	const struct pack_header *hdr;
	unsigned char sha1[20];
	SHA_CTX ctx;

	/* Ensure there are enough bytes for the header and final SHA1 */
	if (pack_size < sizeof(struct pack_header) + 20)
		die("packfile '%s' is too small", pack_name);

	/* Header consistency check */
	hdr = (void *)pack_base;
	if (hdr->hdr_signature != htonl(PACK_SIGNATURE))
		die("packfile '%s' signature mismatch", pack_name);
	if (!pack_version_ok(hdr->hdr_version))
		die("packfile '%s' version %d unsupported",
		    pack_name, ntohl(hdr->hdr_version));

	nr_objects = ntohl(hdr->hdr_entries);

	/* Check packfile integrity */
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, pack_base, pack_size - 20);
	SHA1_Final(sha1, &ctx);
	if (hashcmp(sha1, pack_base + pack_size - 20))
		die("packfile '%s' SHA1 mismatch", pack_name);
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

static void *unpack_entry_data(unsigned long offset,
			       unsigned long *current_pos, unsigned long size)
{
	unsigned long pack_limit = pack_size - 20;
	unsigned long pos = *current_pos;
	z_stream stream;
	void *buf = xmalloc(size);

	memset(&stream, 0, sizeof(stream));
	stream.next_out = buf;
	stream.avail_out = size;
	stream.next_in = pack_base + pos;
	stream.avail_in = pack_limit - pos;
	inflateInit(&stream);

	for (;;) {
		int ret = inflate(&stream, 0);
		if (ret == Z_STREAM_END)
			break;
		if (ret != Z_OK)
			bad_object(offset, "inflate returned %d", ret);
	}
	inflateEnd(&stream);
	if (stream.total_out != size)
		bad_object(offset, "size mismatch (expected %lu, got %lu)",
			   size, stream.total_out);
	*current_pos = pack_limit - stream.avail_in;
	return buf;
}

static void *unpack_raw_entry(unsigned long offset,
			      enum object_type *obj_type,
			      unsigned long *obj_size,
			      unsigned char *delta_base,
			      unsigned long *next_obj_offset)
{
	unsigned long pack_limit = pack_size - 20;
	unsigned long pos = offset;
	unsigned char c;
	unsigned long size;
	unsigned shift;
	enum object_type type;
	void *data;

	c = pack_base[pos++];
	type = (c >> 4) & 7;
	size = (c & 15);
	shift = 4;
	while (c & 0x80) {
		if (pos >= pack_limit)
			bad_object(offset, "object extends past end of pack");
		c = pack_base[pos++];
		size += (c & 0x7fUL) << shift;
		shift += 7;
	}

	switch (type) {
	case OBJ_DELTA:
		if (pos + 20 >= pack_limit)
			bad_object(offset, "object extends past end of pack");
		hashcpy(delta_base, pack_base + pos);
		pos += 20;
		/* fallthru */
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		data = unpack_entry_data(offset, &pos, size);
		break;
	default:
		bad_object(offset, "bad object type %d", type);
	}

	*obj_type = type;
	*obj_size = size;
	*next_obj_offset = pos;
	return data;
}

static int find_delta(const unsigned char *base_sha1)
{
	int first = 0, last = nr_deltas;

        while (first < last) {
                int next = (first + last) / 2;
                struct delta_entry *delta = &deltas[next];
                int cmp;

                cmp = hashcmp(base_sha1, delta->base_sha1);
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

static int find_deltas_based_on_sha1(const unsigned char *base_sha1,
				     int *first_index, int *last_index)
{
	int first = find_delta(base_sha1);
	int last = first;
	int end = nr_deltas - 1;

	if (first < 0)
		return -1;
	while (first > 0 && !hashcmp(deltas[first - 1].base_sha1, base_sha1))
		--first;
	while (last < end && !hashcmp(deltas[last + 1].base_sha1, base_sha1))
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
	enum object_type delta_type;
	unsigned char base_sha1[20];
	unsigned long next_obj_offset;
	int j, first, last;

	obj->real_type = type;
	delta_data = unpack_raw_entry(obj->offset, &delta_type,
				      &delta_size, base_sha1,
				      &next_obj_offset);
	result = patch_delta(base_data, base_size, delta_data, delta_size,
			     &result_size);
	free(delta_data);
	if (!result)
		bad_object(obj->offset, "failed to apply delta");
	sha1_object(result, result_size, type, obj->sha1);
	if (!find_deltas_based_on_sha1(obj->sha1, &first, &last)) {
		for (j = first; j <= last; j++)
			resolve_delta(&deltas[j], result, result_size, type);
	}
	free(result);
}

static int compare_delta_entry(const void *a, const void *b)
{
	const struct delta_entry *delta_a = a;
	const struct delta_entry *delta_b = b;
	return hashcmp(delta_a->base_sha1, delta_b->base_sha1);
}

static void parse_pack_objects(void)
{
	int i;
	unsigned long offset = sizeof(struct pack_header);
	unsigned char base_sha1[20];
	void *data;
	unsigned long data_size;

	/*
	 * First pass:
	 * - find locations of all objects;
	 * - calculate SHA1 of all non-delta objects;
	 * - remember base SHA1 for all deltas.
	 */
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];
		obj->offset = offset;
		data = unpack_raw_entry(offset, &obj->type, &data_size,
					base_sha1, &offset);
		obj->real_type = obj->type;
		if (obj->type == OBJ_DELTA) {
			struct delta_entry *delta = &deltas[nr_deltas++];
			delta->obj = obj;
			hashcpy(delta->base_sha1, base_sha1);
		} else
			sha1_object(data, data_size, obj->type, obj->sha1);
		free(data);
	}
	if (offset != pack_size - 20)
		die("packfile '%s' has junk at the end", pack_name);

	/* Sort deltas by base SHA1 for fast searching */
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
		int j, first, last;

		if (obj->type == OBJ_DELTA)
			continue;
		if (find_deltas_based_on_sha1(obj->sha1, &first, &last))
			continue;
		data = unpack_raw_entry(obj->offset, &obj->type, &data_size,
					base_sha1, &offset);
		for (j = first; j <= last; j++)
			resolve_delta(&deltas[j], data, data_size, obj->type);
		free(data);
	}

	/* Check for unresolved deltas */
	for (i = 0; i < nr_deltas; i++) {
		if (deltas[i].obj->real_type == OBJ_DELTA)
			die("packfile '%s' has unresolved deltas",  pack_name);
	}
}

static int sha1_compare(const void *_a, const void *_b)
{
	struct object_entry *a = *(struct object_entry **)_a;
	struct object_entry *b = *(struct object_entry **)_b;
	return hashcmp(a->sha1, b->sha1);
}

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
	sha1write(f, pack_base + pack_size - 20, 20);
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
	objects = xcalloc(nr_objects, sizeof(struct object_entry));
	deltas = xcalloc(nr_objects, sizeof(struct delta_entry));
	parse_pack_objects();
	free(deltas);
	write_index_file(index_name, sha1);
	free(objects);
	free(index_name_buf);

	printf("%s\n", sha1_to_hex(sha1));

	return 0;
}
