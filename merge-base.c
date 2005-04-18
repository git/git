#include <stdlib.h>
#include "cache.h"
#include "commit.h"

static struct commit *process_list(struct commit_list **list_p, int this_mark,
				   int other_mark)
{
	struct commit_list *parent, *temp;
	struct commit_list *posn = *list_p;
	*list_p = NULL;
	while (posn) {
		parse_commit(posn->item);
		if (posn->item->object.flags & this_mark) {
			/*
			  printf("%d already seen %s %x\n",
			  this_mark
			  sha1_to_hex(posn->parent->sha1),
			  posn->parent->flags);
			*/
			/* do nothing; this indicates that this side
			 * split and reformed, and we only need to
			 * mark it once.
			 */
		} else if (posn->item->object.flags & other_mark) {
			return posn->item;
		} else {
			/*
			  printf("%d based on %s\n",
			  this_mark,
			  sha1_to_hex(posn->parent->sha1));
			*/
			posn->item->object.flags |= this_mark;
			
			parent = posn->item->parents;
			while (parent) {
				temp = malloc(sizeof(struct commit_list));
				temp->next = *list_p;
				temp->item = parent->item;
				*list_p = temp;
				parent = parent->next;
			}
		}
		posn = posn->next;
	}
	return NULL;
}

struct commit *common_ancestor(struct commit *rev1, struct commit *rev2)
{
	struct commit_list *rev1list = malloc(sizeof(struct commit_list));
	struct commit_list *rev2list = malloc(sizeof(struct commit_list));

	rev1list->item = rev1;
	rev1list->next = NULL;

	rev2list->item = rev2;
	rev2list->next = NULL;

	while (rev1list || rev2list) {
		struct commit *ret;
		ret = process_list(&rev1list, 0x1, 0x2);
		if (ret) {
			/* XXXX free lists */
			return ret;
		}
		ret = process_list(&rev2list, 0x2, 0x1);
		if (ret) {
			/* XXXX free lists */
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
	    get_sha1_hex(argv[1], rev1key) ||
	    get_sha1_hex(argv[2], rev2key)) {
		usage("merge-base <commit-id> <commit-id>");
	}
	rev1 = lookup_commit(rev1key);
	rev2 = lookup_commit(rev2key);
	ret = common_ancestor(rev1, rev2);
	if (!ret)
		return 1;
	printf("%s\n", sha1_to_hex(ret->object.sha1));
	return 0;
}
