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
	const unsigned char *bases[21];
	unsigned bases_count = 0;
	int i, failed;
	unsigned char h1[20], h2[20];
	struct merge_options o;
	struct commit *result;

	init_merge_options(&o);
	if (argv[0]) {
		int namelen = strlen(argv[0]);
		if (8 < namelen &&
		    !strcmp(argv[0] + namelen - 8, "-subtree"))
			o.subtree_merge = 1;
	}

	if (argc < 4)
		die("Usage: %s <base>... -- <head> <remote> ...", argv[0]);

	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--"))
			break;
		if (bases_count < ARRAY_SIZE(bases)-1) {
			unsigned char *sha = xmalloc(20);
			if (get_sha1(argv[i], sha))
				die("Could not parse object '%s'", argv[i]);
			bases[bases_count++] = sha;
		}
		else
			warning("Cannot handle more than %zu bases. "
				"Ignoring %s.", ARRAY_SIZE(bases)-1, argv[i]);
	}
	if (argc - i != 3) /* "--" "<head>" "<remote>" */
		die("Not handling anything other than two heads merge.");

	o.branch1 = argv[++i];
	o.branch2 = argv[++i];

	if (get_sha1(o.branch1, h1))
		die("Could not resolve ref '%s'", o.branch1);
	if (get_sha1(o.branch2, h2))
		die("Could not resolve ref '%s'", o.branch2);

	o.branch1 = better_branch_name(o.branch1);
	o.branch2 = better_branch_name(o.branch2);

	if (o.verbosity >= 3)
		printf("Merging %s with %s\n", o.branch1, o.branch2);

	failed = merge_recursive_generic(&o, h1, h2, bases_count, bases, &result);
	if (failed < 0)
		return 128; /* die() error code */
	return failed;
}
