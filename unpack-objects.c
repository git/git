#include "cache.h"
#include "object.h"

static int dry_run;
static int nr_entries;
static const char *base_name;
static const char unpack_usage[] = "git-unpack-objects basename";

struct pack_entry {
	unsigned int offset;
	unsigned char sha1[20];
};

static void *pack_base;
static unsigned long pack_size;

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

static int check_index(void *index, unsigned long idx_size)
{
	unsigned int *array = index;
	unsigned int nr;
	int i;

	if (idx_size < 4*256)
		return error("index file too small");
	nr = 0;
	for (i = 0; i < 256; i++) {
		unsigned int n = ntohl(array[i]);
		if (n < nr)
			return error("non-monotonic index");
		nr = n;
	}
	if (idx_size != 4*256 + nr * 24) {
		printf("idx_size=%lu, expected %u (%u)\n", idx_size, 4*256 + nr * 24, nr);
		return error("wrong index file size");
	}

	nr_entries = nr;
	pack_list = xmalloc(nr * sizeof(struct pack_entry *));
	for (i = 0; i < nr; i++)
		pack_list[i] = index + 4*256 + i*24;

	qsort(pack_list, nr, sizeof(*pack_list), sort_by_offset);

	printf("%d entries\n", nr);
	return 0;
}

static void unpack_entry(struct pack_entry *entry)
{
	unsigned long size;
	unsigned long offset;
	unsigned char *pack;

	/* Have we done this one already due to deltas based on it? */
	if (lookup_object(entry->sha1))
		return;

	offset = ntohl(entry->offset);
	if (offset > pack_size - 5)
		die("object offset outside of pack file");
	pack = pack_base + offset;
	offset = pack_size - offset;
	switch (*pack) {
	case 'C': case 'T': case 'B':
		size = (pack[1] << 24) + (pack[2] << 16) + (pack[3] << 8) + pack[4];
		printf("%s %c %lu\n", sha1_to_hex(entry->sha1), *pack, size);
		break;
	case 'D':
		printf("%s D", sha1_to_hex(entry->sha1));
		printf(" %s\n", sha1_to_hex(pack+1));
		break;
	default:
		die("corrupted pack file");
	}
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
	unsigned long idx_size;
	void *index;

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
	index = map_file("idx", &idx_size);
	pack_base = map_file("pack", &pack_size);
	if (check_index(index, idx_size) < 0)
		die("bad index file");
	unpack_all();
	return 0;
}
