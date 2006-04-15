#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "log-tree.h"

void init_log_tree_opt(struct log_tree_opt *opt)
{
	memset(opt, 0, sizeof *opt);
	opt->ignore_merges = 1;
	opt->header_prefix = "";
	opt->commit_format = CMIT_FMT_RAW;
	diff_setup(&opt->diffopt);
}

int log_tree_opt_parse(struct log_tree_opt *opt, const char **av, int ac)
{
	const char *arg;
	int cnt = diff_opt_parse(&opt->diffopt, av, ac);
	if (0 < cnt)
		return cnt;
	arg = *av;
	if (!strcmp(arg, "-r"))
		opt->diffopt.recursive = 1;
	else if (!strcmp(arg, "-t")) {
		opt->diffopt.recursive = 1;
		opt->diffopt.tree_in_recursive = 1;
	}
	else if (!strcmp(arg, "-m"))
		opt->ignore_merges = 0;
	else if (!strcmp(arg, "-c"))
		opt->combine_merges = 1;
	else if (!strcmp(arg, "--cc")) {
		opt->dense_combined_merges = 1;
		opt->combine_merges = 1;
	}
	else if (!strcmp(arg, "-v")) {
		opt->verbose_header = 1;
		opt->header_prefix = "diff-tree ";
	}
	else if (!strncmp(arg, "--pretty", 8)) {
		opt->verbose_header = 1;
		opt->header_prefix = "diff-tree ";
		opt->commit_format = get_commit_format(arg+8);
	}
	else if (!strcmp(arg, "--root"))
		opt->show_root_diff = 1;
	else if (!strcmp(arg, "--no-commit-id"))
		opt->no_commit_id = 1;
	else if (!strcmp(arg, "--always"))
		opt->always_show_header = 1;
	else
		return 0;
	return 1;
}

int log_tree_diff_flush(struct log_tree_opt *opt)
{
	diffcore_std(&opt->diffopt);
	if (diff_queue_is_empty()) {
		int saved_fmt = opt->diffopt.output_format;
		opt->diffopt.output_format = DIFF_FORMAT_NO_OUTPUT;
		diff_flush(&opt->diffopt);
		opt->diffopt.output_format = saved_fmt;
		return 0;
	}
	if (opt->header) {
		if (!opt->no_commit_id)
			printf("%s%c", opt->header,
			       opt->diffopt.line_termination);
		opt->header = NULL;
	}
	diff_flush(&opt->diffopt);
	return 1;
}

static int diff_root_tree(struct log_tree_opt *opt,
			  const unsigned char *new, const char *base)
{
	int retval;
	void *tree;
	struct tree_desc empty, real;

	tree = read_object_with_reference(new, tree_type, &real.size, NULL);
	if (!tree)
		die("unable to read root tree (%s)", sha1_to_hex(new));
	real.buf = tree;

	empty.buf = "";
	empty.size = 0;
	retval = diff_tree(&empty, &real, base, &opt->diffopt);
	free(tree);
	log_tree_diff_flush(opt);
	return retval;
}

static const char *generate_header(struct log_tree_opt *opt,
				   const unsigned char *commit_sha1,
				   const unsigned char *parent_sha1,
				   const struct commit *commit)
{
	static char this_header[16384];
	int offset;
	unsigned long len;
	int abbrev = opt->diffopt.abbrev;
	const char *msg = commit->buffer;

	if (!opt->verbose_header)
		return sha1_to_hex(commit_sha1);

	len = strlen(msg);

	offset = sprintf(this_header, "%s%s ",
			 opt->header_prefix,
			 diff_unique_abbrev(commit_sha1, abbrev));
	if (commit_sha1 != parent_sha1)
		offset += sprintf(this_header + offset, "(from %s)\n",
				  parent_sha1
				  ? diff_unique_abbrev(parent_sha1, abbrev)
				  : "root");
	else
		offset += sprintf(this_header + offset, "(from parents)\n");
	offset += pretty_print_commit(opt->commit_format, commit, len,
				      this_header + offset,
				      sizeof(this_header) - offset, abbrev);
	if (opt->always_show_header) {
		puts(this_header);
		return NULL;
	}
	return this_header;
}

static int do_diff_combined(struct log_tree_opt *opt, struct commit *commit)
{
	unsigned const char *sha1 = commit->object.sha1;

	opt->header = generate_header(opt, sha1, sha1, commit);
	opt->header = diff_tree_combined_merge(sha1, opt->header,
						opt->dense_combined_merges,
						&opt->diffopt);
	if (!opt->header && opt->verbose_header)
		opt->header_prefix = "\ndiff-tree ";
	return 0;
}

int log_tree_commit(struct log_tree_opt *opt, struct commit *commit)
{
	struct commit_list *parents;
	unsigned const char *sha1 = commit->object.sha1;

	/* Root commit? */
	if (opt->show_root_diff && !commit->parents) {
		opt->header = generate_header(opt, sha1, NULL, commit);
		diff_root_tree(opt, sha1, "");
	}

	/* More than one parent? */
	if (commit->parents && commit->parents->next) {
		if (opt->ignore_merges)
			return 0;
		else if (opt->combine_merges)
			return do_diff_combined(opt, commit);
	}

	for (parents = commit->parents; parents; parents = parents->next) {
		struct commit *parent = parents->item;
		unsigned const char *psha1 = parent->object.sha1;
		opt->header = generate_header(opt, sha1, psha1, commit);
		diff_tree_sha1(psha1, sha1, "", &opt->diffopt);
		log_tree_diff_flush(opt);		

		if (!opt->header && opt->verbose_header)
			opt->header_prefix = "\ndiff-tree ";
	}
	return 0;
}

int parse_whatchanged_opt(int ac, const char **av, struct whatchanged_opt *wcopt)
{
	struct rev_info *rev = &wcopt->revopt;
	struct log_tree_opt *opt = &wcopt->logopt;
	const char **unrecognized = av+1;
	int left = 1;

	ac = setup_revisions(ac, av, rev, "HEAD");
	if (!strcmp(av[0], "show"))
		rev->no_walk = 1;
	while (1 < ac) {
		const char *arg = av[1];
		if (!strncmp(arg, "--pretty", 8)) {
			opt->commit_format = get_commit_format(arg + 8);
		}
		else if (!strcmp(arg, "--no-abbrev")) {
			wcopt->abbrev = 0;
		}
		else if (!strcmp(arg, "--abbrev")) {
			wcopt->abbrev = DEFAULT_ABBREV;
		}
		else if (!strcmp(arg, "--abbrev-commit")) {
			wcopt->abbrev_commit = 1;
		}
		else if (!strncmp(arg, "--abbrev=", 9)) {
			wcopt->abbrev = strtoul(arg + 9, NULL, 10);
			if (wcopt->abbrev && wcopt->abbrev < MINIMUM_ABBREV)
				wcopt->abbrev = MINIMUM_ABBREV;
			else if (40 < wcopt->abbrev)
				wcopt->abbrev = 40;
		}
		else if (!strcmp(arg, "--full-diff")) {
			wcopt->do_diff = 1;
			wcopt->full_diff = 1;
		}
		else {
			int cnt = log_tree_opt_parse(opt, av+1, ac-1);
			if (0 < cnt) {
				wcopt->do_diff = 1;
				av += cnt;
				ac -= cnt;
				continue;
			}
			*unrecognized++ = arg;
			left++;
		}
		ac--; av++;
	}

	if (wcopt->do_diff) {
		opt->diffopt.abbrev = wcopt->abbrev;
		opt->verbose_header = 0;
		opt->always_show_header = 0;
		opt->no_commit_id = 1;
		if (opt->combine_merges)
			opt->ignore_merges = 0;
		if (opt->dense_combined_merges)
			opt->diffopt.output_format = DIFF_FORMAT_PATCH;
		if (opt->diffopt.output_format == DIFF_FORMAT_PATCH)
			opt->diffopt.recursive = 1;
		if (!wcopt->full_diff && rev->prune_data)
			diff_tree_setup_paths(rev->prune_data, &opt->diffopt);
		diff_setup_done(&opt->diffopt);
	}
	return left;
}
