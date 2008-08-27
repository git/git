#include "builtin.h"
#include "cache.h"
#include "commit.h"

static int show_merge_base(struct commit **rev, int rev_nr, int show_all)
{
	struct commit_list *result;

	result = get_merge_bases_many(rev[0], rev_nr - 1, rev + 1, 0);

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
"git merge-base [--all] <commit-id> <commit-id>...";

static struct commit *get_commit_reference(const char *arg)
{
	unsigned char revkey[20];
	struct commit *r;

	if (get_sha1(arg, revkey))
		die("Not a valid object name %s", arg);
	r = lookup_commit_reference(revkey);
	if (!r)
		die("Not a valid commit name %s", arg);

	return r;
}

int cmd_merge_base(int argc, const char **argv, const char *prefix)
{
	struct commit **rev;
	int rev_nr = 0;
	int show_all = 0;

	git_config(git_default_config, NULL);

	while (1 < argc && argv[1][0] == '-') {
		const char *arg = argv[1];
		if (!strcmp(arg, "-a") || !strcmp(arg, "--all"))
			show_all = 1;
		else
			usage(merge_base_usage);
		argc--; argv++;
	}
	if (argc < 3)
		usage(merge_base_usage);

	rev = xmalloc((argc - 1) * sizeof(*rev));

	do {
		rev[rev_nr++] = get_commit_reference(argv[1]);
		argc--; argv++;
	} while (argc > 1);

	return show_merge_base(rev, rev_nr, show_all);
}
