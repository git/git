#include "cache.h"
#include "commit.h"
#include "tag.h"
#include "merge-recursive.h"

static const char *better_branch_name(const char *branch)
{
	static char githead_env[8 + 40 + 1];
	char *name;

	if (strlen(branch) != 40)
		return branch;
	sprintf(githead_env, "GITHEAD_%s", branch);
	name = getenv(githead_env);
	return name ? name : branch;
}

int cmd_merge_recursive(int argc, const char **argv, const char *prefix)
{
	const char *bases[21];
	unsigned bases_count = 0;
	int i, failed;
	const char *branch1, *branch2;
	unsigned char h1[20], h2[20];
	int subtree_merge = 0;

	if (argv[0]) {
		int namelen = strlen(argv[0]);
		if (8 < namelen &&
		    !strcmp(argv[0] + namelen - 8, "-subtree"))
			subtree_merge = 1;
	}

	git_config(merge_recursive_config, NULL);
	merge_recursive_setup(subtree_merge);
	if (argc < 4)
		die("Usage: %s <base>... -- <head> <remote> ...\n", argv[0]);

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--")) {
			bases[bases_count] = NULL;
			break;
		}
		if (bases_count < ARRAY_SIZE(bases)-1)
			bases[bases_count++] = argv[i];
		else
			warning("Cannot handle more than %zu bases. "
				"Ignoring %s.", ARRAY_SIZE(bases)-1, argv[i]);
	}
	if (argc - i != 3) /* "--" "<head>" "<remote>" */
		die("Not handling anything other than two heads merge.");

	branch1 = argv[++i];
	branch2 = argv[++i];

	if (get_sha1(branch1, h1))
		die("Could not resolve ref '%s'", branch1);
	if (get_sha1(branch2, h2))
		die("Could not resolve ref '%s'", branch2);

	branch1 = better_branch_name(branch1);
	branch2 = better_branch_name(branch2);

	if (merge_recursive_verbosity >= 3)
		printf("Merging %s with %s\n", branch1, branch2);

	failed = merge_recursive_generic(bases, h1, branch1, h2, branch2);
	if (failed < 0)
		return 128; /* die() error code */
	return failed;
}
