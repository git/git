#include "cache.h"

#include <sys/types.h>
#include <dirent.h>

/*
 * The low 16 bits of the "flags" field shows whether
 * a commit is part of the path to the root for that
 * parent.
 *
 * Bit 16 is an internal flag that we've seen the
 * definition for this rev, and not just seen it as
 * a parent target.
 */
#define MAX_COMMITS (16)
#define marked(rev)	((rev)->flags & 0xffff)
#define SEEN 0x10000
#define USED 0x20000
#define REACHABLE 0x40000

static int show_unreachable = 0;
static unsigned char head_sha1[20];

struct parent {
	struct revision *parent;
	struct parent *next;
};

struct revision {
	unsigned int flags;
	unsigned char sha1[20];
	unsigned long date;
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

static struct revision *add_relationship(struct revision *rev, unsigned char *needs)
{
	struct revision *parent_rev = lookup_rev(needs);
	struct parent **pp = &rev->parent, *p;

	while ((p = *pp) != NULL) {
		if (p->parent == parent_rev)
			return parent_rev;
		pp = &p->next;
	}

	p = malloc(sizeof(*p));
	p->parent = parent_rev;
	p->next = NULL;
	*pp = p;
	return parent_rev;
}

static void mark_reachable(struct revision *rev)
{
	struct parent *p = rev->parent;

	/* If we've been here already, don't bother */
	if (rev->flags & REACHABLE)
		return;
	rev->flags |= REACHABLE | USED;
	while (p) {
		mark_reachable(p->parent);
		p = p->next;
	}
}

static void check_connectivity(void)
{
	int i;

	/* Look up all the requirements, warn about missing objects.. */
	for (i = 0; i < nr_revs; i++) {
		struct revision *rev = revs[i];

		if (show_unreachable && !(rev->flags & REACHABLE)) {
			printf("unreachable %s\n", sha1_to_hex(rev->sha1));
			continue;
		}

		switch (rev->flags & (SEEN | USED)) {
		case 0:
			printf("bad %s\n", sha1_to_hex(rev->sha1));
			break;
		case USED:
			printf("missing %s\n", sha1_to_hex(rev->sha1));
			break;
		case SEEN:
			printf("dangling %s\n", sha1_to_hex(rev->sha1));
			break;
		}
	}
}

static void mark_needs_sha1(unsigned char *parent, const char * tag, unsigned char *child)
{
	struct revision * child_rev = add_relationship(lookup_rev(parent), child);
	child_rev->flags |= USED;
}

static int mark_sha1_seen(unsigned char *sha1, char *tag)
{
	struct revision *rev = lookup_rev(sha1);

	rev->flags |= SEEN;
	return 0;
}

static int fsck_tree(unsigned char *sha1, void *data, unsigned long size)
{
	int warn_old_tree = 1;

	while (size) {
		int len = 1+strlen(data);
		unsigned char *file_sha1 = data + len;
		char *path = strchr(data, ' ');
		unsigned int mode;
		if (size < len + 20 || !path || sscanf(data, "%o", &mode) != 1)
			return -1;

		/* Warn about trees that don't do the recursive thing.. */
		if (warn_old_tree && strchr(path, '/')) {
			fprintf(stderr, "warning: fsck-cache: tree %s has full pathnames in it\n", sha1_to_hex(sha1));
			warn_old_tree = 0;
		}

		data += len + 20;
		size -= len + 20;
		mark_needs_sha1(sha1, S_ISDIR(mode) ? "tree" : "blob", file_sha1);
	}
	return 0;
}

static int fsck_commit(unsigned char *sha1, void *data, unsigned long size)
{
	int parents;
	unsigned char tree_sha1[20];
	unsigned char parent_sha1[20];

	if (memcmp(data, "tree ", 5))
		return -1;
	if (get_sha1_hex(data + 5, tree_sha1) < 0)
		return -1;
	mark_needs_sha1(sha1, "tree", tree_sha1);
	data += 5 + 40 + 1;	/* "tree " + <hex sha1> + '\n' */
	parents = 0;
	while (!memcmp(data, "parent ", 7)) {
		if (get_sha1_hex(data + 7, parent_sha1) < 0)
			return -1;
		mark_needs_sha1(sha1, "commit", parent_sha1);
		data += 7 + 40 + 1; 	/* "parent " + <hex sha1> + '\n' */
		parents++;
	}
	if (!parents)
		printf("root %s\n", sha1_to_hex(sha1));
	return 0;
}

static int fsck_entry(unsigned char *sha1, char *tag, void *data, unsigned long size)
{
	if (!strcmp(tag, "blob")) {
		/* Nothing to check */;
	} else if (!strcmp(tag, "tree")) {
		if (fsck_tree(sha1, data, size) < 0)
			return -1;
	} else if (!strcmp(tag, "commit")) {
		if (fsck_commit(sha1, data, size) < 0)
			return -1;
	} else
		return -1;
	return mark_sha1_seen(sha1, tag);
}

static int fsck_name(char *hex)
{
	unsigned char sha1[20];
	if (!get_sha1_hex(hex, sha1)) {
		unsigned long mapsize;
		void *map = map_sha1_file(sha1, &mapsize);
		if (map) {
			char type[100];
			unsigned long size;
			void *buffer = NULL;
			if (!check_sha1_signature(sha1, map, mapsize))
				buffer = unpack_sha1_file(map, mapsize, type, &size);
			munmap(map, mapsize);
			if (buffer && !fsck_entry(sha1, type, buffer, size))
				return 0;
		}
	}
	return -1;
}

static int fsck_dir(int i, char *path)
{
	DIR *dir = opendir(path);
	struct dirent *de;

	if (!dir) {
		return error("missing sha1 directory '%s'", path);
	}

	while ((de = readdir(dir)) != NULL) {
		char name[100];
		int len = strlen(de->d_name);

		switch (len) {
		case 2:
			if (de->d_name[1] != '.')
				break;
		case 1:
			if (de->d_name[0] != '.')
				break;
			continue;
		case 38:
			sprintf(name, "%02x", i);
			memcpy(name+2, de->d_name, len+1);
			if (!fsck_name(name))
				continue;
		}
		fprintf(stderr, "bad sha1 file: %s/%s\n", path, de->d_name);
	}
	closedir(dir);
	return 0;
}

int main(int argc, char **argv)
{
	int i, heads;
	char *sha1_dir;

	sha1_dir = getenv(DB_ENVIRONMENT) ? : DEFAULT_DB_ENVIRONMENT;
	for (i = 0; i < 256; i++) {
		static char dir[4096];
		sprintf(dir, "%s/%02x", sha1_dir, i);
		fsck_dir(i, dir);
	}

	heads = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--unreachable")) {
			show_unreachable = 1;
			continue;
		}
		if (!get_sha1_hex(argv[i], head_sha1)) {
			mark_reachable(lookup_rev(head_sha1));
			heads++;
			continue;
		}
		error("fsck-cache [[--unreachable] <head-sha1>*]");
	}

	if (!heads) {
		if (show_unreachable) {
			fprintf(stderr, "unable to do reachability without a head\n");
			show_unreachable = 0; 
		}
		fprintf(stderr, "expect dangling commits - potential heads - due to lack of head information\n");
	}

	check_connectivity();
	return 0;
}
