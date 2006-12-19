#include "builtin.h"
#include "cache.h"
#include "object.h"
#include "delta.h"
#include "pack.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"

static int dry_run, quiet, recover, has_errors;
static const char unpack_usage[] = "git-unpack-objects [-n] [-q] [-r] < pack-file";

/* We always read in 4kB chunks. */
static unsigned char buffer[4096];
static unsigned long offset, len, consumed_bytes;
static SHA_CTX ctx;

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
		SHA1_Update(&ctx, buffer, offset);
		memmove(buffer, buffer + offset, len);
		offset = 0;
	}
	do {
		int ret = xread(0, buffer + len, sizeof(buffer) - len);
		if (ret <= 0) {
			if (!ret)
				die("early EOF");
			die("read error on input: %s", strerror(errno));
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
	consumed_bytes += bytes;
}

static void *get_data(unsigned long size)
{
	z_stream stream;
	void *buf = xmalloc(size);

	memset(&stream, 0, sizeof(stream));

	stream.next_out = buf;
	stream.avail_out = size;
	stream.next_in = fill(1);
	stream.avail_in = len;
	inflateInit(&stream);

	for (;;) {
		int ret = inflate(&stream, 0);
		use(len - stream.avail_in);
		if (stream.total_out == size && ret == Z_STREAM_END)
			break;
		if (ret != Z_OK) {
			error("inflate returned %d\n", ret);
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
	inflateEnd(&stream);
	return buf;
}

struct delta_info {
	unsigned char base_sha1[20];
	unsigned long base_offset;
	unsigned long size;
	void *delta;
	unsigned nr;
	struct delta_info *next;
};

static struct delta_info *delta_list;

static void add_delta_to_list(unsigned nr, unsigned const char *base_sha1,
			      unsigned long base_offset,
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
	unsigned long offset;
	unsigned char sha1[20];
};

static struct obj_info *obj_list;

static void added_object(unsigned nr, const char *type, void *data,
			 unsigned long size);

static void write_object(unsigned nr, void *buf, unsigned long size,
			 const char *type)
{
	if (write_sha1_file(buf, size, type, obj_list[nr].sha1) < 0)
		die("failed to write object");
	added_object(nr, type, buf, size);
}

static void resolve_delta(unsigned nr, const char *type,
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
	write_object(nr, result, result_size, type);
	free(result);
}

static void added_object(unsigned nr, const char *type, void *data,
			 unsigned long size)
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

static void unpack_non_delta_entry(enum object_type kind, unsigned long size,
				   unsigned nr)
{
	void *buf = get_data(size);
	const char *type;

	switch (kind) {
	case OBJ_COMMIT: type = commit_type; break;
	case OBJ_TREE:   type = tree_type; break;
	case OBJ_BLOB:   type = blob_type; break;
	case OBJ_TAG:    type = tag_type; break;
	default: die("bad type %d", kind);
	}
	if (!dry_run && buf)
		write_object(nr, buf, size, type);
	free(buf);
}

static void unpack_delta_entry(enum object_type kind, unsigned long delta_size,
			       unsigned nr)
{
	void *delta_data, *base;
	unsigned long base_size;
	char type[20];
	unsigned char base_sha1[20];

	if (kind == OBJ_REF_DELTA) {
		hashcpy(base_sha1, fill(20));
		use(20);
		delta_data = get_data(delta_size);
		if (dry_run || !delta_data) {
			free(delta_data);
			return;
		}
		if (!has_sha1_file(base_sha1)) {
			hashcpy(obj_list[nr].sha1, null_sha1);
			add_delta_to_list(nr, base_sha1, 0, delta_data, delta_size);
			return;
		}
	} else {
		unsigned base_found = 0;
		unsigned char *pack, c;
		unsigned long base_offset;
		unsigned lo, mid, hi;

		pack = fill(1);
		c = *pack;
		use(1);
		base_offset = c & 127;
		while (c & 128) {
			base_offset += 1;
			if (!base_offset || base_offset & ~(~0UL >> 7))
				die("offset value overflow for delta base object");
			pack = fill(1);
			c = *pack;
			use(1);
			base_offset = (base_offset << 7) + (c & 127);
		}
		base_offset = obj_list[nr].offset - base_offset;

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
			/* The delta base object is itself a delta that
			   has not been	resolved yet. */
			hashcpy(obj_list[nr].sha1, null_sha1);
			add_delta_to_list(nr, null_sha1, base_offset, delta_data, delta_size);
			return;
		}
	}

	base = read_sha1_file(base_sha1, type, &base_size);
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

static void unpack_one(unsigned nr, unsigned total)
{
	unsigned shift;
	unsigned char *pack, c;
	unsigned long size;
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
	if (!quiet) {
		static unsigned long last_sec;
		static unsigned last_percent;
		struct timeval now;
		unsigned percentage = ((nr+1) * 100) / total;

		gettimeofday(&now, NULL);
		if (percentage != last_percent || now.tv_sec != last_sec) {
			last_sec = now.tv_sec;
			last_percent = percentage;
			fprintf(stderr, "%4u%% (%u/%u) done\r",
					percentage, (nr+1), total);
		}
	}
	switch (type) {
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
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
	unsigned nr_objects = ntohl(hdr->hdr_entries);

	if (ntohl(hdr->hdr_signature) != PACK_SIGNATURE)
		die("bad pack file");
	if (!pack_version_ok(hdr->hdr_version))
		die("unknown pack file version %d", ntohl(hdr->hdr_version));
	fprintf(stderr, "Unpacking %d objects\n", nr_objects);

	obj_list = xmalloc(nr_objects * sizeof(*obj_list));
	use(sizeof(struct pack_header));
	for (i = 0; i < nr_objects; i++)
		unpack_one(i, nr_objects);
	if (delta_list)
		die("unresolved deltas left after unpacking");
}

int cmd_unpack_objects(int argc, const char **argv, const char *prefix)
{
	int i;
	unsigned char sha1[20];

	git_config(git_default_config);

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
			if (!strncmp(arg, "--pack_header=", 14)) {
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
	SHA1_Init(&ctx);
	unpack_all();
	SHA1_Update(&ctx, buffer, offset);
	SHA1_Final(sha1, &ctx);
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
	if (!quiet)
		fprintf(stderr, "\n");
	return has_errors;
}
