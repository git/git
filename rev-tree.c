#include "cache.h"

struct relationship {
	unsigned char sha1[20];
	unsigned char parent[20];
};

static struct relationship **rels;
static int nr_rels, rel_allocs;

static int find_relationship(unsigned char *sha1, unsigned char *parent)
{
	int first = 0, last = nr_rels;

	while (first < last) {
		int next = (first + last) / 2;
		struct relationship *rel = rels[next];
		int cmp;

		cmp = memcmp(sha1, rel->sha1, 20);
		if (!cmp) {
			cmp = memcmp(parent, rel->parent, 20);
			if (!cmp)
				return next;
		}
		if (cmp < 0) {
			last = next;
			continue;
		}
		first = next+1;
	}
	return -first-1;
}

static int add_relationship(unsigned char *sha1, unsigned char *parent)
{
	struct relationship *n;
	int pos;

	pos = find_relationship(sha1, parent);
	if (pos >= 0)
		return 0;
	pos = -pos-1;

	if (rel_allocs == nr_rels) {
		rel_allocs = alloc_nr(rel_allocs);
		rels = realloc(rels, rel_allocs * sizeof(struct relationship *));
	}
	n = malloc(sizeof(struct relationship));
	
	memmove(rels + pos + 1, rels + pos, (nr_rels - pos) * sizeof(struct relationship *));
	rels[pos] = n;
	nr_rels++;
	memcpy(n->sha1, sha1, 20);
	memcpy(n->parent, parent, 20);
	return 1;
}

static int already_seen(unsigned char *sha1)
{
	static char null_sha[20];
	int pos = find_relationship(sha1, null_sha);

	if (pos < 0) 
		pos = -pos-1;
	if (pos < nr_rels && !memcmp(sha1, rels[pos]->sha1, 20))
		return 1;
	return 0;
}

static int parse_commit(unsigned char *sha1)
{
	if (!already_seen(sha1)) {
		void *buffer;
		unsigned long size;
		char type[20];
		unsigned char parent[20];

		buffer = read_sha1_file(sha1, type, &size);
		if (!buffer || strcmp(type, "commit"))
			return -1;
		buffer += 46; /* "tree " + "hex sha1" + "\n" */
		while (!memcmp(buffer, "parent ", 7) && !get_sha1_hex(buffer+7, parent)) {
			add_relationship(sha1, parent);
			parse_commit(parent);
			buffer += 48;	/* "parent " + "hex sha1" + "\n" */
		}
	}
	return 0;	
}

static void read_cache_file(const char *path)
{
	FILE *file = fopen(path, "r");
	char line[100];

	while (fgets(line, sizeof(line), file)) {
		unsigned char sha1[20], parent[20];
		if (get_sha1_hex(line, sha1) || get_sha1_hex(line + 41, parent))
			usage("bad rev-tree cache file %s", path);
		add_relationship(sha1, parent);
	}
	fclose(file);
}

/*
 * Usage: rev-tree [--cache <cache-file>] <commit-id>
 *
 * The cache-file can be quite important for big trees. This is an
 * expensive operation if you have to walk the whole chain of
 * parents in a tree with a long revision history.
 */
int main(int argc, char **argv)
{
	int i;
	unsigned char sha1[20];

	while (argc > 2) {
		if (!strcmp(argv[1], "--cache")) {
			read_cache_file(argv[2]);
			argv += 2;
			argc -= 2;
			continue;
		}
		usage("unknown option %s", argv[1]);
	}

	if (argc != 2 || get_sha1_hex(argv[1], sha1))
		usage("rev-tree [--cache <cache-file>] <commit-id>");
	parse_commit(sha1);
	for (i = 0; i < nr_rels; i++) {
		char parent[60];
		struct relationship *rel = rels[i];
		strcpy(parent, sha1_to_hex(rel->parent));
		printf("%s %s\n", sha1_to_hex(rel->sha1), parent);
	}
	return 0;
}
