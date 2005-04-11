#include "cache.h"

#define SEEN 1

struct parent {
	struct revision *parent;
	struct parent *next;
};

struct revision {
	unsigned int flags;
	unsigned char sha1[20];
	struct parent *parent;
};

static struct revision **revs;
static int nr_revs, rev_allocs;

static int find_rev(unsigned char *sha1)
{
	int first = 0, last = nr_revs;

	while (first < last) {
		int next = (first + last) / 2;
		struct revision *rev = revs[next];
		int cmp;

		cmp = memcmp(sha1, rev->sha1, 20);
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

static struct revision *lookup_rev(unsigned char *sha1)
{
	int pos = find_rev(sha1);
	struct revision *n;

	if (pos >= 0)
		return revs[pos];
	
	pos = -pos-1;

	if (rev_allocs == nr_revs) {
		rev_allocs = alloc_nr(rev_allocs);
		revs = realloc(revs, rev_allocs * sizeof(struct revision *));
	}
	n = malloc(sizeof(struct revision));

	n->flags = 0;
	memcpy(n->sha1, sha1, 20);
	n->parent = NULL;

	/* Insert it into the right place */
	memmove(revs + pos + 1, revs + pos, (nr_revs - pos) * sizeof(struct revision *));
	revs[pos] = n;
	nr_revs++;

	return n;
}

static int add_relationship(struct revision *rev, unsigned char *parent_sha)
{
	struct revision *parent_rev = lookup_rev(parent_sha);
	struct parent **pp = &rev->parent, *p;

	while ((p = *pp) != NULL) {
		if (p->parent == parent_rev)
			return 0;
		pp = &p->next;
	}

	p = malloc(sizeof(*p));
	p->parent = parent_rev;
	p->next = NULL;
	*pp = p;
	return 1;
}

static int parse_commit(unsigned char *sha1)
{
	struct revision *rev = lookup_rev(sha1);

	if (!(rev->flags & SEEN)) {
		void *buffer;
		unsigned long size;
		char type[20];
		unsigned char parent[20];

		rev->flags |= SEEN;
		buffer = read_sha1_file(sha1, type, &size);
		if (!buffer || strcmp(type, "commit"))
			return -1;
		buffer += 46; /* "tree " + "hex sha1" + "\n" */
		while (!memcmp(buffer, "parent ", 7) && !get_sha1_hex(buffer+7, parent)) {
			add_relationship(rev, parent);
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
		add_relationship(lookup_rev(sha1), parent);
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
	for (i = 0; i < nr_revs; i++) {
		struct revision *rev = revs[i];
		struct parent *p;

		printf("%s", sha1_to_hex(rev->sha1));
		p = rev->parent;
		while (p) {
			printf(" %s", sha1_to_hex(p->parent->sha1));
			p = p->next;
		}
		printf("\n");
	}
	return 0;
}
