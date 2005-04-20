#include "cache.h"

static int cached_only = 0;
static int recursive = 0;
static int line_termination = '\n';

static int diff_cache(void *tree, unsigned long size, struct cache_entry **ac, int entries, const char *base);

static void update_tree_entry(void **bufp, unsigned long *sizep)
{
	void *buf = *bufp;
	unsigned long size = *sizep;
	int len = strlen(buf) + 1 + 20;

	if (size < len)
		die("corrupt tree file 1 (%s)", size);
	*bufp = buf + len;
	*sizep = size - len;
}

static const unsigned char *extract(void *tree, unsigned long size, const char **pathp, unsigned int *modep)
{
	int len = strlen(tree)+1;
	const unsigned char *sha1 = tree + len;
	const char *path = strchr(tree, ' ');

	if (!path || size < len + 20 || sscanf(tree, "%o", modep) != 1)
		die("corrupt tree file 2 (%d)", size);
	*pathp = path+1;
	return sha1;
}

static char *malloc_base(const char *base, const char *path, int pathlen)
{
	int baselen = strlen(base);
	char *newbase = malloc(baselen + pathlen + 2);
	memcpy(newbase, base, baselen);
	memcpy(newbase + baselen, path, pathlen);
	memcpy(newbase + baselen + pathlen, "/", 2);
	return newbase;
}

static void show_file(const char *prefix, const char *path, unsigned int mode, const unsigned char *sha1, const char *base);

/* A whole sub-tree went away or appeared */
static void show_tree(const char *prefix, void *tree, unsigned long size, const char *base)
{
	while (size) {
		const char *path;
		unsigned int mode;
		const unsigned char *sha1 = extract(tree, size, &path, &mode);
		
		show_file(prefix, path, mode, sha1, base);
		update_tree_entry(&tree, &size);
	}
}

/* A file entry went away or appeared */
static void show_file(const char *prefix, const char *path, unsigned int mode, const unsigned char *sha1, const char *base)
{
	if (recursive && S_ISDIR(mode)) {
		char type[20];
		unsigned long size;
		char *newbase = malloc_base(base, path, strlen(path));
		void *tree;

		tree = read_sha1_file(sha1, type, &size);
		if (!tree || strcmp(type, "tree"))
			die("corrupt tree sha %s", sha1_to_hex(sha1));

		show_tree(prefix, tree, size, newbase);
		
		free(tree);
		free(newbase);
		return;
	}

	printf("%s%o\t%s\t%s\t%s%s%c", prefix, mode,
	       S_ISDIR(mode) ? "tree" : "blob",
	       sha1_to_hex(sha1), base, path,
	       line_termination);
}

static int compare_tree_entry(const char *path1, unsigned int mode1, const unsigned char *sha1,
			      struct cache_entry **ac, int *entries, const char *base)
{
	int baselen = strlen(base);
	struct cache_entry *ce = *ac;
	const char *path2 = ce->name + baselen;
	unsigned int mode2 = ntohl(ce->ce_mode);
	const unsigned char *sha2 = ce->sha1;
	int cmp, pathlen1, pathlen2;
	char old_sha1_hex[50];

	pathlen1 = strlen(path1);
	pathlen2 = strlen(path2);
	cmp = cache_name_compare(path1, pathlen1, path2, pathlen2);
	if (cmp < 0) {
		if (S_ISDIR(mode1)) {
			char type[20];
			unsigned long size;
			void *tree = read_sha1_file(sha1, type, &size);
			char *newbase = malloc(baselen + 2 + pathlen1);

			memcpy(newbase, base, baselen);
			memcpy(newbase + baselen, path1, pathlen1);
			memcpy(newbase + baselen + pathlen1, "/", 2);
			if (!tree || strcmp(type, "tree"))
				die("unable to read tree object %s", sha1_to_hex(sha1));
			*entries = diff_cache(tree, size, ac, *entries, newbase);
			free(newbase);
			free(tree);
			return -1;
		}
		show_file("-", path1, mode1, sha1, base);
		return -1;
	}

	if (!cached_only) {
		static unsigned char no_sha1[20];
		int fd, changed;
		struct stat st;
		fd = open(ce->name, O_RDONLY);
		if (fd < 0 || fstat(fd, &st) < 0) {
			show_file("-", path1, mode1, sha1, base);
			return -1;
		}
		changed = cache_match_stat(ce, &st);
		close(fd);
		if (changed) {
			mode2 = st.st_mode;
			sha2 = no_sha1;
		}
	}

	if (cmp > 0) {
		show_file("+", path2, mode2, sha2, base);
		return 1;
	}
	if (!memcmp(sha1, sha2, 20) && mode1 == mode2)
		return 0;

	/*
	 * If the filemode has changed to/from a directory from/to a regular
	 * file, we need to consider it a remove and an add.
	 */
	if (S_ISDIR(mode1) || S_ISDIR(mode2)) {
		show_file("-", path1, mode1, sha1, base);
		show_file("+", path2, mode2, sha2, base);
		return 0;
	}

	strcpy(old_sha1_hex, sha1_to_hex(sha1));
	printf("*%o->%o\t%s\t%s->%s\t%s%s%c", mode1, mode2,
	       S_ISDIR(mode1) ? "tree" : "blob",
	       old_sha1_hex, sha1_to_hex(sha2), base, path1,
	       line_termination);
	return 0;
}

static int diff_cache(void *tree, unsigned long size, struct cache_entry **ac, int entries, const char *base)
{
	int baselen = strlen(base);

	for (;;) {
		struct cache_entry *ce;
		unsigned int mode;
		const char *path;
		const unsigned char *sha1;
		int left;

		/*
		 * No entries in the cache (with this base)?
		 * Output the tree contents.
		 */
		if (!entries || ce_namelen(ce = *ac) < baselen || memcmp(ce->name, base, baselen)) {
			if (!size)
				return entries;
			sha1 = extract(tree, size, &path, &mode);
			show_file("-", path, mode, sha1, base);
			update_tree_entry(&tree, &size);
			continue;
		}

		/*
		 * No entries in the tree? Output the cache contents
		 */
		if (!size) {
			show_file("+", ce->name, ntohl(ce->ce_mode), ce->sha1, "");
			ac++;
			entries--;
			continue;
		}

		sha1 = extract(tree, size, &path, &mode);
		left = entries;
		switch (compare_tree_entry(path, mode, sha1, ac, &left, base)) {
		case -1:
			update_tree_entry(&tree, &size);
			if (left < entries) {
				ac += (entries - left);
				entries = left;
			}
			continue;
		case 0:
			update_tree_entry(&tree, &size);
			/* Fallthrough */
		case 1:
			ac++;
			entries--;
			continue;
		}
		die("diff-cache: internal error");
	}
	return 0;
}

int main(int argc, char **argv)
{
	unsigned char tree_sha1[20];
	void *tree;
	unsigned long size;
	char type[20];

	read_cache();
	while (argc > 2) {
		char *arg = argv[1];
		argv++;
		argc--;
		if (!strcmp(arg, "-r")) {
			recursive = 1;
			continue;
		}
		if (!strcmp(arg, "-z")) {
			line_termination = '\0';
			continue;
		}
		if (!strcmp(arg, "--cached")) {
			cached_only = 1;
			continue;
		}
		usage("diff-cache [-r] [-z] <tree sha1>");
	}

	if (argc != 2 || get_sha1_hex(argv[1], tree_sha1))
		usage("diff-cache [-r] [-z] <tree sha1>");

	tree = read_sha1_file(tree_sha1, type, &size);
	if (!tree)
		die("bad tree object %s", argv[1]);

	/* We allow people to feed us a commit object, just because we're nice */
	if (!strcmp(type, "commit")) {
		/* tree sha1 is always at offset 5 ("tree ") */
		if (get_sha1_hex(tree + 5, tree_sha1))
			die("bad commit object %s", argv[1]);
		free(tree);
		tree = read_sha1_file(tree_sha1, type, &size);       
		if (!tree)
			die("unable to read tree object %s", sha1_to_hex(tree_sha1));
	}

	if (strcmp(type, "tree"))
		die("bad tree object %s (%s)", sha1_to_hex(tree_sha1), type);

	return diff_cache(tree, size, active_cache, active_nr, "");
}
