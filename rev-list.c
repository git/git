#include "cache.h"
#include "commit.h"

int main(int argc, char **argv)
{
	unsigned char sha1[20];
	struct commit_list *list = NULL;
	struct commit *commit;
	char *commit_arg = NULL;
	int i;
	unsigned long max_age = -1;
	unsigned long min_age = -1;
	int max_count = -1;

	for (i = 1 ; i < argc; i++) {
		char *arg = argv[i];

		if (!strncmp(arg, "--max-count=", 12)) {
			max_count = atoi(arg + 12);
		} else if (!strncmp(arg, "--max-age=", 10)) {
			max_age = atoi(arg + 10);
		} else if (!strncmp(arg, "--min-age=", 10)) {
			min_age = atoi(arg + 10);
		} else {
			commit_arg = arg;
		}
	}

	if (!commit_arg || get_sha1(commit_arg, sha1))
		usage("usage: rev-list [OPTION] commit-id\n"
		      "  --max-count=nr\n"
		      "  --max-age=epoch\n"
		      "  --min-age=epoch\n");

	commit = lookup_commit(sha1);
	if (!commit || parse_commit(commit) < 0)
		die("bad commit object");

	commit_list_insert(commit, &list);
	do {
		struct commit *commit = pop_most_recent_commit(&list, 0x1);

		if (min_age != -1 && (commit->date > min_age))
			continue;
		if (max_age != -1 && (commit->date < max_age))
			break;
		if (max_count != -1 && !max_count--)
			break;
		printf("%s\n", sha1_to_hex(commit->object.sha1));
	} while (list);
	return 0;
}
