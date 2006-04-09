#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "log-tree.h"

static struct log_tree_opt log_tree_opt;

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

int main(int argc, const char **argv)
{
	int nr_sha1;
	char line[1000];
	unsigned char sha1[2][20];
	const char *prefix = setup_git_directory();
	static struct log_tree_opt *opt = &log_tree_opt;
	int read_stdin = 0;

	git_config(git_diff_config);
	nr_sha1 = 0;
	init_log_tree_opt(opt);

	for (;;) {
		int opt_cnt;
		const char *arg;

		argv++;
		argc--;
		arg = *argv;
		if (!arg)
			break;

		if (*arg != '-') {
			if (nr_sha1 < 2 && !get_sha1(arg, sha1[nr_sha1])) {
				nr_sha1++;
				continue;
			}
			break;
		}

		opt_cnt = log_tree_opt_parse(opt, argv, argc);
		if (opt_cnt < 0)
			usage(diff_tree_usage);
		else if (opt_cnt) {
			argv += opt_cnt - 1;
			argc -= opt_cnt - 1;
			continue;
		}

		if (!strcmp(arg, "--")) {
			argv++;
			argc--;
			break;
		}
		if (!strcmp(arg, "--stdin")) {
			read_stdin = 1;
			continue;
		}
		usage(diff_tree_usage);
	}

	if (opt->combine_merges)
		opt->ignore_merges = 0;

	/* We can only do dense combined merges with diff output */
	if (opt->dense_combined_merges)
		opt->diffopt.output_format = DIFF_FORMAT_PATCH;

	if (opt->diffopt.output_format == DIFF_FORMAT_PATCH)
		opt->diffopt.recursive = 1;

	diff_tree_setup_paths(get_pathspec(prefix, argv));
	diff_setup_done(&opt->diffopt);

	switch (nr_sha1) {
	case 0:
		if (!read_stdin)
			usage(diff_tree_usage);
		break;
	case 1:
		diff_tree_commit_sha1(sha1[0]);
		break;
	case 2:
		diff_tree_sha1(sha1[0], sha1[1], "", &opt->diffopt);
		log_tree_diff_flush(opt);
		break;
	}

	if (!read_stdin)
		return 0;

	if (opt->diffopt.detect_rename)
		opt->diffopt.setup |= (DIFF_SETUP_USE_SIZE_CACHE |
				       DIFF_SETUP_USE_CACHE);
	while (fgets(line, sizeof(line), stdin))
		diff_tree_stdin(line);

	return 0;
}
