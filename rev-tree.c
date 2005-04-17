#define _XOPEN_SOURCE /* glibc2 needs this */
#define _BSD_SOURCE /* for tm.tm_gmtoff */
#include <time.h>
#include <ctype.h>

#include "cache.h"
#include "revision.h"

/*
 * revision.h leaves the low 16 bits of the "flags" field of the
 * revision data structure unused. We use it for a "reachable from
 * this commit <N>" bitmask.
 */
#define MAX_COMMITS 16

static int show_edges = 0;
static int basemask = 0;

static void read_cache_file(const char *path)
{
	FILE *file = fopen(path, "r");
	char line[500];

	if (!file)
		die("bad revtree cache file (%s)", path);

	while (fgets(line, sizeof(line), file)) {
		unsigned long date;
		unsigned char sha1[20];
		struct revision *rev;
		const char *buf;

		if (sscanf(line, "%lu", &date) != 1)
			break;
		buf = strchr(line, ' ');
		if (!buf)
			break;
		if (get_sha1_hex(buf+1, sha1))
			break;
		rev = lookup_rev(sha1);
		rev->flags |= SEEN;
		rev->date = date;

		/* parents? */
		while ((buf = strchr(buf+1, ' ')) != NULL) {
			unsigned char parent[20];
			if (get_sha1_hex(buf + 1, parent))
				break;
			add_relationship(rev, parent);
		}
	}
	fclose(file);
}

/*
 * Some revisions are less interesting than others.
 *
 * For example, if we use a cache-file, that one may contain
 * revisions that were never used. They are never interesting.
 *
 * And sometimes we're only interested in "edge" commits, ie
 * places where the marking changes between parent and child.
 */
static int interesting(struct revision *rev)
{
	unsigned mask = marked(rev);

	if (!mask)
		return 0;
	if (show_edges) {
		struct parent *p = rev->parent;
		while (p) {
			if (mask != marked(p->parent))
				return 1;
			p = p->next;
		}
		return 0;
	}
	if (mask & basemask)
		return 0;

	return 1;
}

/*
 * Usage: rev-tree [--edges] [--cache <cache-file>] <commit-id> [<commit-id2>]
 *
 * The cache-file can be quite important for big trees. This is an
 * expensive operation if you have to walk the whole chain of
 * parents in a tree with a long revision history.
 */
int main(int argc, char **argv)
{
	int i;
	int nr = 0;
	unsigned char sha1[MAX_COMMITS][20];

	/*
	 * First - pick up all the revisions we can (both from
	 * caches and from commit file chains).
	 */
	for (i = 1; i < argc ; i++) {
		char *arg = argv[i];

		if (!strcmp(arg, "--cache")) {
			read_cache_file(argv[2]);
			i++;
			continue;
		}

		if (!strcmp(arg, "--edges")) {
			show_edges = 1;
			continue;
		}

		if (arg[0] == '^') {
			arg++;
			basemask |= 1<<nr;
		}
		if (nr >= MAX_COMMITS || get_sha1_hex(arg, sha1[nr]))
			usage("rev-tree [--edges] [--cache <cache-file>] <commit-id> [<commit-id>]");
		parse_commit(sha1[nr]);
		nr++;
	}

	/*
	 * Now we have the maximal tree. Walk the different sha files back to the root.
	 */
	for (i = 0; i < nr; i++)
		mark_reachable(lookup_rev(sha1[i]), 1 << i);

	/*
	 * Now print out the results..
	 */
	for (i = 0; i < nr_revs; i++) {
		struct revision *rev = revs[i];
		struct parent *p;

		if (!interesting(rev))
			continue;

		printf("%lu %s:%d", rev->date, sha1_to_hex(rev->sha1), marked(rev));
		p = rev->parent;
		while (p) {
			printf(" %s:%d", sha1_to_hex(p->parent->sha1), marked(p->parent));
			p = p->next;
		}
		printf("\n");
	}
	return 0;
}
