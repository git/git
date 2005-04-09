#include "cache.h"

struct tree_entry;

static struct tree_entry *update_tree_entry(void **bufp, unsigned long *sizep)
{
	void *buf = *bufp;
	unsigned long size = *sizep;
	int len = strlen(buf) + 1 + 20;

	if (size < len)
		usage("corrupt tree file");
	*bufp = buf + len;
	*sizep = size - len;
	return buf;
}

static const unsigned char *extract(void *tree, unsigned long size, const char **pathp, unsigned int *modep)
{
	int len = strlen(tree)+1;
	const unsigned char *sha1 = tree + len;
	const char *path = strchr(tree, ' ');

	if (!path || size < len + 20 || sscanf(tree, "%o", modep) != 1)
		usage("corrupt tree file");
	*pathp = path+1;
	return sha1;
}

static void show_file(const char *prefix, void *tree, unsigned long size)
{
	unsigned mode;
	const char *path;
	const unsigned char *sha1 = extract(tree, size, &path, &mode);
	printf("%s%o %s %s%c", prefix, mode, sha1_to_hex(sha1), path, 0);
}

static int compare_tree_entry(void *tree1, unsigned long size1, void *tree2, unsigned long size2)
{
	unsigned mode1, mode2;
	const char *path1, *path2;
	const unsigned char *sha1, *sha2;
	int cmp;

	sha1 = extract(tree1, size1, &path1, &mode1);
	sha2 = extract(tree2, size2, &path2, &mode2);

	cmp = cache_name_compare(path1, strlen(path1), path2, strlen(path2));
	if (cmp < 0) {
		show_file("-", tree1, size1);
		return -1;
	}
	if (cmp > 0) {
		show_file("+", tree2, size2);
		return 1;
	}
	if (!memcmp(sha1, sha2, 20) && mode1 == mode2)
		return 0;
	show_file("<", tree1, size1);
	show_file(">", tree2, size2);
	return 0;
}

static int diff_tree(void *tree1, unsigned long size1, void *tree2, unsigned long size2)
{
	while (size1 | size2) {
		if (!size1) {
			show_file("+", tree2, size2);
			update_tree_entry(&tree2, &size2);
			continue;
		}
		if (!size2) {
			show_file("-", tree1, size1);
			update_tree_entry(&tree1, &size1);
			continue;
		}
		switch (compare_tree_entry(tree1, size1, tree2, size2)) {
		case -1:
			update_tree_entry(&tree1, &size1);
			continue;
		case 0:
			update_tree_entry(&tree1, &size1);
			/* Fallthrough */
		case 1:
			update_tree_entry(&tree2, &size2);
			continue;
		}
		usage("diff-tree: internal error");
	}
	return 0;
}

int main(int argc, char **argv)
{
	unsigned char old[20], new[20];
	void *tree1, *tree2;
	unsigned long size1, size2;
	char type[20];

	if (argc != 3 || get_sha1_hex(argv[1], old) || get_sha1_hex(argv[2], new))
		usage("diff-tree <tree sha1> <tree sha1>");
	tree1 = read_sha1_file(old, type, &size1);
	if (!tree1 || strcmp(type, "tree"))
		usage("unable to read source tree");
	tree2 = read_sha1_file(new, type, &size2);
	if (!tree2 || strcmp(type, "tree"))
		usage("unable to read destination tree");
	return diff_tree(tree1, size1, tree2, size2);
}
