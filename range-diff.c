#include "cache.h"
#include "range-diff.h"
#include "string-list.h"
#include "run-command.h"
#include "strvec.h"
#include "hashmap.h"
#include "xdiff-interface.h"
#include "linear-assignment.h"
#include "diffcore.h"
#include "commit.h"
#include "pretty.h"
#include "userdiff.h"
#include "apply.h"
#include "revision.h"

struct patch_util {
	/* For the search for an exact match */
	struct hashmap_entry e;
	const char *diff, *patch;

	int i, shown;
	int diffsize;
	size_t diff_offset;
	/* the index of the matching item in the other branch, or -1 */
	int matching;
	struct object_id oid;
};

/*
 * Reads the patches into a string list, with the `util` field being populated
 * as struct object_id (will need to be free()d).
 */
static int read_patches(const char *range, struct string_list *list,
			const struct strvec *other_arg)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf buf = STRBUF_INIT, contents = STRBUF_INIT;
	struct patch_util *util = NULL;
	int in_header = 1;
	char *line, *current_filename = NULL;
	ssize_t len;
	size_t size;

	strvec_pushl(&cp.args, "log", "--no-color", "-p", "--no-merges",
		     "--reverse", "--date-order", "--decorate=no",
		     "--no-prefix",
		     /*
		      * Choose indicators that are not used anywhere
		      * else in diffs, but still look reasonable
		      * (e.g. will not be confusing when debugging)
		      */
		     "--output-indicator-new=>",
		     "--output-indicator-old=<",
		     "--output-indicator-context=#",
		     "--no-abbrev-commit",
		     "--pretty=medium",
		     "--notes",
		     NULL);
	if (other_arg)
		strvec_pushv(&cp.args, other_arg->v);
	strvec_push(&cp.args, range);
	cp.out = -1;
	cp.no_stdin = 1;
	cp.git_cmd = 1;

	if (start_command(&cp))
		return error_errno(_("could not start `log`"));
	if (strbuf_read(&contents, cp.out, 0) < 0) {
		error_errno(_("could not read `log` output"));
		finish_command(&cp);
		return -1;
	}
	if (finish_command(&cp))
		return -1;

	line = contents.buf;
	size = contents.len;
	for (; size > 0; size -= len, line += len) {
		const char *p;
		char *eol;

		eol = memchr(line, '\n', size);
		if (eol) {
			*eol = '\0';
			len = eol + 1 - line;
		} else {
			len = size;
		}

		if (skip_prefix(line, "commit ", &p)) {
			if (util) {
				string_list_append(list, buf.buf)->util = util;
				strbuf_reset(&buf);
			}
			CALLOC_ARRAY(util, 1);
			if (get_oid(p, &util->oid)) {
				error(_("could not parse commit '%s'"), p);
				free(util);
				free(current_filename);
				string_list_clear(list, 1);
				strbuf_release(&buf);
				strbuf_release(&contents);
				return -1;
			}
			util->matching = -1;
			in_header = 1;
			continue;
		}

		if (!util) {
			error(_("could not parse first line of `log` output: "
				"did not start with 'commit ': '%s'"),
			      line);
			free(current_filename);
			string_list_clear(list, 1);
			strbuf_release(&buf);
			strbuf_release(&contents);
			return -1;
		}

		if (starts_with(line, "diff --git")) {
			struct patch patch = { 0 };
			struct strbuf root = STRBUF_INIT;
			int linenr = 0;
			int orig_len;

			in_header = 0;
			strbuf_addch(&buf, '\n');
			if (!util->diff_offset)
				util->diff_offset = buf.len;
			if (eol)
				*eol = '\n';
			orig_len = len;
			len = parse_git_diff_header(&root, &linenr, 0, line,
						    len, size, &patch);
			if (len < 0) {
				error(_("could not parse git header '%.*s'"),
				      orig_len, line);
				free(util);
				free(current_filename);
				string_list_clear(list, 1);
				strbuf_release(&buf);
				strbuf_release(&contents);
				return -1;
			}
			strbuf_addstr(&buf, " ## ");
			if (patch.is_new > 0)
				strbuf_addf(&buf, "%s (new)", patch.new_name);
			else if (patch.is_delete > 0)
				strbuf_addf(&buf, "%s (deleted)", patch.old_name);
			else if (patch.is_rename)
				strbuf_addf(&buf, "%s => %s", patch.old_name, patch.new_name);
			else
				strbuf_addstr(&buf, patch.new_name);

			free(current_filename);
			if (patch.is_delete > 0)
				current_filename = xstrdup(patch.old_name);
			else
				current_filename = xstrdup(patch.new_name);

			if (patch.new_mode && patch.old_mode &&
			    patch.old_mode != patch.new_mode)
				strbuf_addf(&buf, " (mode change %06o => %06o)",
					    patch.old_mode, patch.new_mode);

			strbuf_addstr(&buf, " ##");
		} else if (in_header) {
			if (starts_with(line, "Author: ")) {
				strbuf_addstr(&buf, " ## Metadata ##\n");
				strbuf_addstr(&buf, line);
				strbuf_addstr(&buf, "\n\n");
				strbuf_addstr(&buf, " ## Commit message ##\n");
			} else if (starts_with(line, "Notes") &&
				   line[strlen(line) - 1] == ':') {
				strbuf_addstr(&buf, "\n\n");
				/* strip the trailing colon */
				strbuf_addf(&buf, " ## %.*s ##\n",
					    (int)(strlen(line) - 1), line);
			} else if (starts_with(line, "    ")) {
				p = line + len - 2;
				while (isspace(*p) && p >= line)
					p--;
				strbuf_add(&buf, line, p - line + 1);
				strbuf_addch(&buf, '\n');
			}
			continue;
		} else if (skip_prefix(line, "@@ ", &p)) {
			p = strstr(p, "@@");
			strbuf_addstr(&buf, "@@");
			if (current_filename && p[2])
				strbuf_addf(&buf, " %s:", current_filename);
			if (p)
				strbuf_addstr(&buf, p + 2);
		} else if (!line[0])
			/*
			 * A completely blank (not ' \n', which is context)
			 * line is not valid in a diff.  We skip it
			 * silently, because this neatly handles the blank
			 * separator line between commits in git-log
			 * output.
			 */
			continue;
		else if (line[0] == '>') {
			strbuf_addch(&buf, '+');
			strbuf_addstr(&buf, line + 1);
		} else if (line[0] == '<') {
			strbuf_addch(&buf, '-');
			strbuf_addstr(&buf, line + 1);
		} else if (line[0] == '#') {
			strbuf_addch(&buf, ' ');
			strbuf_addstr(&buf, line + 1);
		} else {
			strbuf_addch(&buf, ' ');
			strbuf_addstr(&buf, line);
		}

		strbuf_addch(&buf, '\n');
		util->diffsize++;
	}
	strbuf_release(&contents);

	if (util)
		string_list_append(list, buf.buf)->util = util;
	strbuf_release(&buf);
	free(current_filename);

	return 0;
}

static int patch_util_cmp(const void *dummy, const struct patch_util *a,
			  const struct patch_util *b, const char *keydata)
{
	return strcmp(a->diff, keydata ? keydata : b->diff);
}

static void find_exact_matches(struct string_list *a, struct string_list *b)
{
	struct hashmap map = HASHMAP_INIT((hashmap_cmp_fn)patch_util_cmp, NULL);
	int i;

	/* First, add the patches of a to a hash map */
	for (i = 0; i < a->nr; i++) {
		struct patch_util *util = a->items[i].util;

		util->i = i;
		util->patch = a->items[i].string;
		util->diff = util->patch + util->diff_offset;
		hashmap_entry_init(&util->e, strhash(util->diff));
		hashmap_add(&map, &util->e);
	}

	/* Now try to find exact matches in b */
	for (i = 0; i < b->nr; i++) {
		struct patch_util *util = b->items[i].util, *other;

		util->i = i;
		util->patch = b->items[i].string;
		util->diff = util->patch + util->diff_offset;
		hashmap_entry_init(&util->e, strhash(util->diff));
		other = hashmap_remove_entry(&map, util, e, NULL);
		if (other) {
			if (other->matching >= 0)
				BUG("already assigned!");

			other->matching = i;
			util->matching = other->i;
		}
	}

	hashmap_clear(&map);
}

static int diffsize_consume(void *data, char *line, unsigned long len)
{
	(*(int *)data)++;
	return 0;
}

static void diffsize_hunk(void *data, long ob, long on, long nb, long nn,
			  const char *funcline, long funclen)
{
	diffsize_consume(data, NULL, 0);
}

static int diffsize(const char *a, const char *b)
{
	xpparam_t pp = { 0 };
	xdemitconf_t cfg = { 0 };
	mmfile_t mf1, mf2;
	int count = 0;

	mf1.ptr = (char *)a;
	mf1.size = strlen(a);
	mf2.ptr = (char *)b;
	mf2.size = strlen(b);

	cfg.ctxlen = 3;
	if (!xdi_diff_outf(&mf1, &mf2,
			   diffsize_hunk, diffsize_consume, &count,
			   &pp, &cfg))
		return count;

	error(_("failed to generate diff"));
	return COST_MAX;
}

static void get_correspondences(struct string_list *a, struct string_list *b,
				int creation_factor)
{
	int n = a->nr + b->nr;
	int *cost, c, *a2b, *b2a;
	int i, j;

	ALLOC_ARRAY(cost, st_mult(n, n));
	ALLOC_ARRAY(a2b, n);
	ALLOC_ARRAY(b2a, n);

	for (i = 0; i < a->nr; i++) {
		struct patch_util *a_util = a->items[i].util;

		for (j = 0; j < b->nr; j++) {
			struct patch_util *b_util = b->items[j].util;

			if (a_util->matching == j)
				c = 0;
			else if (a_util->matching < 0 && b_util->matching < 0)
				c = diffsize(a_util->diff, b_util->diff);
			else
				c = COST_MAX;
			cost[i + n * j] = c;
		}

		c = a_util->matching < 0 ?
			a_util->diffsize * creation_factor / 100 : COST_MAX;
		for (j = b->nr; j < n; j++)
			cost[i + n * j] = c;
	}

	for (j = 0; j < b->nr; j++) {
		struct patch_util *util = b->items[j].util;

		c = util->matching < 0 ?
			util->diffsize * creation_factor / 100 : COST_MAX;
		for (i = a->nr; i < n; i++)
			cost[i + n * j] = c;
	}

	for (i = a->nr; i < n; i++)
		for (j = b->nr; j < n; j++)
			cost[i + n * j] = 0;

	compute_assignment(n, n, cost, a2b, b2a);

	for (i = 0; i < a->nr; i++)
		if (a2b[i] >= 0 && a2b[i] < b->nr) {
			struct patch_util *a_util = a->items[i].util;
			struct patch_util *b_util = b->items[a2b[i]].util;

			a_util->matching = a2b[i];
			b_util->matching = i;
		}

	free(cost);
	free(a2b);
	free(b2a);
}

static void output_pair_header(struct diff_options *diffopt,
			       int patch_no_width,
			       struct strbuf *buf,
			       struct strbuf *dashes,
			       struct patch_util *a_util,
			       struct patch_util *b_util)
{
	struct object_id *oid = a_util ? &a_util->oid : &b_util->oid;
	struct commit *commit;
	char status;
	const char *color_reset = diff_get_color_opt(diffopt, DIFF_RESET);
	const char *color_old = diff_get_color_opt(diffopt, DIFF_FILE_OLD);
	const char *color_new = diff_get_color_opt(diffopt, DIFF_FILE_NEW);
	const char *color_commit = diff_get_color_opt(diffopt, DIFF_COMMIT);
	const char *color;

	if (!dashes->len)
		strbuf_addchars(dashes, '-',
				strlen(find_unique_abbrev(oid,
							  DEFAULT_ABBREV)));

	if (!b_util) {
		color = color_old;
		status = '<';
	} else if (!a_util) {
		color = color_new;
		status = '>';
	} else if (strcmp(a_util->patch, b_util->patch)) {
		color = color_commit;
		status = '!';
	} else {
		color = color_commit;
		status = '=';
	}

	strbuf_reset(buf);
	strbuf_addstr(buf, status == '!' ? color_old : color);
	if (!a_util)
		strbuf_addf(buf, "%*s:  %s ", patch_no_width, "-", dashes->buf);
	else
		strbuf_addf(buf, "%*d:  %s ", patch_no_width, a_util->i + 1,
			    find_unique_abbrev(&a_util->oid, DEFAULT_ABBREV));

	if (status == '!')
		strbuf_addf(buf, "%s%s", color_reset, color);
	strbuf_addch(buf, status);
	if (status == '!')
		strbuf_addf(buf, "%s%s", color_reset, color_new);

	if (!b_util)
		strbuf_addf(buf, " %*s:  %s", patch_no_width, "-", dashes->buf);
	else
		strbuf_addf(buf, " %*d:  %s", patch_no_width, b_util->i + 1,
			    find_unique_abbrev(&b_util->oid, DEFAULT_ABBREV));

	commit = lookup_commit_reference(the_repository, oid);
	if (commit) {
		if (status == '!')
			strbuf_addf(buf, "%s%s", color_reset, color);

		strbuf_addch(buf, ' ');
		pp_commit_easy(CMIT_FMT_ONELINE, commit, buf);
	}
	strbuf_addf(buf, "%s\n", color_reset);

	fwrite(buf->buf, buf->len, 1, diffopt->file);
}

static struct userdiff_driver section_headers = {
	.funcname = { "^ ## (.*) ##$\n"
		      "^.?@@ (.*)$", REG_EXTENDED }
};

static struct diff_filespec *get_filespec(const char *name, const char *p)
{
	struct diff_filespec *spec = alloc_filespec(name);

	fill_filespec(spec, null_oid(), 0, 0100644);
	spec->data = (char *)p;
	spec->size = strlen(p);
	spec->should_munmap = 0;
	spec->is_stdin = 1;
	spec->driver = &section_headers;

	return spec;
}

static void patch_diff(const char *a, const char *b,
		       struct diff_options *diffopt)
{
	diff_queue(&diff_queued_diff,
		   get_filespec("a", a), get_filespec("b", b));

	diffcore_std(diffopt);
	diff_flush(diffopt);
}

static struct strbuf *output_prefix_cb(struct diff_options *opt, void *data)
{
	return data;
}

static void output(struct string_list *a, struct string_list *b,
		   struct range_diff_options *range_diff_opts)
{
	struct strbuf buf = STRBUF_INIT, dashes = STRBUF_INIT;
	int patch_no_width = decimal_width(1 + (a->nr > b->nr ? a->nr : b->nr));
	int i = 0, j = 0;
	struct diff_options opts;
	struct strbuf indent = STRBUF_INIT;

	if (range_diff_opts->diffopt)
		memcpy(&opts, range_diff_opts->diffopt, sizeof(opts));
	else
		diff_setup(&opts);

	opts.no_free = 1;
	if (!opts.output_format)
		opts.output_format = DIFF_FORMAT_PATCH;
	opts.flags.suppress_diff_headers = 1;
	opts.flags.dual_color_diffed_diffs =
		range_diff_opts->dual_color;
	opts.flags.suppress_hunk_header_line_count = 1;
	opts.output_prefix = output_prefix_cb;
	strbuf_addstr(&indent, "    ");
	opts.output_prefix_data = &indent;
	diff_setup_done(&opts);

	/*
	 * We assume the user is really more interested in the second argument
	 * ("newer" version). To that end, we print the output in the order of
	 * the RHS (the `b` parameter). To put the LHS (the `a` parameter)
	 * commits that are no longer in the RHS into a good place, we place
	 * them once we have shown all of their predecessors in the LHS.
	 */

	while (i < a->nr || j < b->nr) {
		struct patch_util *a_util, *b_util;
		a_util = i < a->nr ? a->items[i].util : NULL;
		b_util = j < b->nr ? b->items[j].util : NULL;

		/* Skip all the already-shown commits from the LHS. */
		while (i < a->nr && a_util->shown)
			a_util = ++i < a->nr ? a->items[i].util : NULL;

		/* Show unmatched LHS commit whose predecessors were shown. */
		if (i < a->nr && a_util->matching < 0) {
			if (!range_diff_opts->right_only)
				output_pair_header(&opts, patch_no_width,
					   &buf, &dashes, a_util, NULL);
			i++;
			continue;
		}

		/* Show unmatched RHS commits. */
		while (j < b->nr && b_util->matching < 0) {
			if (!range_diff_opts->left_only)
				output_pair_header(&opts, patch_no_width,
					   &buf, &dashes, NULL, b_util);
			b_util = ++j < b->nr ? b->items[j].util : NULL;
		}

		/* Show matching LHS/RHS pair. */
		if (j < b->nr) {
			a_util = a->items[b_util->matching].util;
			output_pair_header(&opts, patch_no_width,
					   &buf, &dashes, a_util, b_util);
			if (!(opts.output_format & DIFF_FORMAT_NO_OUTPUT))
				patch_diff(a->items[b_util->matching].string,
					   b->items[j].string, &opts);
			a_util->shown = 1;
			j++;
		}
	}
	strbuf_release(&buf);
	strbuf_release(&dashes);
	strbuf_release(&indent);
	opts.no_free = 0;
	diff_free(&opts);
}

int show_range_diff(const char *range1, const char *range2,
		    struct range_diff_options *range_diff_opts)
{
	int res = 0;

	struct string_list branch1 = STRING_LIST_INIT_DUP;
	struct string_list branch2 = STRING_LIST_INIT_DUP;

	if (range_diff_opts->left_only && range_diff_opts->right_only)
		res = error(_("--left-only and --right-only are mutually exclusive"));

	if (!res && read_patches(range1, &branch1, range_diff_opts->other_arg))
		res = error(_("could not parse log for '%s'"), range1);
	if (!res && read_patches(range2, &branch2, range_diff_opts->other_arg))
		res = error(_("could not parse log for '%s'"), range2);

	if (!res) {
		find_exact_matches(&branch1, &branch2);
		get_correspondences(&branch1, &branch2,
				    range_diff_opts->creation_factor);
		output(&branch1, &branch2, range_diff_opts);
	}

	string_list_clear(&branch1, 1);
	string_list_clear(&branch2, 1);

	return res;
}

int is_range_diff_range(const char *arg)
{
	char *copy = xstrdup(arg); /* setup_revisions() modifies it */
	const char *argv[] = { "", copy, "--", NULL };
	int i, positive = 0, negative = 0;
	struct rev_info revs;

	init_revisions(&revs, NULL);
	if (setup_revisions(3, argv, &revs, NULL) == 1) {
		for (i = 0; i < revs.pending.nr; i++)
			if (revs.pending.objects[i].item->flags & UNINTERESTING)
				negative++;
			else
				positive++;
		for (i = 0; i < revs.pending.nr; i++) {
			struct object *obj = revs.pending.objects[i].item;

			if (obj->type == OBJ_COMMIT)
				clear_commit_marks((struct commit *)obj,
						   ALL_REV_FLAGS);
		}
	}

	free(copy);
	object_array_clear(&revs.pending);
	return negative > 0 && positive > 0;
}
