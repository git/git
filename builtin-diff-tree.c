#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "log-tree.h"
#include "builtin.h"

static struct rev_info log_tree_opt;

static int diff_tree_commit_sha1(const unsigned char *sha1)
{
	struct commit *commit = lookup_commit_reference(sha1);
	if (!commit)
		return -1;
	return log_tree_commit(&log_tree_opt, commit);
}

static int diff_tree_stdin(char *line)
{
	int len = strlen(line);
	unsigned char sha1[20];
	struct commit *commit;

	if (!len || line[len-1] != '\n')
		return -1;
	line[len-1] = 0;
	if (get_sha1_hex(line, sha1))
		return -1;
	commit = lookup_commit(sha1);
	if (!commit || parse_commit(commit))
		return -1;
	if (isspace(line[40]) && !get_sha1_hex(line+41, sha1)) {
		/* Graft the fake parents locally to the commit */
		int pos = 41;
		struct commit_list **pptr, *parents;

		/* Free the real parent list */
		for (parents = commit->parents; parents; ) {
			struct commit_list *tmp = parents->next;
			free(parents);
			parents = tmp;
		}
		commit->parents = NULL;
		pptr = &(commit->parents);
		while (line[pos] && !get_sha1_hex(line + pos, sha1)) {
			struct commit *parent = lookup_commit(sha1);
			if (parent) {
				pptr = &commit_list_insert(parent, pptr)->next;
			}
			pos += 41;
		}
	}
	return log_tree_commit(&log_tree_opt, commit);
}

static const char diff_tree_usage[] =
"git-diff-tree [--stdin] [-m] [-c] [--cc] [-s] [-v] [--pretty] [-t] [-r] [--root] "
"[<common diff options>] <tree-ish> [<tree-ish>] [<path>...]\n"
"  -r            diff recursively\n"
"  --root        include the initial commit as diff against /dev/null\n"
COMMON_DIFF_OPTIONS_HELP;

int cmd_diff_tree(int argc, const char **argv, char **envp)
{
	int nr_sha1;
	char line[1000];
	struct object *tree1, *tree2;
	static struct rev_info *opt = &log_tree_opt;
	struct object_list *list;
	int read_stdin = 0;

	git_config(git_diff_config);
	nr_sha1 = 0;
	init_revisions(opt);
	opt->abbrev = 0;
	opt->diff = 1;
	argc = setup_revisions(argc, argv, opt, NULL);

	while (--argc > 0) {
		const char *arg = *++argv;

		if (!strcmp(arg, "--stdin")) {
			read_stdin = 1;
			continue;
		}
		usage(diff_tree_usage);
	}

	/*
	 * NOTE! "setup_revisions()" will have inserted the revisions
	 * it parsed in reverse order. So if you do
	 *
	 *	git-diff-tree a b
	 *
	 * the commit list will be "b" -> "a" -> NULL, so we reverse
	 * the order of the objects if the first one is not marked
	 * UNINTERESTING.
	 */
	nr_sha1 = 0;
	list = opt->pending_objects;
	if (list) {
		nr_sha1++;
		tree1 = list->item;
		list = list->next;
		if (list) {
			nr_sha1++;
			tree2 = tree1;
			tree1 = list->item;
			if (list->next)
				usage(diff_tree_usage);
			/* Switch them around if the second one was uninteresting.. */
			if (tree2->flags & UNINTERESTING) {
				struct object *tmp = tree2;
				tree2 = tree1;
				tree1 = tmp;
			}
		}
	}

	switch (nr_sha1) {
	case 0:
		if (!read_stdin)
			usage(diff_tree_usage);
		break;
	case 1:
		diff_tree_commit_sha1(tree1->sha1);
		break;
	case 2:
		diff_tree_sha1(tree1->sha1,
			       tree2->sha1,
			       "", &opt->diffopt);
		log_tree_diff_flush(opt);
		break;
	}

	if (!read_stdin)
		return 0;

	if (opt->diffopt.detect_rename)
		opt->diffopt.setup |= (DIFF_SETUP_USE_SIZE_CACHE |
				       DIFF_SETUP_USE_CACHE);
	while (fgets(line, sizeof(line), stdin)) {
		unsigned char sha1[20];

		if (get_sha1_hex(line, sha1)) {
			fputs(line, stdout);
			fflush(stdout);
		}
		else
			diff_tree_stdin(line);
	}
	return 0;
}
