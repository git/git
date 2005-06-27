#include <ctype.h>
#include "cache.h"
#include "object.h"
#include "delta.h"
#include "csum-file.h"

static const char pack_usage[] = "git-pack-objects [--window=N] [--depth=N] base-name < object-list";

enum object_type {
	OBJ_NONE,
	OBJ_COMMIT,
	OBJ_TREE,
	OBJ_BLOB,
	OBJ_DELTA
};

struct object_entry {
	unsigned char sha1[20];
	unsigned long size;
	unsigned long offset;
	unsigned int depth;
	unsigned int hash;
	enum object_type type;
	unsigned long delta_size;
	struct object_entry *delta;
};

static struct object_entry **sorted_by_sha, **sorted_by_type;
static struct object_entry *objects = NULL;
static int nr_objects = 0, nr_alloc = 0;
static const char *base_name;
static unsigned char pack_file_sha1[20];

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

static unsigned long write_object(struct sha1file *f, struct object_entry *entry)
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
	sha1write(f, header, hdrlen);
	datalen = sha1write_compressed(f, buf, size);
	free(buf);
	return hdrlen + datalen;
}

static void write_pack_file(void)
{
	int i;
	struct sha1file *f = sha1create("%s.%s", base_name, "pack");
	unsigned long offset = 0;
	unsigned long mb;

	for (i = 0; i < nr_objects; i++) {
		struct object_entry *entry = objects + i;
		entry->offset = offset;
		offset += write_object(f, entry);
	}
	sha1close(f, pack_file_sha1, 1);
	mb = offset >> 20;
	offset &= 0xfffff;
}

static void write_index_file(void)
{
	int i;
	struct sha1file *f = sha1create("%s.%s", base_name, "idx");
	struct object_entry **list = sorted_by_sha;
	struct object_entry **last = list + nr_objects;
	unsigned int array[256];

	/*
	 * Write the first-level table (the list is sorted,
	 * but we use a 256-entry lookup to be able to avoid
	 * having to do eight extra binary search iterations).
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
	sha1write(f, array, 256 * sizeof(int));

	/*
	 * Write the actual SHA1 entries..
	 */
	list = sorted_by_sha;
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *entry = *list++;
		unsigned int offset = htonl(entry->offset);
		sha1write(f, &offset, 4);
		sha1write(f, entry->sha1, 20);
	}
	sha1write(f, pack_file_sha1, 20);
	sha1close(f, NULL, 1);
}

static void add_object_entry(unsigned char *sha1, unsigned int hash)
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
	entry->hash = hash;
	nr_objects = idx+1;
}

static void check_object(struct object_entry *entry)
{
	char type[20];

	if (!sha1_object_info(entry->sha1, type, &entry->size)) {
		if (!strcmp(type, "commit")) {
			entry->type = OBJ_COMMIT;
		} else if (!strcmp(type, "tree")) {
			entry->type = OBJ_TREE;
		} else if (!strcmp(type, "blob")) {
			entry->type = OBJ_BLOB;
		} else
			die("unable to pack object %s of type %s",
			    sha1_to_hex(entry->sha1), type);
	}
	else
		die("unable to get type of object %s",
		    sha1_to_hex(entry->sha1));
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
	if (a->hash < b->hash)
		return -1;
	if (a->hash > b->hash)
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
 * We search for deltas _backwards_ in a list sorted by type and
 * by size, so that we see progressively smaller and smaller files.
 * That's because we prefer deltas to be from the bigger file
 * to the smaller - deletes are potentially cheaper, but perhaps
 * more importantly, the bigger file is likely the more recent
 * one.
 */
static int try_delta(struct unpacked *cur, struct unpacked *old, unsigned max_depth)
{
	struct object_entry *cur_entry = cur->entry;
	struct object_entry *old_entry = old->entry;
	unsigned long size, oldsize, delta_size, sizediff;
	long max_size;
	void *delta_buf;

	/* Don't bother doing diffs between different types */
	if (cur_entry->type != old_entry->type)
		return -1;

	size = cur_entry->size;
	if (size < 50)
		return -1;
	oldsize = old_entry->size;
	sizediff = oldsize > size ? oldsize - size : size - oldsize;
	if (sizediff > size / 8)
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
	if (sizediff >= max_size)
		return -1;
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
	int i, idx;
	unsigned int array_size = window * sizeof(struct unpacked);
	struct unpacked *array = xmalloc(array_size);

	memset(array, 0, array_size);
	i = nr_objects;
	idx = 0;
	while (--i >= 0) {
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
		idx++;
		if (idx >= window)
			idx = 0;
	}
}

int main(int argc, char **argv)
{
	char line[PATH_MAX + 20];
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
		unsigned int hash;
		char *p;
		unsigned char sha1[20];

		if (get_sha1_hex(line, sha1))
			die("expected sha1, got garbage");
		hash = 0;
		p = line+40;
		while (*p) {
			unsigned char c = *p++;
			if (isspace(c))
				continue;
			hash = hash * 11 + c;
		}
		add_object_entry(sha1, hash);
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
