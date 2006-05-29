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
 *
 *
 * Another pathological example how this thing used to fail to mark an
 * ancestor of a merge base as UNINTERESTING before we introduced the
 * postprocessing phase (mark_reachable_commits).
 *
 *		  2
 *		  H
 *	    1    / \
 *	    G   A   \
 *	    |\ /     \ 
 *	    | B       \
 *	    |  \       \
 *	     \  C       F
 *	      \  \     / 
 *	       \  D   /   
 *		\ |  /
 *		 \| /
 *		  E
 *
 *	 list			A B C D E F G H
 *	 G1 H2			- - - - - - 1 2
 *	 H2 E1 B1		- 1 - - 1 - 1 2
 *	 F2 E1 B1 A2		2 1 - - 1 2 1 2
 *	 E3 B1 A2		2 1 - - 3 2 1 2
 *	 B1 A2			2 1 - - 3 2 1 2
 *	 C1 A2			2 1 1 - 3 2 1 2
 *	 D1 A2			2 1 1 1 3 2 1 2
 *	 A2			2 1 1 1 3 2 1 2
 *	 B3			2 3 1 1 3 2 1 2
 *	 C7			2 3 7 1 3 2 1 2
 *
 * At this point, unfortunately, everybody in the list is
 * uninteresting, so we fail to complete the following two
 * steps to fully marking uninteresting commits.
 *
 *	 D7			2 3 7 7 3 2 1 2
 *	 E7			2 3 7 7 7 2 1 2
 *
 * and we ended up showing E as an interesting merge base.
 * The postprocessing phase re-injects C and continues traversal
 * to contaminate D and E.
 */

static int show_all = 0;

static void mark_reachable_commits(struct commit_list *result,
				   struct commit_list *list)
{
	struct commit_list *tmp;

	/*
	 * Postprocess to fully contaminate the well.
	 */
	for (tmp = result; tmp; tmp = tmp->next) {
		struct commit *c = tmp->item;
		/* Reinject uninteresting ones to list,
		 * so we can scan their parents.
		 */
		if (c->object.flags & UNINTERESTING)
			commit_list_insert(c, &list);
	}
	while (list) {
		struct commit *c = list->item;
		struct commit_list *parents;

		tmp = list;
		list = list->next;
		free(tmp);

		/* Anything taken out of the list is uninteresting, so
		 * mark all its parents uninteresting.  We do not
		 * parse new ones (we already parsed all the relevant
		 * ones).
		 */
		parents = c->parents;
		while (parents) {
			struct commit *p = parents->item;
			parents = parents->next;
			if (!(p->object.flags & UNINTERESTING)) {
				p->object.flags |= UNINTERESTING;
				commit_list_insert(p, &list);
			}
		}
	}
}

static int merge_base(struct commit *rev1, struct commit *rev2)
{
	struct commit_list *list = NULL;
	struct commit_list *result = NULL;
	struct commit_list *tmp = NULL;

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
		struct commit_list *parents;
		int flags = commit->object.flags & 7;

		tmp = list;
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

	if (result->next && list)
		mark_reachable_commits(result, list);

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

	setup_git_directory();
	git_config(git_default_config);

	while (1 < argc && argv[1][0] == '-') {
		char *arg = argv[1];
		if (!strcmp(arg, "-a") || !strcmp(arg, "--all"))
			show_all = 1;
		else
			usage(merge_base_usage);
		argc--; argv++;
	}
	if (argc != 3)
		usage(merge_base_usage);
	if (get_sha1(argv[1], rev1key))
		die("Not a valid object name %s", argv[1]);
	if (get_sha1(argv[2], rev2key))
		die("Not a valid object name %s", argv[2]);
	rev1 = lookup_commit_reference(rev1key);
	rev2 = lookup_commit_reference(rev2key);
	if (!rev1 || !rev2)
		return 1;
	return merge_base(rev1, rev2);
}
