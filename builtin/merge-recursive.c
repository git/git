#include "builtin.h"
#include "commit.h"
#include "tag.h"
#include "merge-recursive.h"
#include "xdiff-interface.h"

static const char builtin_merge_recursive_usage[] =
	"git %s <base>... -- <head> <remote> ...";

static char *better_branch_name(const char *branch)
{
	static char githead_env[8 + GIT_MAX_HEXSZ + 1];
	char *name;

	if (strlen(branch) != the_hash_algo->hexsz)
		return xstrdup(branch);
	xsnprintf(githead_env, sizeof(githead_env), "GITHEAD_%s", branch);
	name = getenv(githead_env);
	return xstrdup(name ? name : branch);
}

int cmd_merge_recursive(int argc, const char **argv, const char *prefix)
{
	const struct object_id *bases[21];
	unsigned bases_count = 0;
	int i, failed;
	struct object_id h1, h2;
	struct merge_options o;
	char *better1, *better2;
	struct commit *result;

	init_merge_options(&o, the_repository);
	if (argv[0] && ends_with(argv[0], "-subtree"))
		o.subtree_shift = "";

	if (argc < 4)
		usagef(builtin_merge_recursive_usage, argv[0]);

	for (i = 1; i < argc; ++i) {
		const char *arg = argv[i];

		if (starts_with(arg, "--")) {
			if (!arg[2])
				break;
			if (parse_merge_opt(&o, arg + 2))
				die(_("unknown option %s"), arg);
			continue;
		}
		if (bases_count < ARRAY_SIZE(bases)-1) {
			struct object_id *oid = xmalloc(sizeof(struct object_id));
			if (get_oid(argv[i], oid))
				die(_("could not parse object '%s'"), argv[i]);
			bases[bases_count++] = oid;
		}
		else
			warning(Q_("cannot handle more than %d base. "
				   "Ignoring %s.",
				   "cannot handle more than %d bases. "
				   "Ignoring %s.",
				    (int)ARRAY_SIZE(bases)-1),
				(int)ARRAY_SIZE(bases)-1, argv[i]);
	}
	if (argc - i != 3) /* "--" "<head>" "<remote>" */
		die(_("not handling anything other than two heads merge."));

	o.branch1 = argv[++i];
	o.branch2 = argv[++i];

	if (get_oid(o.branch1, &h1))
		die(_("could not resolve ref '%s'"), o.branch1);
	if (get_oid(o.branch2, &h2))
		die(_("could not resolve ref '%s'"), o.branch2);

	o.branch1 = better1 = better_branch_name(o.branch1);
	o.branch2 = better2 = better_branch_name(o.branch2);

	if (o.verbosity >= 3)
		printf(_("Merging %s with %s\n"), o.branch1, o.branch2);

	failed = merge_recursive_generic(&o, &h1, &h2, bases_count, bases, &result);

	free(better1);
	free(better2);

	if (failed < 0)
		return 128; /* die() error code */
	return failed;
}
