#include <stdlib.h>
#include "cache.h"
#include "commit.h"

static struct commit *process_list(struct commit_list **list_p, int this_mark,
				   int other_mark)
{
	struct commit *item = (*list_p)->item;

	if (item->object.flags & other_mark) {
		return item;
	} else {
		pop_most_recent_commit(list_p, this_mark);
	}
	return NULL;
}

static struct commit *common_ancestor(struct commit *rev1, struct commit *rev2)
{
	struct commit_list *rev1list = NULL;
	struct commit_list *rev2list = NULL;

	commit_list_insert(rev1, &rev1list);
	rev1->object.flags |= 0x1;
	commit_list_insert(rev2, &rev2list);
	rev2->object.flags |= 0x2;

	parse_commit(rev1);
	parse_commit(rev2);

	while (rev1list || rev2list) {
		struct commit *ret;
		if (!rev1list) {
			// process 2
			ret = process_list(&rev2list, 0x2, 0x1);
		} else if (!rev2list) {
			// process 1
			ret = process_list(&rev1list, 0x1, 0x2);
		} else if (rev1list->item->date < rev2list->item->date) {
			// process 2
			ret = process_list(&rev2list, 0x2, 0x1);
		} else {
			// process 1
			ret = process_list(&rev1list, 0x1, 0x2);
		}
		if (ret) {
			free_commit_list(rev1list);
			free_commit_list(rev2list);
			return ret;
		}
	}
	return NULL;
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
	ret = common_ancestor(rev1, rev2);
	if (!ret)
		return 1;
	printf("%s\n", sha1_to_hex(ret->object.sha1));
	return 0;
}
