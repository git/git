#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "builtin.h"
#include "log-tree.h"
#include "graph.h"
#include "bisect.h"

static const char rev_list_usage[] =
"git rev-list [OPTION] <commit-id>... [ -- paths... ]\n"
"  limiting output:\n"
"    --max-count=nr\n"
"    --max-age=epoch\n"
"    --min-age=epoch\n"
"    --sparse\n"
"    --no-merges\n"
"    --remove-empty\n"
"    --all\n"
"    --branches\n"
"    --tags\n"
"    --remotes\n"
"    --stdin\n"
"    --quiet\n"
"  ordering output:\n"
"    --topo-order\n"
"    --date-order\n"
"    --reverse\n"
"  formatting output:\n"
"    --parents\n"
"    --children\n"
"    --objects | --objects-edge\n"
"    --unpacked\n"
"    --header | --pretty\n"
"    --abbrev=nr | --no-abbrev\n"
"    --abbrev-commit\n"
"    --left-right\n"
"  special purpose:\n"
"    --bisect\n"
"    --bisect-vars\n"
"    --bisect-all"
;

static struct rev_info revs;

static int show_timestamp;
static int hdr_termination;
static const char *header_prefix;

static void finish_commit(struct commit *commit);
static void show_commit(struct commit *commit)
{
	graph_show_commit(revs.graph);

	if (show_timestamp)
		printf("%lu ", commit->date);
	if (header_prefix)
		fputs(header_prefix, stdout);

	if (!revs.graph) {
		if (commit->object.flags & BOUNDARY)
			putchar('-');
		else if (commit->object.flags & UNINTERESTING)
			putchar('^');
		else if (revs.left_right) {
			if (commit->object.flags & SYMMETRIC_LEFT)
				putchar('<');
			else
				putchar('>');
		}
	}
	if (revs.abbrev_commit && revs.abbrev)
		fputs(find_unique_abbrev(commit->object.sha1, revs.abbrev),
		      stdout);
	else
		fputs(sha1_to_hex(commit->object.sha1), stdout);
	if (revs.print_parents) {
		struct commit_list *parents = commit->parents;
		while (parents) {
			printf(" %s", sha1_to_hex(parents->item->object.sha1));
			parents = parents->next;
		}
	}
	if (revs.children.name) {
		struct commit_list *children;

		children = lookup_decoration(&revs.children, &commit->object);
		while (children) {
			printf(" %s", sha1_to_hex(children->item->object.sha1));
			children = children->next;
		}
	}
	show_decorations(&revs, commit);
	if (revs.commit_format == CMIT_FMT_ONELINE)
		putchar(' ');
	else
		putchar('\n');

	if (revs.verbose_header && commit->buffer) {
		struct strbuf buf = STRBUF_INIT;
		pretty_print_commit(revs.commit_format, commit,
				    &buf, revs.abbrev, NULL, NULL,
				    revs.date_mode, 0);
		if (revs.graph) {
			if (buf.len) {
				if (revs.commit_format != CMIT_FMT_ONELINE)
					graph_show_oneline(revs.graph);

				graph_show_commit_msg(revs.graph, &buf);

				/*
				 * Add a newline after the commit message.
				 *
				 * Usually, this newline produces a blank
				 * padding line between entries, in which case
				 * we need to add graph padding on this line.
				 *
				 * However, the commit message may not end in a
				 * newline.  In this case the newline simply
				 * ends the last line of the commit message,
				 * and we don't need any graph output.  (This
				 * always happens with CMIT_FMT_ONELINE, and it
				 * happens with CMIT_FMT_USERFORMAT when the
				 * format doesn't explicitly end in a newline.)
				 */
				if (buf.len && buf.buf[buf.len - 1] == '\n')
					graph_show_padding(revs.graph);
				putchar('\n');
			} else {
				/*
				 * If the message buffer is empty, just show
				 * the rest of the graph output for this
				 * commit.
				 */
				if (graph_show_remainder(revs.graph))
					putchar('\n');
			}
		} else {
			if (buf.len)
				printf("%s%c", buf.buf, hdr_termination);
		}
		strbuf_release(&buf);
	} else {
		if (graph_show_remainder(revs.graph))
			putchar('\n');
	}
	maybe_flush_or_die(stdout, "stdout");
	finish_commit(commit);
}

static void finish_commit(struct commit *commit)
{
	if (commit->parents) {
		free_commit_list(commit->parents);
		commit->parents = NULL;
	}
	free(commit->buffer);
	commit->buffer = NULL;
}

static void finish_object(struct object_array_entry *p)
{
	if (p->item->type == OBJ_BLOB && !has_sha1_file(p->item->sha1))
		die("missing blob object '%s'", sha1_to_hex(p->item->sha1));
}

static void show_object(struct object_array_entry *p)
{
	/* An object with name "foo\n0000000..." can be used to
	 * confuse downstream "git pack-objects" very badly.
	 */
	const char *ep = strchr(p->name, '\n');

	finish_object(p);
	if (ep) {
		printf("%s %.*s\n", sha1_to_hex(p->item->sha1),
		       (int) (ep - p->name),
		       p->name);
	}
	else
		printf("%s %s\n", sha1_to_hex(p->item->sha1), p->name);
}

static void show_edge(struct commit *commit)
{
	printf("-%s\n", sha1_to_hex(commit->object.sha1));
}

static inline int log2i(int n)
{
	int log2 = 0;

	for (; n > 1; n >>= 1)
		log2++;

	return log2;
}

static inline int exp2i(int n)
{
	return 1 << n;
}

/*
 * Estimate the number of bisect steps left (after the current step)
 *
 * For any x between 0 included and 2^n excluded, the probability for
 * n - 1 steps left looks like:
 *
 * P(2^n + x) == (2^n - x) / (2^n + x)
 *
 * and P(2^n + x) < 0.5 means 2^n < 3x
 */
static int estimate_bisect_steps(int all)
{
	int n, x, e;

	if (all < 3)
		return 0;

	n = log2i(all);
	e = exp2i(n);
	x = all - e;

	return (e < 3 * x) ? n : n - 1;
}

static int show_bisect_vars(int reaches, int all, int bisect_find_all)
{
	int cnt;
	char hex[41];

	if (!revs.commits)
		return 1;

	/*
	 * revs.commits can reach "reaches" commits among
	 * "all" commits.  If it is good, then there are
	 * (all-reaches) commits left to be bisected.
	 * On the other hand, if it is bad, then the set
	 * to bisect is "reaches".
	 * A bisect set of size N has (N-1) commits further
	 * to test, as we already know one bad one.
	 */
	cnt = all - reaches;
	if (cnt < reaches)
		cnt = reaches;
	strcpy(hex, sha1_to_hex(revs.commits->item->object.sha1));

	if (bisect_find_all) {
		traverse_commit_list(&revs, show_commit, show_object);
		printf("------\n");
	}

	printf("bisect_rev=%s\n"
	       "bisect_nr=%d\n"
	       "bisect_good=%d\n"
	       "bisect_bad=%d\n"
	       "bisect_all=%d\n"
	       "bisect_steps=%d\n",
	       hex,
	       cnt - 1,
	       all - reaches - 1,
	       reaches - 1,
	       all,
	       estimate_bisect_steps(all));

	return 0;
}

int cmd_rev_list(int argc, const char **argv, const char *prefix)
{
	struct commit_list *list;
	int i;
	int read_from_stdin = 0;
	int bisect_list = 0;
	int bisect_show_vars = 0;
	int bisect_find_all = 0;
	int quiet = 0;

	git_config(git_default_config, NULL);
	init_revisions(&revs, prefix);
	revs.abbrev = 0;
	revs.commit_format = CMIT_FMT_UNSPECIFIED;
	argc = setup_revisions(argc, argv, &revs, NULL);

	quiet = DIFF_OPT_TST(&revs.diffopt, QUIET);
	for (i = 1 ; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--header")) {
			revs.verbose_header = 1;
			continue;
		}
		if (!strcmp(arg, "--timestamp")) {
			show_timestamp = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect")) {
			bisect_list = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect-all")) {
			bisect_list = 1;
			bisect_find_all = 1;
			revs.show_decorations = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect-vars")) {
			bisect_list = 1;
			bisect_show_vars = 1;
			continue;
		}
		if (!strcmp(arg, "--stdin")) {
			if (read_from_stdin++)
				die("--stdin given twice?");
			read_revisions_from_stdin(&revs);
			continue;
		}
		usage(rev_list_usage);

	}
	if (revs.commit_format != CMIT_FMT_UNSPECIFIED) {
		/* The command line has a --pretty  */
		hdr_termination = '\n';
		if (revs.commit_format == CMIT_FMT_ONELINE)
			header_prefix = "";
		else
			header_prefix = "commit ";
	}
	else if (revs.verbose_header)
		/* Only --header was specified */
		revs.commit_format = CMIT_FMT_RAW;

	list = revs.commits;

	if ((!list &&
	     (!(revs.tag_objects||revs.tree_objects||revs.blob_objects) &&
	      !revs.pending.nr)) ||
	    revs.diff)
		usage(rev_list_usage);

	save_commit_buffer = revs.verbose_header ||
		revs.grep_filter.pattern_list;
	if (bisect_list)
		revs.limited = 1;

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	if (revs.tree_objects)
		mark_edges_uninteresting(revs.commits, &revs, show_edge);

	if (bisect_list) {
		int reaches = reaches, all = all;

		revs.commits = find_bisection(revs.commits, &reaches, &all,
					      bisect_find_all);
		if (bisect_show_vars)
			return show_bisect_vars(reaches, all, bisect_find_all);
	}

	traverse_commit_list(&revs,
		quiet ? finish_commit : show_commit,
		quiet ? finish_object : show_object);

	return 0;
}
