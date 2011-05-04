#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "parse-options.h"

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

static const char * const merge_base_usage[] = {
	"git merge-base [-a|--all] <commit> <commit>...",
	"git merge-base [-a|--all] --octopus <commit>...",
	"git merge-base --independent <commit>...",
	NULL
};

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

static int handle_octopus(int count, const char **args, int reduce, int show_all)
{
	struct commit_list *revs = NULL;
	struct commit_list *result;
	int i;

	if (reduce)
		show_all = 1;

	for (i = count - 1; i >= 0; i--)
		commit_list_insert(get_commit_reference(args[i]), &revs);

	result = reduce ? reduce_heads(revs) : get_octopus_merge_bases(revs);

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

int cmd_merge_base(int argc, const char **argv, const char *prefix)
{
	struct commit **rev;
	int rev_nr = 0;
	int show_all = 0;
	int octopus = 0;
	int reduce = 0;

	struct option options[] = {
		OPT_BOOLEAN('a', "all", &show_all, "output all common ancestors"),
		OPT_BOOLEAN(0, "octopus", &octopus, "find ancestors for a single n-way merge"),
		OPT_BOOLEAN(0, "independent", &reduce, "list revs not reachable from others"),
		OPT_END()
	};

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, options, merge_base_usage, 0);
	if (!octopus && !reduce && argc < 2)
		usage_with_options(merge_base_usage, options);
	if (reduce && (show_all || octopus))
		die("--independent cannot be used with other options");

	if (octopus || reduce)
		return handle_octopus(argc, argv, reduce, show_all);

	rev = xmalloc(argc * sizeof(*rev));
	while (argc-- > 0)
		rev[rev_nr++] = get_commit_reference(*argv++);
	return show_merge_base(rev, rev_nr, show_all);
}
