#include "cache.h"

#include <sys/types.h>
#include <dirent.h>

struct needs {
	unsigned char parent[20];
	unsigned char needs[20];
	char tag[10];
};

struct seen {
	unsigned char sha1[20];
	char tag[10];
	unsigned needed;
};

static struct needs *needs;
static struct seen *seen;

static int nr_seen, alloc_seen, nr_needs, alloc_needs;

/*
 * These two functions build up a graph in memory about
 * what objects we've referenced, and found, and types..
 */
static int compare_seen(const void *s1, const void *s2)
{
	return memcmp(s1, s2, 20);
}

static int lookup_seen(unsigned char *sha1, char *tag)
{
	int first = 0, last = nr_seen;

	while (last > first) {
		int next = (last + first) / 2;
		struct seen *s = seen + next;
		int cmp = memcmp(sha1, s->sha1, 20);

		if (cmp < 0) {
			last = next;
			continue;
		}
		if (cmp > 0) {
			first = next+1;
			continue;
		}
		if (strcmp(tag, s->tag))
			break;
		s->needed++;
		return 1;
	}
	return 0;
}

static void check_connectivity(void)
{
	int i;

	/* Sort the "seen" tags for quicker lookup */
	qsort(seen, nr_seen, sizeof(struct seen), compare_seen);

	/* Look up all the requirements, warn about missing objects.. */
	for (i = 0; i < nr_needs; i++) {
		struct needs *n = needs + i;
		char hex[60];

		if (lookup_seen(n->needs, n->tag))
			continue;
		strcpy(hex, sha1_to_hex(n->parent));
		printf("missing %s: %s referenced by %s\n", n->tag, sha1_to_hex(n->needs), hex);
	}

	/* Tell the user about things not referenced.. */
	for (i = 0; i < nr_seen; i++) {
		struct seen *s = seen + i;

		if (s->needed)
			continue;
		printf("unreferenced %s: %s\n", s->tag, sha1_to_hex(s->sha1));
	}
}

static void mark_needs_sha1(unsigned char *parent, const char * tag, unsigned char *child)
{
	struct needs *n;

	if (nr_needs == alloc_needs) {
		alloc_needs = alloc_nr(alloc_needs);
		needs = realloc(needs, alloc_needs*sizeof(struct needs));
	}
	n = needs + nr_needs;
	nr_needs++;
	memcpy(n->parent, parent, 20);
	memcpy(n->needs, child, 20);
	strncpy(n->tag, tag, sizeof(n->tag));
}

static int mark_sha1_seen(unsigned char *sha1, char *tag)
{
	struct seen *s;

	if (nr_seen == alloc_seen) {
		alloc_seen = alloc_nr(alloc_seen);
		seen = realloc(seen, alloc_seen*sizeof(struct seen));
	}
	s = seen + nr_seen;
	memset(s, 0, sizeof(*s));
	nr_seen++;
	memcpy(s->sha1, sha1, 20);
	strncpy(s->tag, tag, sizeof(s->tag));
	
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
		printf("root: %s\n", sha1_to_hex(sha1));
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
		fprintf(stderr, "missing sha1 directory '%s'", path);
		return -1;
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
	int i;
	char *sha1_dir;

	if (argc != 1)
		usage("fsck-cache");
	sha1_dir = getenv(DB_ENVIRONMENT) ? : DEFAULT_DB_ENVIRONMENT;
	for (i = 0; i < 256; i++) {
		static char dir[4096];
		sprintf(dir, "%s/%02x", sha1_dir, i);
		fsck_dir(i, dir);
	}
	check_connectivity();
	return 0;
}
