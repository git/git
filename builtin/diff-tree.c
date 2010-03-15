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

/* Diff one or more commits. */
static int stdin_diff_commit(struct commit *commit, char *line, int len)
{
	unsigned char sha1[20];
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

/* Diff two trees. */
static int stdin_diff_trees(struct tree *tree1, char *line, int len)
{
	unsigned char sha1[20];
	struct tree *tree2;
	if (len != 82 || !isspace(line[40]) || get_sha1_hex(line + 41, sha1))
		return error("Need exactly two trees, separated by a space");
	tree2 = lookup_tree(sha1);
	if (!tree2 || parse_tree(tree2))
		return -1;
	printf("%s %s\n", sha1_to_hex(tree1->object.sha1),
			  sha1_to_hex(tree2->object.sha1));
	diff_tree_sha1(tree1->object.sha1, tree2->object.sha1,
		       "", &log_tree_opt.diffopt);
	log_tree_diff_flush(&log_tree_opt);
	return 0;
}

static int diff_tree_stdin(char *line)
{
	int len = strlen(line);
	unsigned char sha1[20];
	struct object *obj;

	if (!len || line[len-1] != '\n')
		return -1;
	line[len-1] = 0;
	if (get_sha1_hex(line, sha1))
		return -1;
	obj = lookup_unknown_object(sha1);
	if (!obj || !obj->parsed)
		obj = parse_object(sha1);
	if (!obj)
		return -1;
	if (obj->type == OBJ_COMMIT)
		return stdin_diff_commit((struct commit *)obj, line, len);
	if (obj->type == OBJ_TREE)
		return stdin_diff_trees((struct tree *)obj, line, len);
	error("Object %s is a %s, not a commit or tree",
	      sha1_to_hex(sha1), typename(obj->type));
	return -1;
}

static const char diff_tree_usage[] =
"git diff-tree [--stdin] [-m] [-c] [--cc] [-s] [-v] [--pretty] [-t] [-r] [--root] "
"[<common diff options>] <tree-ish> [<tree-ish>] [<path>...]\n"
"  -r            diff recursively\n"
"  --root        include the initial commit as diff against /dev/null\n"
COMMON_DIFF_OPTIONS_HELP;

static void diff_tree_tweak_rev(struct rev_info *rev, struct setup_revision_opt *opt)
{
	if (!rev->diffopt.output_format) {
		if (rev->dense_combined_merges)
			rev->diffopt.output_format = DIFF_FORMAT_PATCH;
		else
			rev->diffopt.output_format = DIFF_FORMAT_RAW;
	}
}

int cmd_diff_tree(int argc, const char **argv, const char *prefix)
{
	int nr_sha1;
	char line[1000];
	struct object *tree1, *tree2;
	static struct rev_info *opt = &log_tree_opt;
	struct setup_revision_opt s_r_opt;
	int read_stdin = 0;

	init_revisions(opt, prefix);
	git_config(git_diff_basic_config, NULL); /* no "diff" UI options */
	opt->abbrev = 0;
	opt->diff = 1;
	opt->disable_stdin = 1;
	memset(&s_r_opt, 0, sizeof(s_r_opt));
	s_r_opt.tweak = diff_tree_tweak_rev;
	argc = setup_revisions(argc, argv, opt, &s_r_opt);

	while (--argc > 0) {
		const char *arg = *++argv;

		if (!strcmp(arg, "--stdin")) {
			read_stdin = 1;
			continue;
		}
		usage(diff_tree_usage);
	}

	/*
	 * NOTE! We expect "a ^b" to be equal to "a..b", so we
	 * reverse the order of the objects if the second one
	 * is marked UNINTERESTING.
	 */
	nr_sha1 = opt->pending.nr;
	switch (nr_sha1) {
	case 0:
		if (!read_stdin)
			usage(diff_tree_usage);
		break;
	case 1:
		tree1 = opt->pending.objects[0].item;
		diff_tree_commit_sha1(tree1->sha1);
		break;
	case 2:
		tree1 = opt->pending.objects[0].item;
		tree2 = opt->pending.objects[1].item;
		if (tree2->flags & UNINTERESTING) {
			struct object *tmp = tree2;
			tree2 = tree1;
			tree1 = tmp;
		}
		diff_tree_sha1(tree1->sha1,
			       tree2->sha1,
			       "", &opt->diffopt);
		log_tree_diff_flush(opt);
		break;
	}

	if (read_stdin) {
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
	}

	return diff_result_code(&opt->diffopt, 0);
}
