#include "cache.h"
#include "commit.h"

/*
 * Show one commit
 */
static void show_commit(struct commit *commit)
{
	char cmdline[400];
	char hex[100];

	strcpy(hex, sha1_to_hex(commit->object.sha1));
	printf("Id: %s\n", hex);
	fflush(NULL);
	sprintf(cmdline, "git-cat-file commit %s", hex);
	system(cmdline);
	if (commit->parents) {
		char *against = sha1_to_hex(commit->parents->item->object.sha1);
		printf("\n\n======== diff against %s ========\n", against);
		fflush(NULL);
		sprintf(cmdline, "git-diff-tree -p %s %s", against, hex);
		system(cmdline);
	}
	printf("======== end ========\n\n");
}

/*
 * Show all unseen commits, depth-first
 */
static void show_unseen(struct commit *top)
{
	struct commit_list *parents;

	if (top->object.flags & 2)
		return;
	top->object.flags |= 2;
	parents = top->parents;
	while (parents) {
		show_unseen(parents->item);
		parents = parents->next;
	}
	show_commit(top);
}

static void export(struct commit *top, struct commit *base)
{
	mark_reachable(&top->object, 1);
	if (base)
		mark_reachable(&base->object, 2);
	show_unseen(top);
}

static struct commit *get_commit(unsigned char *sha1)
{
	struct commit *commit = lookup_commit(sha1);
	if (!commit->object.parsed) {
		struct commit_list *parents;

		if (parse_commit(commit) < 0)
			die("unable to parse commit %s", sha1_to_hex(sha1));
		parents = commit->parents;
		while (parents) {
			get_commit(parents->item->object.sha1);
			parents = parents->next;
		}
	}
	return commit;
}

int main(int argc, char **argv)
{
	unsigned char base_sha1[20];
	unsigned char top_sha1[20];

	if (argc < 2 || argc > 4 ||
	    get_sha1(argv[1], top_sha1) ||
	    (argc == 3 && get_sha1(argv[2], base_sha1)))
		usage("git-export top [base]");
	export(get_commit(top_sha1), argc==3 ? get_commit(base_sha1) : NULL);
	return 0;
}
