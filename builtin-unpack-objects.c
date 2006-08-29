#include "builtin.h"
#include "cache.h"
#include "object.h"
#include "delta.h"
#include "pack.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"

#include <sys/time.h>

static int dry_run, quiet;
static const char unpack_usage[] = "git-unpack-objects [-n] [-q] < pack-file";

/* We always read in 4kB chunks. */
static unsigned char buffer[4096];
static unsigned long offset, len;
static SHA_CTX ctx;

/*
 * Make sure at least "min" bytes are available in the buffer, and
 * return the pointer to the buffer.
 */
static void * fill(int min)
{
	if (min <= len)
		return buffer + offset;
	if (min > sizeof(buffer))
		die("cannot fill %d bytes", min);
	if (offset) {
		SHA1_Update(&ctx, buffer, offset);
		memcpy(buffer, buffer + offset, len);
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
		if (ret != Z_OK)
			die("inflate returned %d\n", ret);
		stream.next_in = fill(1);
		stream.avail_in = len;
	}
	inflateEnd(&stream);
	return buf;
}

struct delta_info {
	unsigned char base_sha1[20];
	unsigned long size;
	void *delta;
	struct delta_info *next;
};

static struct delta_info *delta_list;

static void add_delta_to_list(unsigned char *base_sha1, void *delta, unsigned long size)
{
	struct delta_info *info = xmalloc(sizeof(*info));

	hashcpy(info->base_sha1, base_sha1);
	info->size = size;
	info->delta = delta;
	info->next = delta_list;
	delta_list = info;
}

static void added_object(unsigned char *sha1, const char *type, void *data, unsigned long size);

static void write_object(void *buf, unsigned long size, const char *type)
{
	unsigned char sha1[20];
	if (write_sha1_file(buf, size, type, sha1) < 0)
		die("failed to write object");
	added_object(sha1, type, buf, size);
}

static int resolve_delta(const char *type,
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
	write_object(result, result_size, type);
	free(result);
	return 0;
}

static void added_object(unsigned char *sha1, const char *type, void *data, unsigned long size)
{
	struct delta_info **p = &delta_list;
	struct delta_info *info;

	while ((info = *p) != NULL) {
		if (!hashcmp(info->base_sha1, sha1)) {
			*p = info->next;
			p = &delta_list;
			resolve_delta(type, data, size, info->delta, info->size);
			free(info);
			continue;
		}
		p = &info->next;
	}
}

static int unpack_non_delta_entry(enum object_type kind, unsigned long size)
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
	if (!dry_run)
		write_object(buf, size, type);
	free(buf);
	return 0;
}

static int unpack_delta_entry(unsigned long delta_size)
{
	void *delta_data, *base;
	unsigned long base_size;
	char type[20];
	unsigned char base_sha1[20];
	int result;

	hashcpy(base_sha1, fill(20));
	use(20);

	delta_data = get_data(delta_size);
	if (dry_run) {
		free(delta_data);
		return 0;
	}

	if (!has_sha1_file(base_sha1)) {
		add_delta_to_list(base_sha1, delta_data, delta_size);
		return 0;
	}
	base = read_sha1_file(base_sha1, type, &base_size);
	if (!base)
		die("failed to read delta-pack base object %s", sha1_to_hex(base_sha1));
	result = resolve_delta(type, base, base_size, delta_data, delta_size);
	free(base);
	return result;
}

static void unpack_one(unsigned nr, unsigned total)
{
	unsigned shift;
	unsigned char *pack, c;
	unsigned long size;
	enum object_type type;

	pack = fill(1);
	c = *pack;
	use(1);
	type = (c >> 4) & 7;
	size = (c & 15);
	shift = 4;
	while (c & 0x80) {
		pack = fill(1);
		c = *pack++;
		use(1);
		size += (c & 0x7f) << shift;
		shift += 7;
	}
	if (!quiet) {
		static unsigned long last_sec;
		static unsigned last_percent;
		struct timeval now;
		unsigned percentage = (nr * 100) / total;

		gettimeofday(&now, NULL);
		if (percentage != last_percent || now.tv_sec != last_sec) {
			last_sec = now.tv_sec;
			last_percent = percentage;
			fprintf(stderr, "%4u%% (%u/%u) done\r", percentage, nr, total);
		}
	}
	switch (type) {
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		unpack_non_delta_entry(type, size);
		return;
	case OBJ_DELTA:
		unpack_delta_entry(size);
		return;
	default:
		die("bad object type %d", type);
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

	use(sizeof(struct pack_header));
	for (i = 0; i < nr_objects; i++)
		unpack_one(i+1, nr_objects);
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
	return 0;
}
