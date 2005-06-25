#include "cache.h"

static int nr_entries;
static const char *base_name;
static const char unpack_usage[] = "git-unpack-objects basename";

struct pack_entry {
	unsigned int offset;
	unsigned char sha1[20];
};

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
		printf("idx_size=%d, expected %d (%d)\n", idx_size, 4*256 + nr * 24, nr);
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

int main(int argc, char **argv)
{
	int i;
	unsigned long idx_size, pack_size;
	void *index, *pack;

	for (i = 1 ; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg == '-') {
			/* Maybe we'll have some flags here some day.. */
			usage(unpack_usage);
		}
		if (base_name)
			usage(unpack_usage);
		base_name = arg;
	}
	if (!base_name)
		usage(unpack_usage);
	index = map_file("idx", &idx_size);
	pack = map_file("pack", &pack_size);
	if (check_index(index, idx_size) < 0)
		die("bad index file");
	return 0;
}
