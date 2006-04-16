#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "log-tree.h"

int log_tree_diff_flush(struct rev_info *opt)
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

static int diff_root_tree(struct rev_info *opt,
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

static const char *generate_header(struct rev_info *opt,
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

static int do_diff_combined(struct rev_info *opt, struct commit *commit)
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

int log_tree_commit(struct rev_info *opt, struct commit *commit)
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
