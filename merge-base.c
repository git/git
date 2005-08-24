#include <stdlib.h>
#include "cache.h"
#include "commit.h"

#define PARENT1 1
#define PARENT2 2
#define UNINTERESTING 4

static struct commit *interesting(struct commit_list *list)
{
	while (list) {
		struct commit *commit = list->item;
		list = list->next;
		if (commit->object.flags & UNINTERESTING)
			continue;
		return commit;
	}
	return NULL;
}

/*
 * A pathological example of how this thing works.
 *
 * Suppose we had this commit graph, where chronologically
 * the timestamp on the commit are A <= B <= C <= D <= E <= F
 * and we are trying to figure out the merge base for E and F
 * commits.
 *
 *                  F
 *                 / \
 *            E   A   D
 *             \ /   /  
 *              B   /
 *               \ /
 *                C
 *
 * First we push E and F to list to be processed.  E gets bit 1
 * and F gets bit 2.  The list becomes:
 *
 *     list=F(2) E(1), result=empty
 *
 * Then we pop F, the newest commit, from the list.  Its flag is 2.
 * We scan its parents, mark them reachable from the side that F is
 * reachable from, and push them to the list:
 *
 *     list=E(1) D(2) A(2), result=empty
 *
 * Next pop E and do the same.
 *
 *     list=D(2) B(1) A(2), result=empty
 *
 * Next pop D and do the same.
 *
 *     list=C(2) B(1) A(2), result=empty
 *
 * Next pop C and do the same.
 *
 *     list=B(1) A(2), result=empty
 *
 * Now it is B's turn.  We mark its parent, C, reachable from B's side,
 * and push it to the list:
 *
 *     list=C(3) A(2), result=empty
 *
 * Now pop C and notice it has flags==3.  It is placed on the result list,
 * and the list now contains:
 *
 *     list=A(2), result=C(3)
 *
 * We pop A and do the same.
 * 
 *     list=B(3), result=C(3)
 *
 * Next, we pop B and something very interesting happens.  It has flags==3
 * so it is also placed on the result list, and its parents are marked
 * uninteresting, retroactively, and placed back on the list:
 *
 *    list=C(7), result=C(7) B(3)
 * 
 * Now, list does not have any interesting commit.  So we find the newest
 * commit from the result list that is not marked uninteresting.  Which is
 * commit B.
 */

static int show_all = 0;

static int merge_base(struct commit *rev1, struct commit *rev2)
{
	struct commit_list *list = NULL;
	struct commit_list *result = NULL;

	if (rev1 == rev2) {
		printf("%s\n", sha1_to_hex(rev1->object.sha1));
		return 0;
	}

	parse_commit(rev1);
	parse_commit(rev2);

	rev1->object.flags |= 1;
	rev2->object.flags |= 2;
	insert_by_date(rev1, &list);
	insert_by_date(rev2, &list);

	while (interesting(list)) {
		struct commit *commit = list->item;
		struct commit_list *tmp = list, *parents;
		int flags = commit->object.flags & 7;

		list = list->next;
		free(tmp);
		if (flags == 3) {
			insert_by_date(commit, &result);

			/* Mark parents of a found merge uninteresting */
			flags |= UNINTERESTING;
		}
		parents = commit->parents;
		while (parents) {
			struct commit *p = parents->item;
			parents = parents->next;
			if ((p->object.flags & flags) == flags)
				continue;
			parse_commit(p);
			p->object.flags |= flags;
			insert_by_date(p, &list);
		}
	}

	if (!result)
		return 1;

	while (result) {
		struct commit *commit = result->item;
		result = result->next;
		if (commit->object.flags & UNINTERESTING)
			continue;
		printf("%s\n", sha1_to_hex(commit->object.sha1));
		if (!show_all)
			return 0;
		commit->object.flags |= UNINTERESTING;
	}
	return 0;
}

static const char merge_base_usage[] =
"git-merge-base [--all] <commit-id> <commit-id>";

int main(int argc, char **argv)
{
	struct commit *rev1, *rev2;
	unsigned char rev1key[20], rev2key[20];

	while (1 < argc && argv[1][0] == '-') {
		char *arg = argv[1];
		if (!strcmp(arg, "-a") || !strcmp(arg, "--all"))
			show_all = 1;
		else
			usage(merge_base_usage);
		argc--; argv++;
	}
	if (argc != 3 ||
	    get_sha1(argv[1], rev1key) ||
	    get_sha1(argv[2], rev2key))
		usage(merge_base_usage);
	rev1 = lookup_commit_reference(rev1key);
	rev2 = lookup_commit_reference(rev2key);
	if (!rev1 || !rev2)
		return 1;
	return merge_base(rev1, rev2);
}
