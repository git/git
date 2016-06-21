#include <stdlib.h>
#include "cache.h"
#include "commit.h"

static struct commit *common_ancestor(struct commit *rev1, struct commit *rev2)
{
	struct commit_list *list = NULL;
	struct commit_list *result = NULL;

	if (rev1 == rev2)
		return rev1;

	parse_commit(rev1);
	parse_commit(rev2);

	rev1->object.flags |= 1;
	rev2->object.flags |= 2;
	insert_by_date(rev1, &list);
	insert_by_date(rev2, &list);

	while (list) {
		struct commit *commit = list->item;
		struct commit_list *tmp = list, *parents;
		int flags = commit->object.flags & 3;

		list = list->next;
		free(tmp);
		switch (flags) {
		case 3:
			insert_by_date(commit, &result);
			continue;
		case 0:
			die("git-merge-base: commit without either parent?");
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
		return NULL;
	return result->item;
}

int main(int argc, char **argv)
{
	struct commit *rev1, *rev2, *ret;
	unsigned char rev1key[20], rev2key[20];

	if (argc != 3 ||
	    get_sha1(argv[1], rev1key) ||
	    get_sha1(argv[2], rev2key)) {
		usage("git-merge-base <commit-id> <commit-id>");
	}
	rev1 = lookup_commit_reference(rev1key);
	rev2 = lookup_commit_reference(rev2key);
	if (!rev1 || !rev2)
		return 1;
	ret = common_ancestor(rev1, rev2);
	if (!ret)
		return 1;
	printf("%s\n", sha1_to_hex(ret->object.sha1));
	return 0;
}
