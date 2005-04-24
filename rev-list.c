#include "cache.h"
#include "commit.h"

int main(int argc, char **argv)
{
	unsigned char sha1[20];
	struct commit_list *list = NULL;
	struct commit *commit;

	if (argc != 2 || get_sha1_hex(argv[1], sha1))
		usage("rev-list <commit-id>");

	commit = lookup_commit(sha1);
	if (!commit || parse_commit(commit) < 0)
		die("bad commit object");

	commit_list_insert(commit, &list);
	do {
		struct commit *commit = pop_most_recent_commit(&list, 0x1);
		printf("%s\n", sha1_to_hex(commit->object.sha1));
	} while (list);
	return 0;
}
