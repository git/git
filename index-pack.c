#include "cache.h"
#include "delta.h"
#include "pack.h"
#include "csum-file.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"

static const char index_pack_usage[] =
"git-index-pack [-v] [-o <index-file>] [{ ---keep | --keep=<msg> }] { <pack-file> | --stdin [--fix-thin] [<pack-file>] }";

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
	union delta_base base;
	int obj_no;
};

static struct object_entry *objects;
static struct delta_entry *deltas;
static int nr_objects;
static int nr_deltas;
static int nr_resolved_deltas;

static int from_stdin;
static int verbose;

static volatile sig_atomic_t progress_update;

static void progress_interval(int signum)
{
	progress_update = 1;
}

static void setup_progress_signal(void)
{
	struct sigaction sa;
	struct itimerval v;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = progress_interval;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa, NULL);

	v.it_interval.tv_sec = 1;
	v.it_interval.tv_usec = 0;
	v.it_value = v.it_interval;
	setitimer(ITIMER_REAL, &v, NULL);

}

static unsigned display_progress(unsigned n, unsigned total, unsigned last_pc)
{
	unsigned percent = n * 100 / total;
	if (percent != last_pc || progress_update) {
		fprintf(stderr, "%4u%% (%u/%u) done\r", percent, n, total);
		progress_update = 0;
	}
	return percent;
}

/* We always read in 4kB chunks. */
static unsigned char input_buffer[4096];
static unsigned long input_offset, input_len, consumed_bytes;
static SHA_CTX input_ctx;
static int input_fd, output_fd, pack_fd;

/* Discard current buffer used content. */
static void flush(void)
{
	if (input_offset) {
		if (output_fd >= 0)
			write_or_die(output_fd, input_buffer, input_offset);
		SHA1_Update(&input_ctx, input_buffer, input_offset);
		memmove(input_buffer, input_buffer + input_offset, input_len);
		input_offset = 0;
	}
}

/*
 * Make sure at least "min" bytes are available in the buffer, and
 * return the pointer to the buffer.
 */
static void *fill(int min)
{
	if (min <= input_len)
		return input_buffer + input_offset;
	if (min > sizeof(input_buffer))
		die("cannot fill %d bytes", min);
	flush();
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

static const char *open_pack_file(const char *pack_name)
{
	if (from_stdin) {
		input_fd = 0;
		if (!pack_name) {
			static char tmpfile[PATH_MAX];
			snprintf(tmpfile, sizeof(tmpfile),
				 "%s/tmp_pack_XXXXXX", get_object_directory());
			output_fd = mkstemp(tmpfile);
			pack_name = xstrdup(tmpfile);
		} else
			output_fd = open(pack_name, O_CREAT|O_EXCL|O_RDWR, 0600);
		if (output_fd < 0)
			die("unable to create %s: %s\n", pack_name, strerror(errno));
		pack_fd = output_fd;
	} else {
		input_fd = open(pack_name, O_RDONLY);
		if (input_fd < 0)
			die("cannot open packfile '%s': %s",
			    pack_name, strerror(errno));
		output_fd = -1;
		pack_fd = input_fd;
	}
	SHA1_Init(&input_ctx);
	return pack_name;
}

static void parse_pack_header(void)
{
	struct pack_header *hdr = fill(sizeof(struct pack_header));

	/* Header consistency check */
	if (hdr->hdr_signature != htonl(PACK_SIGNATURE))
		die("pack signature mismatch");
	if (!pack_version_ok(hdr->hdr_version))
		die("pack version %d unsupported", ntohl(hdr->hdr_version));

	nr_objects = ntohl(hdr->hdr_entries);
	use(sizeof(struct pack_header));
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
	die("pack has bad object at offset %lu: %s", offset, buf);
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
		bad_object(obj->offset, "unknown object type %d", obj->type);
	}
	obj->hdr_size = consumed_bytes - obj->offset;

	return unpack_entry_data(obj->offset, obj->size);
}

static void *get_data_from_pack(struct object_entry *obj)
{
	unsigned long from = obj[0].offset + obj[0].hdr_size;
	unsigned long len = obj[1].offset - from;
	unsigned long rdy = 0;
	unsigned char *src, *data;
	z_stream stream;
	int st;

	src = xmalloc(len);
	data = src;
	do {
		ssize_t n = pread(pack_fd, data + rdy, len - rdy, from + rdy);
		if (n <= 0)
			die("cannot pread pack file: %s", strerror(errno));
		rdy += n;
	} while (rdy < len);
	data = xmalloc(obj->size);
	memset(&stream, 0, sizeof(stream));
	stream.next_out = data;
	stream.avail_out = obj->size;
	stream.next_in = src;
	stream.avail_in = len;
	inflateInit(&stream);
	while ((st = inflate(&stream, Z_FINISH)) == Z_OK);
	inflateEnd(&stream);
	if (st != Z_STREAM_END || stream.total_out != obj->size)
		die("serious inflate inconsistency");
	free(src);
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

static int find_delta_children(const union delta_base *base,
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
	hash_sha1_file(data, size, typename(type), sha1);
	if (has_sha1_file(sha1)) {
		void *has_data;
		enum object_type has_type;
		unsigned long has_size;
		has_data = read_sha1_file(sha1, &has_type, &has_size);
		if (!has_data)
			die("cannot read existing object %s", sha1_to_hex(sha1));
		if (size != has_size || type != has_type ||
		    memcmp(data, has_data, size) != 0)
			die("SHA1 COLLISION FOUND WITH %s !", sha1_to_hex(sha1));
		free(has_data);
	}
}

static void resolve_delta(struct object_entry *delta_obj, void *base_data,
			  unsigned long base_size, enum object_type type)
{
	void *delta_data;
	unsigned long delta_size;
	void *result;
	unsigned long result_size;
	union delta_base delta_base;
	int j, first, last;

	delta_obj->real_type = type;
	delta_data = get_data_from_pack(delta_obj);
	delta_size = delta_obj->size;
	result = patch_delta(base_data, base_size, delta_data, delta_size,
			     &result_size);
	free(delta_data);
	if (!result)
		bad_object(delta_obj->offset, "failed to apply delta");
	sha1_object(result, result_size, type, delta_obj->sha1);
	nr_resolved_deltas++;

	hashcpy(delta_base.sha1, delta_obj->sha1);
	if (!find_delta_children(&delta_base, &first, &last)) {
		for (j = first; j <= last; j++) {
			struct object_entry *child = objects + deltas[j].obj_no;
			if (child->real_type == OBJ_REF_DELTA)
				resolve_delta(child, result, result_size, type);
		}
	}

	memset(&delta_base, 0, sizeof(delta_base));
	delta_base.offset = delta_obj->offset;
	if (!find_delta_children(&delta_base, &first, &last)) {
		for (j = first; j <= last; j++) {
			struct object_entry *child = objects + deltas[j].obj_no;
			if (child->real_type == OBJ_OFS_DELTA)
				resolve_delta(child, result, result_size, type);
		}
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
	int i, percent = -1;
	struct delta_entry *delta = deltas;
	void *data;
	struct stat st;

	/*
	 * First pass:
	 * - find locations of all objects;
	 * - calculate SHA1 of all non-delta objects;
	 * - remember base (SHA1 or offset) for all deltas.
	 */
	if (verbose)
		fprintf(stderr, "Indexing %d objects.\n", nr_objects);
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];
		data = unpack_raw_entry(obj, &delta->base);
		obj->real_type = obj->type;
		if (obj->type == OBJ_REF_DELTA || obj->type == OBJ_OFS_DELTA) {
			nr_deltas++;
			delta->obj_no = i;
			delta++;
		} else
			sha1_object(data, obj->size, obj->type, obj->sha1);
		free(data);
		if (verbose)
			percent = display_progress(i+1, nr_objects, percent);
	}
	objects[i].offset = consumed_bytes;
	if (verbose)
		fputc('\n', stderr);

	/* Check pack integrity */
	flush();
	SHA1_Final(sha1, &input_ctx);
	if (hashcmp(fill(20), sha1))
		die("pack is corrupted (SHA1 mismatch)");
	use(20);

	/* If input_fd is a file, we should have reached its end now. */
	if (fstat(input_fd, &st))
		die("cannot fstat packfile: %s", strerror(errno));
	if (S_ISREG(st.st_mode) &&
			lseek(input_fd, 0, SEEK_CUR) - input_len != st.st_size)
		die("pack has junk at the end");

	if (!nr_deltas)
		return;

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
	if (verbose)
		fprintf(stderr, "Resolving %d deltas.\n", nr_deltas);
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *obj = &objects[i];
		union delta_base base;
		int j, ref, ref_first, ref_last, ofs, ofs_first, ofs_last;

		if (obj->type == OBJ_REF_DELTA || obj->type == OBJ_OFS_DELTA)
			continue;
		hashcpy(base.sha1, obj->sha1);
		ref = !find_delta_children(&base, &ref_first, &ref_last);
		memset(&base, 0, sizeof(base));
		base.offset = obj->offset;
		ofs = !find_delta_children(&base, &ofs_first, &ofs_last);
		if (!ref && !ofs)
			continue;
		data = get_data_from_pack(obj);
		if (ref)
			for (j = ref_first; j <= ref_last; j++) {
				struct object_entry *child = objects + deltas[j].obj_no;
				if (child->real_type == OBJ_REF_DELTA)
					resolve_delta(child, data,
						      obj->size, obj->type);
			}
		if (ofs)
			for (j = ofs_first; j <= ofs_last; j++) {
				struct object_entry *child = objects + deltas[j].obj_no;
				if (child->real_type == OBJ_OFS_DELTA)
					resolve_delta(child, data,
						      obj->size, obj->type);
			}
		free(data);
		if (verbose)
			percent = display_progress(nr_resolved_deltas,
						   nr_deltas, percent);
	}
	if (verbose && nr_resolved_deltas == nr_deltas)
		fputc('\n', stderr);
}

static int write_compressed(int fd, void *in, unsigned int size)
{
	z_stream stream;
	unsigned long maxsize;
	void *out;

	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, zlib_compression_level);
	maxsize = deflateBound(&stream, size);
	out = xmalloc(maxsize);

	/* Compress it */
	stream.next_in = in;
	stream.avail_in = size;
	stream.next_out = out;
	stream.avail_out = maxsize;
	while (deflate(&stream, Z_FINISH) == Z_OK);
	deflateEnd(&stream);

	size = stream.total_out;
	write_or_die(fd, out, size);
	free(out);
	return size;
}

static void append_obj_to_pack(const unsigned char *sha1, void *buf,
			       unsigned long size, enum object_type type)
{
	struct object_entry *obj = &objects[nr_objects++];
	unsigned char header[10];
	unsigned long s = size;
	int n = 0;
	unsigned char c = (type << 4) | (s & 15);
	s >>= 4;
	while (s) {
		header[n++] = c | 0x80;
		c = s & 0x7f;
		s >>= 7;
	}
	header[n++] = c;
	write_or_die(output_fd, header, n);
	obj[1].offset = obj[0].offset + n;
	obj[1].offset += write_compressed(output_fd, buf, size);
	hashcpy(obj->sha1, sha1);
}

static int delta_pos_compare(const void *_a, const void *_b)
{
	struct delta_entry *a = *(struct delta_entry **)_a;
	struct delta_entry *b = *(struct delta_entry **)_b;
	return a->obj_no - b->obj_no;
}

static void fix_unresolved_deltas(int nr_unresolved)
{
	struct delta_entry **sorted_by_pos;
	int i, n = 0, percent = -1;

	/*
	 * Since many unresolved deltas may well be themselves base objects
	 * for more unresolved deltas, we really want to include the
	 * smallest number of base objects that would cover as much delta
	 * as possible by picking the
	 * trunc deltas first, allowing for other deltas to resolve without
	 * additional base objects.  Since most base objects are to be found
	 * before deltas depending on them, a good heuristic is to start
	 * resolving deltas in the same order as their position in the pack.
	 */
	sorted_by_pos = xmalloc(nr_unresolved * sizeof(*sorted_by_pos));
	for (i = 0; i < nr_deltas; i++) {
		if (objects[deltas[i].obj_no].real_type != OBJ_REF_DELTA)
			continue;
		sorted_by_pos[n++] = &deltas[i];
	}
	qsort(sorted_by_pos, n, sizeof(*sorted_by_pos), delta_pos_compare);

	for (i = 0; i < n; i++) {
		struct delta_entry *d = sorted_by_pos[i];
		void *data;
		unsigned long size;
		enum object_type type;
		int j, first, last;

		if (objects[d->obj_no].real_type != OBJ_REF_DELTA)
			continue;
		data = read_sha1_file(d->base.sha1, &type, &size);
		if (!data)
			continue;

		find_delta_children(&d->base, &first, &last);
		for (j = first; j <= last; j++) {
			struct object_entry *child = objects + deltas[j].obj_no;
			if (child->real_type == OBJ_REF_DELTA)
				resolve_delta(child, data, size, type);
		}

		if (check_sha1_signature(d->base.sha1, data, size, typename(type)))
			die("local object %s is corrupt", sha1_to_hex(d->base.sha1));
		append_obj_to_pack(d->base.sha1, data, size, type);
		free(data);
		if (verbose)
			percent = display_progress(nr_resolved_deltas,
						   nr_deltas, percent);
	}
	free(sorted_by_pos);
	if (verbose)
		fputc('\n', stderr);
}

static void readjust_pack_header_and_sha1(unsigned char *sha1)
{
	struct pack_header hdr;
	SHA_CTX ctx;
	int size;

	/* Rewrite pack header with updated object number */
	if (lseek(output_fd, 0, SEEK_SET) != 0)
		die("cannot seek back: %s", strerror(errno));
	if (read_in_full(output_fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		die("cannot read pack header back: %s", strerror(errno));
	hdr.hdr_entries = htonl(nr_objects);
	if (lseek(output_fd, 0, SEEK_SET) != 0)
		die("cannot seek back: %s", strerror(errno));
	write_or_die(output_fd, &hdr, sizeof(hdr));
	if (lseek(output_fd, 0, SEEK_SET) != 0)
		die("cannot seek back: %s", strerror(errno));

	/* Recompute and store the new pack's SHA1 */
	SHA1_Init(&ctx);
	do {
		unsigned char *buf[4096];
		size = xread(output_fd, buf, sizeof(buf));
		if (size < 0)
			die("cannot read pack data back: %s", strerror(errno));
		SHA1_Update(&ctx, buf, size);
	} while (size > 0);
	SHA1_Final(sha1, &ctx);
	write_or_die(output_fd, sha1, 20);
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
static const char *write_index_file(const char *index_name, unsigned char *sha1)
{
	struct sha1file *f;
	struct object_entry **sorted_by_sha, **list, **last;
	unsigned int array[256];
	int i, fd;
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

	if (!index_name) {
		static char tmpfile[PATH_MAX];
		snprintf(tmpfile, sizeof(tmpfile),
			 "%s/tmp_idx_XXXXXX", get_object_directory());
		fd = mkstemp(tmpfile);
		index_name = xstrdup(tmpfile);
	} else {
		unlink(index_name);
		fd = open(index_name, O_CREAT|O_EXCL|O_WRONLY, 0600);
	}
	if (fd < 0)
		die("unable to create %s: %s", index_name, strerror(errno));
	f = sha1fd(fd, index_name);

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
	return index_name;
}

static void final(const char *final_pack_name, const char *curr_pack_name,
		  const char *final_index_name, const char *curr_index_name,
		  const char *keep_name, const char *keep_msg,
		  unsigned char *sha1)
{
	const char *report = "pack";
	char name[PATH_MAX];
	int err;

	if (!from_stdin) {
		close(input_fd);
	} else {
		err = close(output_fd);
		if (err)
			die("error while closing pack file: %s", strerror(errno));
		chmod(curr_pack_name, 0444);
	}

	if (keep_msg) {
		int keep_fd, keep_msg_len = strlen(keep_msg);
		if (!keep_name) {
			snprintf(name, sizeof(name), "%s/pack/pack-%s.keep",
				 get_object_directory(), sha1_to_hex(sha1));
			keep_name = name;
		}
		keep_fd = open(keep_name, O_RDWR|O_CREAT|O_EXCL, 0600);
		if (keep_fd < 0) {
			if (errno != EEXIST)
				die("cannot write keep file");
		} else {
			if (keep_msg_len > 0) {
				write_or_die(keep_fd, keep_msg, keep_msg_len);
				write_or_die(keep_fd, "\n", 1);
			}
			close(keep_fd);
			report = "keep";
		}
	}

	if (final_pack_name != curr_pack_name) {
		if (!final_pack_name) {
			snprintf(name, sizeof(name), "%s/pack/pack-%s.pack",
				 get_object_directory(), sha1_to_hex(sha1));
			final_pack_name = name;
		}
		if (move_temp_to_file(curr_pack_name, final_pack_name))
			die("cannot store pack file");
	}

	chmod(curr_index_name, 0444);
	if (final_index_name != curr_index_name) {
		if (!final_index_name) {
			snprintf(name, sizeof(name), "%s/pack/pack-%s.idx",
				 get_object_directory(), sha1_to_hex(sha1));
			final_index_name = name;
		}
		if (move_temp_to_file(curr_index_name, final_index_name))
			die("cannot store index file");
	}

	if (!from_stdin) {
		printf("%s\n", sha1_to_hex(sha1));
	} else {
		char buf[48];
		int len = snprintf(buf, sizeof(buf), "%s\t%s\n",
				   report, sha1_to_hex(sha1));
		write_or_die(1, buf, len);

		/*
		 * Let's just mimic git-unpack-objects here and write
		 * the last part of the input buffer to stdout.
		 */
		while (input_len) {
			err = xwrite(1, input_buffer + input_offset, input_len);
			if (err <= 0)
				break;
			input_len -= err;
			input_offset += err;
		}
	}
}

int main(int argc, char **argv)
{
	int i, fix_thin_pack = 0;
	const char *curr_pack, *pack_name = NULL;
	const char *curr_index, *index_name = NULL;
	const char *keep_name = NULL, *keep_msg = NULL;
	char *index_name_buf = NULL, *keep_name_buf = NULL;
	unsigned char sha1[20];

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp(arg, "--stdin")) {
				from_stdin = 1;
			} else if (!strcmp(arg, "--fix-thin")) {
				fix_thin_pack = 1;
			} else if (!strcmp(arg, "--keep")) {
				keep_msg = "";
			} else if (!prefixcmp(arg, "--keep=")) {
				keep_msg = arg + 7;
			} else if (!prefixcmp(arg, "--pack_header=")) {
				struct pack_header *hdr;
				char *c;

				hdr = (struct pack_header *)input_buffer;
				hdr->hdr_signature = htonl(PACK_SIGNATURE);
				hdr->hdr_version = htonl(strtoul(arg + 14, &c, 10));
				if (*c != ',')
					die("bad %s", arg);
				hdr->hdr_entries = htonl(strtoul(c + 1, &c, 10));
				if (*c)
					die("bad %s", arg);
				input_len = sizeof(*hdr);
			} else if (!strcmp(arg, "-v")) {
				verbose = 1;
			} else if (!strcmp(arg, "-o")) {
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

	if (!pack_name && !from_stdin)
		usage(index_pack_usage);
	if (fix_thin_pack && !from_stdin)
		die("--fix-thin cannot be used without --stdin");
	if (!index_name && pack_name) {
		int len = strlen(pack_name);
		if (!has_extension(pack_name, ".pack"))
			die("packfile name '%s' does not end with '.pack'",
			    pack_name);
		index_name_buf = xmalloc(len);
		memcpy(index_name_buf, pack_name, len - 5);
		strcpy(index_name_buf + len - 5, ".idx");
		index_name = index_name_buf;
	}
	if (keep_msg && !keep_name && pack_name) {
		int len = strlen(pack_name);
		if (!has_extension(pack_name, ".pack"))
			die("packfile name '%s' does not end with '.pack'",
			    pack_name);
		keep_name_buf = xmalloc(len);
		memcpy(keep_name_buf, pack_name, len - 5);
		strcpy(keep_name_buf + len - 5, ".keep");
		keep_name = keep_name_buf;
	}

	curr_pack = open_pack_file(pack_name);
	parse_pack_header();
	objects = xmalloc((nr_objects + 1) * sizeof(struct object_entry));
	deltas = xmalloc(nr_objects * sizeof(struct delta_entry));
	if (verbose)
		setup_progress_signal();
	parse_pack_objects(sha1);
	if (nr_deltas != nr_resolved_deltas) {
		if (fix_thin_pack) {
			int nr_unresolved = nr_deltas - nr_resolved_deltas;
			int nr_objects_initial = nr_objects;
			if (nr_unresolved <= 0)
				die("confusion beyond insanity");
			objects = xrealloc(objects,
					   (nr_objects + nr_unresolved + 1)
					   * sizeof(*objects));
			fix_unresolved_deltas(nr_unresolved);
			if (verbose)
				fprintf(stderr, "%d objects were added to complete this thin pack.\n",
					nr_objects - nr_objects_initial);
			readjust_pack_header_and_sha1(sha1);
		}
		if (nr_deltas != nr_resolved_deltas)
			die("pack has %d unresolved deltas",
			    nr_deltas - nr_resolved_deltas);
	} else {
		/* Flush remaining pack final 20-byte SHA1. */
		flush();
	}
	free(deltas);
	curr_index = write_index_file(index_name, sha1);
	final(pack_name, curr_pack,
		index_name, curr_index,
		keep_name, keep_msg,
		sha1);
	free(objects);
	free(index_name_buf);
	free(keep_name_buf);

	return 0;
}
