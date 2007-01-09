#include "cache.h"
#include "commit.h"

static int show_merge_base(struct commit *rev1, struct commit *rev2, int show_all)
{
	struct commit_list *result = get_merge_bases(rev1, rev2, 0);

	if (!result)
		return 1;

	while (result) {
		printf("%s\n", sha1_to_hex(result->item->object.sha1));
		if (!show_all)
			return 0;
		result = result->next;
	}

	return 0;
}

static const char merge_base_usage[] =
"git-merge-base [--all] <commit-id> <commit-id>";

int cmd_merge_base(int argc, const char **argv, const char *prefix)
{
	struct commit *rev1, *rev2;
	unsigned char rev1key[20], rev2key[20];
	int show_all = 0;

	git_config(git_default_config);

	while (1 < argc && argv[1][0] == '-') {
		const char *arg = argv[1];
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
	return show_merge_base(rev1, rev2, show_all);
}
