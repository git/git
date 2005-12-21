#include "cache.h"
#include "diff.h"
#include "commit.h"

static int show_root_diff = 0;
static int no_commit_id = 0;
static int verbose_header = 0;
static int ignore_merges = 1;
static int read_stdin = 0;

static const char *header = NULL;
static const char *header_prefix = "";
static enum cmit_fmt commit_format = CMIT_FMT_RAW;

static struct diff_options diff_options;

static int call_diff_flush(void)
{
	diffcore_std(&diff_options);
	if (diff_queue_is_empty()) {
		int saved_fmt = diff_options.output_format;
		diff_options.output_format = DIFF_FORMAT_NO_OUTPUT;
		diff_flush(&diff_options);
		diff_options.output_format = saved_fmt;
		return 0;
	}
	if (header) {
		if (!no_commit_id)
			printf("%s%c", header, diff_options.line_termination);
		header = NULL;
	}
	diff_flush(&diff_options);
	return 1;
}

static int diff_tree_sha1_top(const unsigned char *old,
			      const unsigned char *new, const char *base)
{
	int ret;

	ret = diff_tree_sha1(old, new, base, &diff_options);
	call_diff_flush();
	return ret;
}

static int diff_root_tree(const unsigned char *new, const char *base)
{
	int retval;
	void *tree;
	struct tree_desc empty, real;

	tree = read_object_with_reference(new, "tree", &real.size, NULL);
	if (!tree)
		die("unable to read root tree (%s)", sha1_to_hex(new));
	real.buf = tree;

	empty.buf = "";
	empty.size = 0;
	retval = diff_tree(&empty, &real, base, &diff_options);
	free(tree);
	call_diff_flush();
	return retval;
}

static const char *generate_header(const unsigned char *commit_sha1,
				   const unsigned char *parent_sha1,
				   const char *msg)
{
	static char this_header[16384];
	int offset;
	unsigned long len;
	int abbrev = diff_options.abbrev;

	if (!verbose_header)
		return sha1_to_hex(commit_sha1);

	len = strlen(msg);

	offset = sprintf(this_header, "%s%s ",
			 header_prefix,
			 diff_unique_abbrev(commit_sha1, abbrev));
	offset += sprintf(this_header + offset, "(from %s)\n",
			 parent_sha1 ?
			 diff_unique_abbrev(parent_sha1, abbrev) : "root");
	offset += pretty_print_commit(commit_format, msg, len,
				      this_header + offset,
				      sizeof(this_header) - offset);
	return this_header;
}

static int diff_tree_commit(const unsigned char *commit_sha1)
{
	struct commit *commit;
	struct commit_list *parents;
	char name[50];
	unsigned char sha1[20];

	sprintf(name, "%s^0", sha1_to_hex(commit_sha1));
	if (get_sha1(name, sha1))
		return -1;
	name[40] = 0;
	commit = lookup_commit(sha1);
	
	/* Root commit? */
	if (show_root_diff && !commit->parents) {
		header = generate_header(sha1, NULL, commit->buffer);
		diff_root_tree(commit_sha1, "");
	}

	/* More than one parent? */
	if (ignore_merges && commit->parents && commit->parents->next)
		return 0;

	for (parents = commit->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;
		header = generate_header(sha1,
					 parent->object.sha1,
					 commit->buffer);
		diff_tree_sha1_top(parent->object.sha1, commit_sha1, "");
		if (!header && verbose_header) {
			header_prefix = "\ndiff-tree ";
			/*
			 * Don't print multiple merge entries if we
			 * don't print the diffs.
			 */
		}
	}
	return 0;
}

static int diff_tree_stdin(char *line)
{
	int len = strlen(line);
	unsigned char commit[20], parent[20];
	static char this_header[1000];
	int abbrev = diff_options.abbrev;

	if (!len || line[len-1] != '\n')
		return -1;
	line[len-1] = 0;
	if (get_sha1_hex(line, commit))
		return -1;
	if (isspace(line[40]) && !get_sha1_hex(line+41, parent)) {
		line[40] = 0;
		line[81] = 0;
		sprintf(this_header, "%s (from %s)\n",
			diff_unique_abbrev(commit, abbrev),
			diff_unique_abbrev(parent, abbrev));
		header = this_header;
		return diff_tree_sha1_top(parent, commit, "");
	}
	line[40] = 0;
	return diff_tree_commit(commit);
}

static const char diff_tree_usage[] =
"git-diff-tree [--stdin] [-m] [-s] [-v] [--pretty] [-t] [-r] [--root] "
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

	git_config(git_diff_config);
	nr_sha1 = 0;
	diff_setup(&diff_options);

	for (;;) {
		int diff_opt_cnt;
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

		diff_opt_cnt = diff_opt_parse(&diff_options, argv, argc);
		if (diff_opt_cnt < 0)
			usage(diff_tree_usage);
		else if (diff_opt_cnt) {
			argv += diff_opt_cnt - 1;
			argc -= diff_opt_cnt - 1;
			continue;
		}


		if (!strcmp(arg, "--")) {
			argv++;
			argc--;
			break;
		}
		if (!strcmp(arg, "-r")) {
			diff_options.recursive = 1;
			continue;
		}
		if (!strcmp(arg, "-t")) {
			diff_options.recursive = 1;
			diff_options.tree_in_recursive = 1;
			continue;
		}
		if (!strcmp(arg, "-m")) {
			ignore_merges = 0;
			continue;
		}
		if (!strcmp(arg, "-v")) {
			verbose_header = 1;
			header_prefix = "diff-tree ";
			continue;
		}
		if (!strncmp(arg, "--pretty", 8)) {
			verbose_header = 1;
			header_prefix = "diff-tree ";
			commit_format = get_commit_format(arg+8);
			continue;
		}
		if (!strcmp(arg, "--stdin")) {
			read_stdin = 1;
			continue;
		}
		if (!strcmp(arg, "--root")) {
			show_root_diff = 1;
			continue;
		}
		if (!strcmp(arg, "--no-commit-id")) {
			no_commit_id = 1;
			continue;
		}
		usage(diff_tree_usage);
	}
	if (diff_options.output_format == DIFF_FORMAT_PATCH)
		diff_options.recursive = 1;

	diff_tree_setup_paths(get_pathspec(prefix, argv));
	diff_setup_done(&diff_options);

	switch (nr_sha1) {
	case 0:
		if (!read_stdin)
			usage(diff_tree_usage);
		break;
	case 1:
		diff_tree_commit(sha1[0]);
		break;
	case 2:
		diff_tree_sha1_top(sha1[0], sha1[1], "");
		break;
	}

	if (!read_stdin)
		return 0;

	if (diff_options.detect_rename)
		diff_options.setup |= (DIFF_SETUP_USE_SIZE_CACHE |
				       DIFF_SETUP_USE_CACHE);
	while (fgets(line, sizeof(line), stdin))
		diff_tree_stdin(line);

	return 0;
}
