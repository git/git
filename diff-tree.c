#include <ctype.h>
#include "cache.h"
#include "diff.h"
#include "commit.h"

static int show_root_diff = 0;
static int verbose_header = 0;
static int ignore_merges = 1;
static int recursive = 0;
static int show_tree_entry_in_recursive = 0;
static int read_stdin = 0;
static int diff_output_format = DIFF_FORMAT_RAW;
static int diff_line_termination = '\n';
static int detect_rename = 0;
static int find_copies_harder = 0;
static int diff_setup_opt = 0;
static int diff_score_opt = 0;
static const char *pickaxe = NULL;
static int pickaxe_opts = 0;
static int diff_break_opt = -1;
static const char *orderfile = NULL;
static const char *diff_filter = NULL;
static const char *header = NULL;
static const char *header_prefix = "";
static enum cmit_fmt commit_format = CMIT_FMT_RAW;

// What paths are we interested in?
static int nr_paths = 0;
static const char **paths = NULL;
static int *pathlens = NULL;

static int diff_tree_sha1(const unsigned char *old, const unsigned char *new, const char *base);

static void update_tree_entry(void **bufp, unsigned long *sizep)
{
	void *buf = *bufp;
	unsigned long size = *sizep;
	int len = strlen(buf) + 1 + 20;

	if (size < len)
		die("corrupt tree file");
	*bufp = buf + len;
	*sizep = size - len;
}

static const unsigned char *extract(void *tree, unsigned long size, const char **pathp, unsigned int *modep)
{
	int len = strlen(tree)+1;
	const unsigned char *sha1 = tree + len;
	const char *path = strchr(tree, ' ');
	unsigned int mode;

	if (!path || size < len + 20 || sscanf(tree, "%o", &mode) != 1)
		die("corrupt tree file");
	*pathp = path+1;
	*modep = DIFF_FILE_CANON_MODE(mode);
	return sha1;
}

static char *malloc_base(const char *base, const char *path, int pathlen)
{
	int baselen = strlen(base);
	char *newbase = xmalloc(baselen + pathlen + 2);
	memcpy(newbase, base, baselen);
	memcpy(newbase + baselen, path, pathlen);
	memcpy(newbase + baselen + pathlen, "/", 2);
	return newbase;
}

static void show_file(const char *prefix, void *tree, unsigned long size, const char *base);
static void show_tree(const char *prefix, void *tree, unsigned long size, const char *base);

/* A file entry went away or appeared */
static void show_file(const char *prefix, void *tree, unsigned long size, const char *base)
{
	unsigned mode;
	const char *path;
	const unsigned char *sha1 = extract(tree, size, &path, &mode);

	if (recursive && S_ISDIR(mode)) {
		char type[20];
		unsigned long size;
		char *newbase = malloc_base(base, path, strlen(path));
		void *tree;

		tree = read_sha1_file(sha1, type, &size);
		if (!tree || strcmp(type, "tree"))
			die("corrupt tree sha %s", sha1_to_hex(sha1));

		show_tree(prefix, tree, size, newbase);

		free(tree);
		free(newbase);
		return;
	}

	diff_addremove(prefix[0], mode, sha1, base, path);
}

static int compare_tree_entry(void *tree1, unsigned long size1, void *tree2, unsigned long size2, const char *base)
{
	unsigned mode1, mode2;
	const char *path1, *path2;
	const unsigned char *sha1, *sha2;
	int cmp, pathlen1, pathlen2;

	sha1 = extract(tree1, size1, &path1, &mode1);
	sha2 = extract(tree2, size2, &path2, &mode2);

	pathlen1 = strlen(path1);
	pathlen2 = strlen(path2);
	cmp = base_name_compare(path1, pathlen1, mode1, path2, pathlen2, mode2);
	if (cmp < 0) {
		show_file("-", tree1, size1, base);
		return -1;
	}
	if (cmp > 0) {
		show_file("+", tree2, size2, base);
		return 1;
	}
	if (!find_copies_harder && !memcmp(sha1, sha2, 20) && mode1 == mode2)
		return 0;

	/*
	 * If the filemode has changed to/from a directory from/to a regular
	 * file, we need to consider it a remove and an add.
	 */
	if (S_ISDIR(mode1) != S_ISDIR(mode2)) {
		show_file("-", tree1, size1, base);
		show_file("+", tree2, size2, base);
		return 0;
	}

	if (recursive && S_ISDIR(mode1)) {
		int retval;
		char *newbase = malloc_base(base, path1, pathlen1);
		if (show_tree_entry_in_recursive)
			diff_change(mode1, mode2, sha1, sha2, base, path1);
		retval = diff_tree_sha1(sha1, sha2, newbase);
		free(newbase);
		return retval;
	}

	diff_change(mode1, mode2, sha1, sha2, base, path1);
	return 0;
}

static int interesting(void *tree, unsigned long size, const char *base)
{
	const char *path;
	unsigned mode;
	int i;
	int baselen, pathlen;

	if (!nr_paths)
		return 1;

	(void)extract(tree, size, &path, &mode);

	pathlen = strlen(path);
	baselen = strlen(base);

	for (i=0; i < nr_paths; i++) {
		const char *match = paths[i];
		int matchlen = pathlens[i];

		if (baselen >= matchlen) {
			/* If it doesn't match, move along... */
			if (strncmp(base, match, matchlen))
				continue;

			/* The base is a subdirectory of a path which was specified. */
			return 1;
		}

		/* Does the base match? */
		if (strncmp(base, match, baselen))
			continue;

		match += baselen;
		matchlen -= baselen;

		if (pathlen > matchlen)
			continue;

		if (matchlen > pathlen) {
			if (match[pathlen] != '/')
				continue;
			if (!S_ISDIR(mode))
				continue;
		}

		if (strncmp(path, match, pathlen))
			continue;

		return 1;
	}
	return 0; /* No matches */
}

/* A whole sub-tree went away or appeared */
static void show_tree(const char *prefix, void *tree, unsigned long size, const char *base)
{
	while (size) {
		if (interesting(tree, size, base))
			show_file(prefix, tree, size, base);
		update_tree_entry(&tree, &size);
	}
}

static int diff_tree(void *tree1, unsigned long size1, void *tree2, unsigned long size2, const char *base)
{
	while (size1 | size2) {
		if (nr_paths && size1 && !interesting(tree1, size1, base)) {
			update_tree_entry(&tree1, &size1);
			continue;
		}
		if (nr_paths && size2 && !interesting(tree2, size2, base)) {
			update_tree_entry(&tree2, &size2);
			continue;
		}
		if (!size1) {
			show_file("+", tree2, size2, base);
			update_tree_entry(&tree2, &size2);
			continue;
		}
		if (!size2) {
			show_file("-", tree1, size1, base);
			update_tree_entry(&tree1, &size1);
			continue;
		}
		switch (compare_tree_entry(tree1, size1, tree2, size2, base)) {
		case -1:
			update_tree_entry(&tree1, &size1);
			continue;
		case 0:
			update_tree_entry(&tree1, &size1);
			/* Fallthrough */
		case 1:
			update_tree_entry(&tree2, &size2);
			continue;
		}
		die("git-diff-tree: internal error");
	}
	return 0;
}

static int diff_tree_sha1(const unsigned char *old, const unsigned char *new, const char *base)
{
	void *tree1, *tree2;
	unsigned long size1, size2;
	int retval;

	tree1 = read_object_with_reference(old, "tree", &size1, NULL);
	if (!tree1)
		die("unable to read source tree (%s)", sha1_to_hex(old));
	tree2 = read_object_with_reference(new, "tree", &size2, NULL);
	if (!tree2)
		die("unable to read destination tree (%s)", sha1_to_hex(new));
	retval = diff_tree(tree1, size1, tree2, size2, base);
	free(tree1);
	free(tree2);
	return retval;
}

static void call_diff_setup(void)
{
	diff_setup(diff_setup_opt);
}

static int call_diff_flush(void)
{
	diffcore_std(NULL,
		     detect_rename, diff_score_opt,
		     pickaxe, pickaxe_opts,
		     diff_break_opt,
		     orderfile,
		     diff_filter);
	if (diff_queue_is_empty()) {
		diff_flush(DIFF_FORMAT_NO_OUTPUT, diff_line_termination);
		return 0;
	}
	if (header) {
		printf("%s%c", header, diff_line_termination);
		header = NULL;
	}
	diff_flush(diff_output_format, diff_line_termination);
	return 1;
}

static int diff_tree_sha1_top(const unsigned char *old,
			      const unsigned char *new, const char *base)
{
	int ret;

	call_diff_setup();
	ret = diff_tree_sha1(old, new, base);
	call_diff_flush();
	return ret;
}

static int diff_root_tree(const unsigned char *new, const char *base)
{
	int retval;
	void *tree;
	unsigned long size;

	call_diff_setup();
	tree = read_object_with_reference(new, "tree", &size, NULL);
	if (!tree)
		die("unable to read root tree (%s)", sha1_to_hex(new));
	retval = diff_tree("", 0, tree, size, base);
	free(tree);
	call_diff_flush();
	return retval;
}

static const char *generate_header(const char *commit, const char *parent, const char *msg, unsigned long len)
{
	static char this_header[16384];
	int offset;

	if (!verbose_header)
		return commit;

	offset = sprintf(this_header, "%s%s (from %s)\n", header_prefix, commit, parent);
	offset += pretty_print_commit(commit_format, msg, len, this_header + offset, sizeof(this_header) - offset);
	return this_header;
}

static int diff_tree_commit(const unsigned char *commit, const char *name)
{
	unsigned long size, offset;
	char *buf = read_object_with_reference(commit, "commit", &size, NULL);

	if (!buf)
		return -1;

	if (!name) {
		static char commit_name[60];
		strcpy(commit_name, sha1_to_hex(commit));
		name = commit_name;
	}

	/* Root commit? */
	if (show_root_diff && memcmp(buf + 46, "parent ", 7)) {
		header = generate_header(name, "root", buf, size);
		diff_root_tree(commit, "");
	}

	/* More than one parent? */
	if (ignore_merges) {
		if (!memcmp(buf + 46 + 48, "parent ", 7))
			return 0;
	}

	offset = 46;
	while (offset + 48 < size && !memcmp(buf + offset, "parent ", 7)) {
		unsigned char parent[20];
		if (get_sha1_hex(buf + offset + 7, parent))
			return -1;
		header = generate_header(name, sha1_to_hex(parent), buf, size);
		diff_tree_sha1_top(parent, commit, "");
		if (!header && verbose_header) {
			header_prefix = "\ndiff-tree ";
			/*
			 * Don't print multiple merge entries if we
			 * don't print the diffs.
			 */
		}
		offset += 48;
	}
	return 0;
}

static int diff_tree_stdin(char *line)
{
	int len = strlen(line);
	unsigned char commit[20], parent[20];
	static char this_header[1000];

	if (!len || line[len-1] != '\n')
		return -1;
	line[len-1] = 0;
	if (get_sha1_hex(line, commit))
		return -1;
	if (isspace(line[40]) && !get_sha1_hex(line+41, parent)) {
		line[40] = 0;
		line[81] = 0;
		sprintf(this_header, "%s (from %s)\n", line, line+41);
		header = this_header;
		return diff_tree_sha1_top(parent, commit, "");
	}
	line[40] = 0;
	return diff_tree_commit(commit, line);
}

static const char diff_tree_usage[] =
"git-diff-tree [--stdin] [-m] [-s] [-v] [--pretty] [-t] "
"[<common diff options>] <tree-ish> <tree-ish>"
COMMON_DIFF_OPTIONS_HELP;

int main(int argc, const char **argv)
{
	int nr_sha1;
	char line[1000];
	unsigned char sha1[2][20];

	nr_sha1 = 0;
	for (;;) {
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

		if (!strcmp(arg, "--")) {
			argv++;
			argc--;
			break;
		}
		if (!strcmp(arg, "-r")) {
			recursive = 1;
			continue;
		}
		if (!strcmp(arg, "-t")) {
			recursive = show_tree_entry_in_recursive = 1;
			continue;
		}
		if (!strcmp(arg, "-R")) {
			diff_setup_opt |= DIFF_SETUP_REVERSE;
			continue;
		}
		if (!strcmp(arg, "-p") || !strcmp(arg, "-u")) {
			diff_output_format = DIFF_FORMAT_PATCH;
			recursive = 1;
			continue;
		}
		if (!strncmp(arg, "-S", 2)) {
			pickaxe = arg + 2;
			continue;
		}
		if (!strncmp(arg, "-O", 2)) {
			orderfile = arg + 2;
			continue;
		}
		if (!strncmp(arg, "--diff-filter=", 14)) {
			diff_filter = arg + 14;
			continue;
		}
		if (!strcmp(arg, "--pickaxe-all")) {
			pickaxe_opts = DIFF_PICKAXE_ALL;
			continue;
		}
		if (!strncmp(arg, "-M", 2)) {
			detect_rename = DIFF_DETECT_RENAME;
			if ((diff_score_opt = diff_scoreopt_parse(arg)) == -1)
				usage(diff_tree_usage);
			continue;
		}
		if (!strncmp(arg, "-C", 2)) {
			detect_rename = DIFF_DETECT_COPY;
			if ((diff_score_opt = diff_scoreopt_parse(arg)) == -1)
				usage(diff_tree_usage);
			continue;
		}
		if (!strncmp(arg, "-B", 2)) {
			if ((diff_break_opt = diff_scoreopt_parse(arg)) == -1)
				usage(diff_tree_usage);
			continue;
		}
		if (!strcmp(arg, "--find-copies-harder")) {
			find_copies_harder = 1;
			continue;
		}
		if (!strcmp(arg, "--name-only")) {
			diff_output_format = DIFF_FORMAT_NAME;
			continue;
		}
		if (!strcmp(arg, "-z")) {
			diff_line_termination = 0;
			continue;
		}
		if (!strcmp(arg, "-m")) {
			ignore_merges = 0;
			continue;
		}
		if (!strcmp(arg, "-s")) {
			diff_output_format = DIFF_FORMAT_NO_OUTPUT;
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
		usage(diff_tree_usage);
	}
	if (find_copies_harder && detect_rename != DIFF_DETECT_COPY)
		usage(diff_tree_usage);

	if (argc > 0) {
		int i;

		paths = argv;
		nr_paths = argc;
		pathlens = xmalloc(nr_paths * sizeof(int));
		for (i=0; i<nr_paths; i++)
			pathlens[i] = strlen(paths[i]);
	}

	switch (nr_sha1) {
	case 0:
		if (!read_stdin)
			usage(diff_tree_usage);
		break;
	case 1:
		diff_tree_commit(sha1[0], NULL);
		break;
	case 2:
		diff_tree_sha1_top(sha1[0], sha1[1], "");
		break;
	}

	if (!read_stdin)
		return 0;

	if (detect_rename)
		diff_setup_opt |= (DIFF_SETUP_USE_SIZE_CACHE |
				   DIFF_SETUP_USE_CACHE);
	while (fgets(line, sizeof(line), stdin))
		diff_tree_stdin(line);

	return 0;
}
