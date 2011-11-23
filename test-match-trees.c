#include "cache.h"
#include "tree.h"

int main(int ac, char **av)
{
	unsigned char hash1[20], hash2[20], shifted[20];
	struct tree *one, *two;

	if (get_sha1(av[1], hash1))
		die("cannot parse %s as an object name", av[1]);
	if (get_sha1(av[2], hash2))
		die("cannot parse %s as an object name", av[2]);
	one = parse_tree_indirect(hash1);
	if (!one)
		die("not a treeish %s", av[1]);
	two = parse_tree_indirect(hash2);
	if (!two)
		die("not a treeish %s", av[2]);

	shift_tree(one->object.sha1, two->object.sha1, shifted, -1);
	printf("shifted: %s\n", sha1_to_hex(shifted));

	exit(0);
}
