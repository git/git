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

static struct commit *get_ref(const char *ref)
{
	unsigned char sha1[20];
	struct object *object;

	if (get_sha1(ref, sha1))
		die("Could not resolve ref '%s'", ref);
	object = deref_tag(parse_object(sha1), ref, strlen(ref));
	if (!object)
		return NULL;
	if (object->type == OBJ_TREE)
		return make_virtual_commit((struct tree*)object,
			better_branch_name(ref));
	if (object->type != OBJ_COMMIT)
		return NULL;
	if (parse_commit((struct commit *)object))
		die("Could not parse commit '%s'", sha1_to_hex(object->sha1));
	return (struct commit *)object;
}

int cmd_merge_recursive(int argc, const char **argv, const char *prefix)
{
	static const char *bases[20];
	static unsigned bases_count = 0;
	int i, clean;
	const char *branch1, *branch2;
	struct commit *result, *h1, *h2;
	struct commit_list *ca = NULL;
	struct lock_file *lock = xcalloc(1, sizeof(struct lock_file));
	int index_fd;
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
		if (!strcmp(argv[i], "--"))
			break;
		if (bases_count < sizeof(bases)/sizeof(*bases))
			bases[bases_count++] = argv[i];
	}
	if (argc - i != 3) /* "--" "<head>" "<remote>" */
		die("Not handling anything other than two heads merge.");

	branch1 = argv[++i];
	branch2 = argv[++i];

	h1 = get_ref(branch1);
	h2 = get_ref(branch2);

	branch1 = better_branch_name(branch1);
	branch2 = better_branch_name(branch2);

	if (merge_recursive_verbosity >= 3)
		printf("Merging %s with %s\n", branch1, branch2);

	index_fd = hold_locked_index(lock, 1);

	for (i = 0; i < bases_count; i++) {
		struct commit *ancestor = get_ref(bases[i]);
		ca = commit_list_insert(ancestor, &ca);
	}
	clean = merge_recursive(h1, h2, branch1, branch2, ca, &result);

	if (active_cache_changed &&
	    (write_cache(index_fd, active_cache, active_nr) ||
	     commit_locked_index(lock)))
			die ("unable to write %s", get_index_file());

	return clean ? 0: 1;
}
