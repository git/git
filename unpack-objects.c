#include "cache.h"
#include "object.h"
#include "delta.h"
#include "pack.h"

static int dry_run;
static int nr_entries;
static const char *base_name;
static const char unpack_usage[] = "git-unpack-objects basename";

struct pack_entry {
	unsigned int offset; /* network byte order */
	unsigned char sha1[20];
};

static void *pack_base;
static unsigned long pack_size;
static void *index_base;
static unsigned long index_size;

static struct pack_entry **pack_list;

static void *map_file(const char *suffix, unsigned long *sizep)
{
	static char pathname[PATH_MAX];
	unsigned long len;
	int fd;
	struct stat st;
	void *map;

	len = snprintf(pathname, PATH_MAX, "%s.%s", base_name, suffix);
	if (len >= PATH_MAX)
		die("bad pack base-name");
	fd = open(pathname, O_RDONLY);
	if (fd < 0 || fstat(fd, &st))
		die("unable to open '%s'", pathname);
	len = st.st_size;
	if (!len)
		die("bad pack file '%s'", pathname);
	map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (-1 == (int)(long)map)
		die("unable to mmap '%s'", pathname);
	close(fd);
	*sizep = len;
	return map;
}

static int sort_by_offset(const void *_a, const void *_b)
{
	struct pack_entry *a = *(struct pack_entry **)_a;
	struct pack_entry *b = *(struct pack_entry **)_b;
	unsigned int o1, o2;

	o1 = ntohl(a->offset);
	o2 = ntohl(b->offset);
	return o1 < o2 ? -1 : 1;
}

static int check_index(void)
{
	unsigned int *array = index_base;
	unsigned int nr;
	int i;

	if (index_size < 4*256 + 20)
		return error("index file too small");
	nr = 0;
	for (i = 0; i < 256; i++) {
		unsigned int n = ntohl(array[i]);
		if (n < nr)
			return error("non-monotonic index");
		nr = n;
	}
	/*
	 * Total size:
	 *  - 256 index entries 4 bytes each
	 *  - 24-byte entries * nr (20-byte sha1 + 4-byte offset)
	 *  - 20-byte SHA1 of the packfile
	 *  - 20-byte SHA1 file checksum
	 */
	if (index_size != 4*256 + nr * 24 + 20 + 20)
		return error("wrong index file size");

	nr_entries = nr;
	pack_list = xmalloc(nr * sizeof(struct pack_entry *));
	for (i = 0; i < nr; i++)
		pack_list[i] = index_base + 4*256 + i*24;

	qsort(pack_list, nr, sizeof(*pack_list), sort_by_offset);

	printf("%d entries\n", nr);
	return 0;
}

static int unpack_non_delta_entry(struct pack_entry *entry,
				  enum object_type kind,
				  unsigned char *data,
				  unsigned long size,
				  unsigned long left)
{
	int st;
	z_stream stream;
	char *buffer;
	unsigned char sha1[20];
	char *type;

	printf("%s %c %lu\n", sha1_to_hex(entry->sha1), ".CTBGD"[kind], size);
	if (dry_run)
		return 0;

	buffer = xmalloc(size + 1);
	buffer[size] = 0;
	memset(&stream, 0, sizeof(stream));
	stream.next_in = data;
	stream.avail_in = left;
	stream.next_out = buffer;
	stream.avail_out = size;

	inflateInit(&stream);
	st = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	if ((st != Z_STREAM_END) || stream.total_out != size)
		goto err_finish;
	switch (kind) {
	case OBJ_COMMIT: type = "commit"; break;
	case OBJ_TREE:   type = "tree"; break;
	case OBJ_BLOB:   type = "blob"; break;
	case OBJ_TAG:    type = "tag"; break;
	default: goto err_finish;
	}
	if (write_sha1_file(buffer, size, type, sha1) < 0)
		die("failed to write %s (%s)",
		    sha1_to_hex(entry->sha1), type);
	printf("%s %s\n", sha1_to_hex(sha1), type);
	if (memcmp(sha1, entry->sha1, 20))
		die("resulting %s have wrong SHA1", type);

 finish:
	st = 0;
	free(buffer);
	return st;
 err_finish:
	st = -1;
	goto finish;
}

static int find_pack_entry(unsigned char *sha1, struct pack_entry **ent)
{
	int *level1_ofs = index_base;
	int hi = ntohl(level1_ofs[*sha1]);
	int lo = ((*sha1 == 0x0) ? 0 : ntohl(level1_ofs[*sha1 - 1]));
	void *index = index_base + 4*256;

	do {
		int mi = (lo + hi) / 2;
		int cmp = memcmp(index + 24 * mi + 4, sha1, 20);
		if (!cmp) {
			*ent = index + 24 * mi;
			return 1;
		}
		if (cmp > 0)
			hi = mi;
		else
			lo = mi+1;
	} while (lo < hi);
	return 0;
}

/* forward declaration for a mutually recursive function */
static void unpack_entry(struct pack_entry *);

static int unpack_delta_entry(struct pack_entry *entry,
			      unsigned char *base_sha1,
			      unsigned long delta_size,
			      unsigned long left)
{
	void *data, *delta_data, *result, *base;
	unsigned long data_size, result_size, base_size;
	z_stream stream;
	int st;
	char type[20];
	unsigned char sha1[20];

	if (left < 20)
		die("truncated pack file");
	data = base_sha1 + 20;
	data_size = left - 20;
	printf("%s D %lu", sha1_to_hex(entry->sha1), delta_size);
	printf(" %s\n", sha1_to_hex(base_sha1));

	if (dry_run)
		return 0;

	/* pack+5 is the base sha1, unless we have it, we need to
	 * unpack it first.
	 */
	if (!has_sha1_file(base_sha1)) {
		struct pack_entry *base;
		if (!find_pack_entry(base_sha1, &base))
			die("cannot find delta-pack base object");
		unpack_entry(base);
	}
	delta_data = xmalloc(delta_size);

	memset(&stream, 0, sizeof(stream));

	stream.next_in = data;
	stream.avail_in = data_size;
	stream.next_out = delta_data;
	stream.avail_out = delta_size;

	inflateInit(&stream);
	st = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	if ((st != Z_STREAM_END) || stream.total_out != delta_size)
		die("delta data unpack failed");

	base = read_sha1_file(base_sha1, type, &base_size);
	if (!base)
		die("failed to read delta-pack base object %s", sha1_to_hex(base_sha1));
	result = patch_delta(base, base_size,
			     delta_data, delta_size,
			     &result_size);
	if (!result)
		die("failed to apply delta");
	free(delta_data);

	if (write_sha1_file(result, result_size, type, sha1) < 0)
		die("failed to write %s (%s)",
		    sha1_to_hex(entry->sha1), type);
	free(result);
	printf("%s %s\n", sha1_to_hex(sha1), type);
	if (memcmp(sha1, entry->sha1, 20))
		die("resulting %s have wrong SHA1", type);
	return 0;
}

static void unpack_entry(struct pack_entry *entry)
{
	unsigned long offset, size, left;
	unsigned char *pack, c;
	int type;

	/* Have we done this one already due to deltas based on it? */
	if (lookup_object(entry->sha1))
		return;

	offset = ntohl(entry->offset);
	if (offset >= pack_size)
		goto bad;

	pack = pack_base + offset;
	c = *pack++;
	offset++;
	type = (c >> 4) & 7;
	size = (c & 15);
	while (c & 0x80) {
		if (offset >= pack_size)
			goto bad;
		offset++;
		c = *pack++;
		size = (size << 7) + (c & 0x7f);
		
	}
	left = pack_size - offset;
	switch (type) {
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		unpack_non_delta_entry(entry, type, pack, size, left);
		return;
	case OBJ_DELTA:
		unpack_delta_entry(entry, pack+5, size, left);
		return;
	}
bad:
	die("corrupted pack file");
}

/*
 * We unpack from the end, older files first. Now, usually
 * there are deltas etc, so we'll not actually write the
 * objects in that order, but we might as well try..
 */
static void unpack_all(void)
{
	int i = nr_entries;

	while (--i >= 0) {
		struct pack_entry *entry = pack_list[i];
		unpack_entry(entry);
	}
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1 ; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!strcmp(arg, "-n")) {
				dry_run = 1;
				continue;
			}
			usage(unpack_usage);
		}
		if (base_name)
			usage(unpack_usage);
		base_name = arg;
	}
	if (!base_name)
		usage(unpack_usage);
	index_base = map_file("idx", &index_size);
	pack_base = map_file("pack", &pack_size);
	if (check_index() < 0)
		die("bad index file");
	unpack_all();
	return 0;
}
