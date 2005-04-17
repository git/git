#include "cache.h"
#include "revision.h"

/*
 * This is stupid. We could have much better heurstics, I bet.
 */
static int better(struct revision *new, struct revision *old)
{
	return new->date > old->date;
}

static struct revision *common_parent(struct revision *rev1, struct revision *rev2)
{
	int i;
	struct revision *best = NULL;

	mark_reachable(rev1, 1);
	mark_reachable(rev2, 2);
	for (i = 0; i < nr_revs ;i++) {
		struct revision *rev = revs[i];
		if ((rev->flags & 3) != 3)
			continue;
		if (!best) {
			best = rev;
			continue;
		}
		if (better(rev, best))
			best = rev;
	}
	return best;
}

int main(int argc, char **argv)
{
	unsigned char rev1[20], rev2[20];
	struct revision *common;

	if (argc != 3 || get_sha1_hex(argv[1], rev1) || get_sha1_hex(argv[2], rev2))
		usage("merge-base <commit1> <commit2>");

	/*
	 * We will eventually want to include a revision cache file
	 * that "rev-tree.c" has generated, since this is going to
	 * otherwise be quite expensive for big trees..
	 *
	 * That's some time off, though, and in the meantime we know
	 * that we have a solution to the eventual expense.
	 */
	parse_commit(rev1);
	parse_commit(rev2);

	common = common_parent(lookup_rev(rev1), lookup_rev(rev2));
	if (!common)
		die("no common parent found");
	printf("%s\n", sha1_to_hex(common->sha1));
	return 0;
}
