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
"    --max-count=<n>\n"
"    --max-age=<epoch>\n"
"    --min-age=<epoch>\n"
"    --sparse\n"
"    --no-merges\n"
"    --min-parents=<n>\n"
"    --no-min-parents\n"
"    --max-parents=<n>\n"
"    --no-max-parents\n"
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
"    --abbrev=<n> | --no-abbrev\n"
"    --abbrev-commit\n"
"    --left-right\n"
"  special purpose:\n"
"    --bisect\n"
"    --bisect-vars\n"
"    --bisect-all"
;

static void finish_commit(struct commit *commit, void *data);
static void show_commit(struct commit *commit, void *data)
{
	struct rev_list_info *info = data;
	struct rev_info *revs = info->revs;

	if (info->flags & REV_LIST_QUIET) {
		finish_commit(commit, data);
		return;
	}

	graph_show_commit(revs->graph);

	if (revs->count) {
		if (commit->object.flags & PATCHSAME)
			revs->count_same++;
		else if (commit->object.flags & SYMMETRIC_LEFT)
			revs->count_left++;
		else
			revs->count_right++;
		finish_commit(commit, data);
		return;
	}

	if (info->show_timestamp)
		printf("%lu ", commit->date);
	if (info->header_prefix)
		fputs(info->header_prefix, stdout);

	if (!revs->graph)
		fputs(get_revision_mark(revs, commit), stdout);
	if (revs->abbrev_commit && revs->abbrev)
		fputs(find_unique_abbrev(commit->object.sha1, revs->abbrev),
		      stdout);
	else
		fputs(sha1_to_hex(commit->object.sha1), stdout);
	if (revs->print_parents) {
		struct commit_list *parents = commit->parents;
		while (parents) {
			printf(" %s", sha1_to_hex(parents->item->object.sha1));
			parents = parents->next;
		}
	}
	if (revs->children.name) {
		struct commit_list *children;

		children = lookup_decoration(&revs->children, &commit->object);
		while (children) {
			printf(" %s", sha1_to_hex(children->item->object.sha1));
			children = children->next;
		}
	}
	show_decorations(revs, commit);
	if (revs->commit_format == CMIT_FMT_ONELINE)
		putchar(' ');
	else
		putchar('\n');

	if (revs->verbose_header && commit->buffer) {
		struct strbuf buf = STRBUF_INIT;
		struct pretty_print_context ctx = {0};
		ctx.abbrev = revs->abbrev;
		ctx.date_mode = revs->date_mode;
		ctx.date_mode_explicit = revs->date_mode_explicit;
		ctx.fmt = revs->commit_format;
		pretty_print_commit(&ctx, commit, &buf);
		if (revs->graph) {
			if (buf.len) {
				if (revs->commit_format != CMIT_FMT_ONELINE)
					graph_show_oneline(revs->graph);

				graph_show_commit_msg(revs->graph, &buf);

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
					graph_show_padding(revs->graph);
				putchar('\n');
			} else {
				/*
				 * If the message buffer is empty, just show
				 * the rest of the graph output for this
				 * commit.
				 */
				if (graph_show_remainder(revs->graph))
					putchar('\n');
				if (revs->commit_format == CMIT_FMT_ONELINE)
					putchar('\n');
			}
		} else {
			if (revs->commit_format != CMIT_FMT_USERFORMAT ||
			    buf.len) {
				fwrite(buf.buf, 1, buf.len, stdout);
				putchar(info->hdr_termination);
			}
		}
		strbuf_release(&buf);
	} else {
		if (graph_show_remainder(revs->graph))
			putchar('\n');
	}
	maybe_flush_or_die(stdout, "stdout");
	finish_commit(commit, data);
}

static void finish_commit(struct commit *commit, void *data)
{
	if (commit->parents) {
		free_commit_list(commit->parents);
		commit->parents = NULL;
	}
	free(commit->buffer);
	commit->buffer = NULL;
}

static void finish_object(struct object *obj,
			  const struct name_path *path, const char *name,
			  void *cb_data)
{
	struct rev_list_info *info = cb_data;
	if (obj->type == OBJ_BLOB && !has_sha1_file(obj->sha1))
		die("missing blob object '%s'", sha1_to_hex(obj->sha1));
	if (info->revs->verify_objects && !obj->parsed && obj->type != OBJ_COMMIT)
		parse_object(obj->sha1);
}

static void show_object(struct object *obj,
			const struct name_path *path, const char *component,
			void *cb_data)
{
	struct rev_list_info *info = cb_data;
	finish_object(obj, path, component, cb_data);
	if (info->flags & REV_LIST_QUIET)
		return;
	show_object_with_name(stdout, obj, path, component);
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
int estimate_bisect_steps(int all)
{
	int n, x, e;

	if (all < 3)
		return 0;

	n = log2i(all);
	e = exp2i(n);
	x = all - e;

	return (e < 3 * x) ? n : n - 1;
}

void print_commit_list(struct commit_list *list,
		       const char *format_cur,
		       const char *format_last)
{
	for ( ; list; list = list->next) {
		const char *format = list->next ? format_cur : format_last;
		printf(format, sha1_to_hex(list->item->object.sha1));
	}
}

static void print_var_str(const char *var, const char *val)
{
	printf("%s='%s'\n", var, val);
}

static void print_var_int(const char *var, int val)
{
	printf("%s=%d\n", var, val);
}

static int show_bisect_vars(struct rev_list_info *info, int reaches, int all)
{
	int cnt, flags = info->flags;
	char hex[41] = "";
	struct commit_list *tried;
	struct rev_info *revs = info->revs;

	if (!revs->commits)
		return 1;

	revs->commits = filter_skipped(revs->commits, &tried,
				       flags & BISECT_SHOW_ALL,
				       NULL, NULL);

	/*
	 * revs->commits can reach "reaches" commits among
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

	if (revs->commits)
		strcpy(hex, sha1_to_hex(revs->commits->item->object.sha1));

	if (flags & BISECT_SHOW_ALL) {
		traverse_commit_list(revs, show_commit, show_object, info);
		printf("------\n");
	}

	print_var_str("bisect_rev", hex);
	print_var_int("bisect_nr", cnt - 1);
	print_var_int("bisect_good", all - reaches - 1);
	print_var_int("bisect_bad", reaches - 1);
	print_var_int("bisect_all", all);
	print_var_int("bisect_steps", estimate_bisect_steps(all));

	return 0;
}

int cmd_rev_list(int argc, const char **argv, const char *prefix)
{
	struct rev_info revs;
	struct rev_list_info info;
	int i;
	int bisect_list = 0;
	int bisect_show_vars = 0;
	int bisect_find_all = 0;

	git_config(git_default_config, NULL);
	init_revisions(&revs, prefix);
	revs.abbrev = DEFAULT_ABBREV;
	revs.commit_format = CMIT_FMT_UNSPECIFIED;
	argc = setup_revisions(argc, argv, &revs, NULL);

	memset(&info, 0, sizeof(info));
	info.revs = &revs;
	if (revs.bisect)
		bisect_list = 1;

	if (DIFF_OPT_TST(&revs.diffopt, QUICK))
		info.flags |= REV_LIST_QUIET;
	for (i = 1 ; i < argc; i++) {
		const char *arg = argv[i];

		if (!strcmp(arg, "--header")) {
			revs.verbose_header = 1;
			continue;
		}
		if (!strcmp(arg, "--timestamp")) {
			info.show_timestamp = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect")) {
			bisect_list = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect-all")) {
			bisect_list = 1;
			bisect_find_all = 1;
			info.flags |= BISECT_SHOW_ALL;
			revs.show_decorations = 1;
			continue;
		}
		if (!strcmp(arg, "--bisect-vars")) {
			bisect_list = 1;
			bisect_show_vars = 1;
			continue;
		}
		usage(rev_list_usage);

	}
	if (revs.commit_format != CMIT_FMT_UNSPECIFIED) {
		/* The command line has a --pretty  */
		info.hdr_termination = '\n';
		if (revs.commit_format == CMIT_FMT_ONELINE)
			info.header_prefix = "";
		else
			info.header_prefix = "commit ";
	}
	else if (revs.verbose_header)
		/* Only --header was specified */
		revs.commit_format = CMIT_FMT_RAW;

	if ((!revs.commits &&
	     (!(revs.tag_objects||revs.tree_objects||revs.blob_objects) &&
	      !revs.pending.nr)) ||
	    revs.diff)
		usage(rev_list_usage);

	save_commit_buffer = (revs.verbose_header ||
			      revs.grep_filter.pattern_list ||
			      revs.grep_filter.header_list);
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
			return show_bisect_vars(&info, reaches, all);
	}

	traverse_commit_list(&revs, show_commit, show_object, &info);

	if (revs.count) {
		if (revs.left_right && revs.cherry_mark)
			printf("%d\t%d\t%d\n", revs.count_left, revs.count_right, revs.count_same);
		else if (revs.left_right)
			printf("%d\t%d\n", revs.count_left, revs.count_right);
		else if (revs.cherry_mark)
			printf("%d\t%d\n", revs.count_left + revs.count_right, revs.count_same);
		else
			printf("%d\n", revs.count_left + revs.count_right);
	}

	return 0;
}
