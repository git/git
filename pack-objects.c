#include "cache.h"
#include "object.h"
#include "delta.h"

static const char pack_usage[] = "git-pack-objects [--window=N] [--depth=N] base-name < object-list";

enum object_type {
	OBJ_NONE,
	OBJ_COMMIT,
	OBJ_TREE,
	OBJ_BLOB,
	OBJ_DELTA	// NOTE! This is _not_ the same as a "delta" object in the filesystem
};

struct object_entry {
	unsigned char sha1[20];
	unsigned long size;
	unsigned long offset;
	unsigned int depth;
	unsigned int flags;
	enum object_type type;
	unsigned long delta_size;
	struct object_entry *delta;
};

static struct object_entry **sorted_by_sha, **sorted_by_type;
static struct object_entry *objects = NULL;
static int nr_objects = 0, nr_alloc = 0;
static const char *base_name;

struct myfile {
	int fd;
	unsigned long chars;
	unsigned char buffer[8192];
};

static FILE *create_file(const char *suffix)
{
	static char filename[PATH_MAX];
	unsigned len;

	len = snprintf(filename, PATH_MAX, "%s.%s", base_name, suffix);
	if (len >= PATH_MAX)
		die("you wascally wabbit, you");
	return fopen(filename, "w");
}

static unsigned long fwrite_compressed(void *in, unsigned long size, FILE *f)
{
	z_stream stream;
	unsigned long maxsize;
	void *out;

	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, Z_DEFAULT_COMPRESSION);
	maxsize = deflateBound(&stream, size);
	out = xmalloc(maxsize);

	/* Compress it */
	stream.next_in = in;
	stream.avail_in = size;

	stream.next_out = out;
	stream.avail_out = maxsize;

	while (deflate(&stream, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&stream);

	size = stream.total_out;
	fwrite(out, size, 1, f);
	free(out);
	return size;
}

static void *delta_against(void *buf, unsigned long size, struct object_entry *entry)
{
	unsigned long othersize, delta_size;
	char type[10];
	void *otherbuf = read_sha1_file(entry->delta->sha1, type, &othersize);
	void *delta_buf;

	if (!otherbuf)
		die("unable to read %s", sha1_to_hex(entry->delta->sha1));
        delta_buf = diff_delta(otherbuf, othersize,
			       buf, size, &delta_size, ~0UL);
        if (!delta_buf || delta_size != entry->delta_size)
        	die("delta size changed");
        free(buf);
        free(otherbuf);
	return delta_buf;
}

static unsigned long write_object(FILE *f, struct object_entry *entry)
{
	unsigned long size;
	char type[10];
	void *buf = read_sha1_file(entry->sha1, type, &size);
	char header[25];
	unsigned hdrlen, datalen;

	if (!buf)
		die("unable to read %s", sha1_to_hex(entry->sha1));
	if (size != entry->size)
		die("object %s size inconsistency (%lu vs %lu)", sha1_to_hex(entry->sha1), size, entry->size);

	/*
	 * The object header is a byte of 'type' followed by four bytes of
	 * length, except for deltas that has the 20 bytes of delta sha
	 * instead.
	 */
	header[0] = ".CTB"[entry->type];
	hdrlen = 5;
	if (entry->delta) {
		header[0] = 'D';
		memcpy(header+5, entry->delta, 20);
		buf = delta_against(buf, size, entry);
		size = entry->delta_size;
		hdrlen = 25;
	}
	datalen = htonl(size);
	memcpy(header+1, &datalen, 4);
	fwrite(header, hdrlen, 1, f);
	datalen = fwrite_compressed(buf, size, f);
	free(buf);
	return hdrlen + datalen;
}

static void write_pack_file(void)
{
	int i;
	FILE *f = create_file("pack");
	unsigned long offset = 0;
	unsigned long mb;

	for (i = 0; i < nr_objects; i++) {
		struct object_entry *entry = objects + i;
		entry->offset = offset;
		offset += write_object(f, entry);
	}
	fclose(f);
	mb = offset >> 20;
	offset &= 0xfffff;
}

static void write_index_file(void)
{
	int i;
	FILE *f = create_file("idx");
	struct object_entry **list = sorted_by_sha;
	struct object_entry **last = list + nr_objects;
	unsigned int array[256];

	/*
	 * Write the first-level table (the list is sorted,
	 * but we use a 256-entry lookup to be able to avoid
	 * having to do eight extra binary search iterations)
	 */
	for (i = 0; i < 256; i++) {
		struct object_entry **next = list;
		while (next < last) {
			struct object_entry *entry = *next;
			if (entry->sha1[0] != i)
				break;
			next++;
		}
		array[i] = htonl(next - sorted_by_sha);
		list = next;
	}
	fwrite(array, 256, sizeof(int), f);

	/*
	 * Write the actual SHA1 entries..
	 */
	list = sorted_by_sha;
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *entry = *list++;
		unsigned int offset = htonl(entry->offset);
		fwrite(&offset, 4, 1, f);
		fwrite(entry->sha1, 20, 1, f);
	}
	fclose(f);
}

static void add_object_entry(unsigned char *sha1)
{
	unsigned int idx = nr_objects;
	struct object_entry *entry;

	if (idx >= nr_alloc) {
		unsigned int needed = (idx + 1024) * 3 / 2;
		objects = xrealloc(objects, needed * sizeof(*entry));
		nr_alloc = needed;
	}
	entry = objects + idx;
	memset(entry, 0, sizeof(*entry));
	memcpy(entry->sha1, sha1, 20);
	nr_objects = idx+1;
}

static void check_object(struct object_entry *entry)
{
	char buffer[128];
	char type[10];
	unsigned long mapsize;
	z_stream stream;
	void *map;

	map = map_sha1_file(entry->sha1, &mapsize);
	if (!map)
		die("unable to map %s", sha1_to_hex(entry->sha1));
	if (unpack_sha1_header(&stream, map, mapsize, buffer, sizeof(buffer)) < 0)
		die("unable to unpack %s header", sha1_to_hex(entry->sha1));
	munmap(map, mapsize);
	if (parse_sha1_header(buffer, type, &entry->size) < 0)
		die("unable to parse %s header", sha1_to_hex(entry->sha1));
	if (!strcmp(type, "commit")) {
		entry->type = OBJ_COMMIT;
	} else if (!strcmp(type, "tree")) {
		entry->type = OBJ_TREE;
	} else if (!strcmp(type, "blob")) {
		entry->type = OBJ_BLOB;
	} else
		die("unable to pack object %s of type %s", sha1_to_hex(entry->sha1), type);
}

static void get_object_details(void)
{
	int i;
	struct object_entry *entry = objects;

	for (i = 0; i < nr_objects; i++)
		check_object(entry++);
}

typedef int (*entry_sort_t)(const struct object_entry *, const struct object_entry *);

static entry_sort_t current_sort;

static int sort_comparator(const void *_a, const void *_b)
{
	struct object_entry *a = *(struct object_entry **)_a;
	struct object_entry *b = *(struct object_entry **)_b;
	return current_sort(a,b);
}

static struct object_entry **create_sorted_list(entry_sort_t sort)
{
	struct object_entry **list = xmalloc(nr_objects * sizeof(struct object_entry *));
	int i;

	for (i = 0; i < nr_objects; i++)
		list[i] = objects + i;
	current_sort = sort;
	qsort(list, nr_objects, sizeof(struct object_entry *), sort_comparator);
	return list;
}

static int sha1_sort(const struct object_entry *a, const struct object_entry *b)
{
	return memcmp(a->sha1, b->sha1, 20);
}

static int type_size_sort(const struct object_entry *a, const struct object_entry *b)
{
	if (a->type < b->type)
		return -1;
	if (a->type > b->type)
		return 1;
	if (a->size < b->size)
		return -1;
	if (a->size > b->size)
		return 1;
	return a < b ? -1 : (a > b);
}

struct unpacked {
	struct object_entry *entry;
	void *data;
};

/*
 * We search for deltas in a list sorted by type and by size, and
 * walk the "old" chain backwards in the list, so if the type
 * has changed or the size difference is too big, there's no point
 * in even continuing the walk, since the other old objects are
 * going to be even smaller or of a different type. So return -1
 * once we determine that there's no point even trying.
 */
static int try_delta(struct unpacked *cur, struct unpacked *old, unsigned max_depth)
{
	struct object_entry *cur_entry = cur->entry;
	struct object_entry *old_entry = old->entry;
	unsigned long size, oldsize, delta_size;
	long max_size;
	void *delta_buf;

	/* Don't bother doing diffs between different types */
	if (cur_entry->type != old_entry->type)
		return -1;

	/* Size is guaranteed to be larger than or equal to oldsize */
	size = cur_entry->size;
	if (size < 50)
		return -1;
	oldsize = old_entry->size;
	if (size - oldsize > oldsize / 4)
		return -1;
	if (old_entry->depth >= max_depth)
		return 0;

	/*
	 * NOTE!
	 *
	 * We always delta from the bigger to the smaller, since that's
	 * more space-efficient (deletes don't have to say _what_ they
	 * delete).
	 */
	max_size = size / 2 - 20;
	if (cur_entry->delta)
		max_size = cur_entry->delta_size-1;
	delta_buf = diff_delta(old->data, oldsize,
			       cur->data, size, &delta_size, max_size);
	if (!delta_buf)
		return 0;
	cur_entry->delta = old_entry;
	cur_entry->delta_size = delta_size;
	cur_entry->depth = old_entry->depth + 1;
	free(delta_buf);
	return 0;
}

static void find_deltas(struct object_entry **list, int window, int depth)
{
	unsigned int i;
	unsigned int array_size = window * sizeof(struct unpacked);
	struct unpacked *array = xmalloc(array_size);

	memset(array, 0, array_size);
	for (i = 0; i < nr_objects; i++) {
		unsigned int idx = i % window;
		struct object_entry *entry = list[i];
		struct unpacked *n = array + idx;
		unsigned long size;
		char type[10];
		int j;

		free(n->data);
		n->entry = entry;
		n->data = read_sha1_file(entry->sha1, type, &size);
		if (size != entry->size)
			die("object %s inconsistent object length (%lu vs %lu)", sha1_to_hex(entry->sha1), size, entry->size);
		j = window;
		while (--j > 0) {
			unsigned int other_idx = idx + j;
			struct unpacked *m;
			if (other_idx >= window)
				other_idx -= window;
			m = array + other_idx;
			if (!m->entry)
				break;
			if (try_delta(n, m, depth) < 0)
				break;
		}
	}
}

int main(int argc, char **argv)
{
	char line[128];
	int window = 10, depth = 10;
	int i;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			if (!strncmp("--window=", arg, 9)) {
				char *end;
				window = strtoul(arg+9, &end, 0);
				if (!arg[9] || *end)
					usage(pack_usage);
				continue;
			}
			if (!strncmp("--depth=", arg, 8)) {
				char *end;
				depth = strtoul(arg+8, &end, 0);
				if (!arg[8] || *end)
					usage(pack_usage);
				continue;
			}
			usage(pack_usage);
		}
		if (base_name)
			usage(pack_usage);
		base_name = arg;
	}

	if (!base_name)
		usage(pack_usage);

	while (fgets(line, sizeof(line), stdin) != NULL) {
		unsigned char sha1[20];
		if (get_sha1_hex(line, sha1))
			die("expected sha1, got garbage");
		add_object_entry(sha1);
	}
	get_object_details();

	printf("Packing %d objects\n", nr_objects);

	sorted_by_sha = create_sorted_list(sha1_sort);
	sorted_by_type = create_sorted_list(type_size_sort);
	if (window && depth)
		find_deltas(sorted_by_type, window+1, depth);

	write_pack_file();
	write_index_file();
	return 0;
}
