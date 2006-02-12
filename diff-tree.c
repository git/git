#include "cache.h"
#include "diff.h"
#include "commit.h"

static int show_root_diff = 0;
static int no_commit_id = 0;
static int verbose_header = 0;
static int ignore_merges = 1;
static int combine_merges = 0;
static int dense_combined_merges = 0;
static int read_stdin = 0;
static int always_show_header = 0;

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
				   const struct commit *commit)
{
	static char this_header[16384];
	int offset;
	unsigned long len;
	int abbrev = diff_options.abbrev;
	const char *msg = commit->buffer;

	if (!verbose_header)
		return sha1_to_hex(commit_sha1);

	len = strlen(msg);

	offset = sprintf(this_header, "%s%s ",
			 header_prefix,
			 diff_unique_abbrev(commit_sha1, abbrev));
	if (commit_sha1 != parent_sha1)
		offset += sprintf(this_header + offset, "(from %s)\n",
				  parent_sha1
				  ? diff_unique_abbrev(parent_sha1, abbrev)
				  : "root");
	else
		offset += sprintf(this_header + offset, "(from parents)\n");
	offset += pretty_print_commit(commit_format, commit, len,
				      this_header + offset,
				      sizeof(this_header) - offset, abbrev);
	if (always_show_header) {
		puts(this_header);
		return NULL;
	}
	return this_header;
}

static int diff_tree_commit(struct commit *commit)
{
	struct commit_list *parents;
	unsigned const char *sha1 = commit->object.sha1;

	/* Root commit? */
	if (show_root_diff && !commit->parents) {
		header = generate_header(sha1, NULL, commit);
		diff_root_tree(sha1, "");
	}

	/* More than one parent? */
	if (commit->parents && commit->parents->next) {
		if (ignore_merges)
			return 0;
		else if (combine_merges) {
			header = generate_header(sha1, sha1, commit);
			header = diff_tree_combined_merge(sha1, header,
							dense_combined_merges,
							&diff_options);
			if (!header && verbose_header)
				header_prefix = "\ndiff-tree ";
			return 0;
		}
	}

	for (parents = commit->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;
		header = generate_header(sha1, parent->object.sha1, commit);
		diff_tree_sha1_top(parent->object.sha1, sha1, "");
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

static int diff_tree_commit_sha1(const unsigned char *sha1)
{
	struct commit *commit = lookup_commit_reference(sha1);
	if (!commit)
		return -1;
	return diff_tree_commit(commit);
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
	return diff_tree_commit(commit);
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
		if (!strcmp(arg, "-c")) {
			combine_merges = 1;
			continue;
		}
		if (!strcmp(arg, "--cc")) {
			dense_combined_merges = combine_merges = 1;
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
		if (!strcmp(arg, "--always")) {
			always_show_header = 1;
			continue;
		}
		usage(diff_tree_usage);
	}

	if (combine_merges)
		ignore_merges = 0;

	/* We can only do dense combined merges with diff output */
	if (dense_combined_merges)
		diff_options.output_format = DIFF_FORMAT_PATCH;

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
		diff_tree_commit_sha1(sha1[0]);
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
