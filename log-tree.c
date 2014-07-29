#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "tag.h"
#include "graph.h"
#include "log-tree.h"
#include "reflog-walk.h"
#include "refs.h"
#include "string-list.h"
#include "color.h"
#include "gpg-interface.h"
#include "sequencer.h"
#include "line-log.h"

struct decoration name_decoration = { "object names" };

enum decoration_type {
	DECORATION_NONE = 0,
	DECORATION_REF_LOCAL,
	DECORATION_REF_REMOTE,
	DECORATION_REF_TAG,
	DECORATION_REF_STASH,
	DECORATION_REF_HEAD,
	DECORATION_GRAFTED,
};

static char decoration_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_BOLD_GREEN,	/* REF_LOCAL */
	GIT_COLOR_BOLD_RED,	/* REF_REMOTE */
	GIT_COLOR_BOLD_YELLOW,	/* REF_TAG */
	GIT_COLOR_BOLD_MAGENTA,	/* REF_STASH */
	GIT_COLOR_BOLD_CYAN,	/* REF_HEAD */
	GIT_COLOR_BOLD_BLUE,	/* GRAFTED */
};

static const char *decorate_get_color(int decorate_use_color, enum decoration_type ix)
{
	if (want_color(decorate_use_color))
		return decoration_colors[ix];
	return "";
}

static int parse_decorate_color_slot(const char *slot)
{
	/*
	 * We're comparing with 'ignore-case' on
	 * (because config.c sets them all tolower),
	 * but let's match the letters in the literal
	 * string values here with how they are
	 * documented in Documentation/config.txt, for
	 * consistency.
	 *
	 * We love being consistent, don't we?
	 */
	if (!strcasecmp(slot, "branch"))
		return DECORATION_REF_LOCAL;
	if (!strcasecmp(slot, "remoteBranch"))
		return DECORATION_REF_REMOTE;
	if (!strcasecmp(slot, "tag"))
		return DECORATION_REF_TAG;
	if (!strcasecmp(slot, "stash"))
		return DECORATION_REF_STASH;
	if (!strcasecmp(slot, "HEAD"))
		return DECORATION_REF_HEAD;
	return -1;
}

int parse_decorate_color_config(const char *var, const int ofs, const char *value)
{
	int slot = parse_decorate_color_slot(var + ofs);
	if (slot < 0)
		return 0;
	if (!value)
		return config_error_nonbool(var);
	color_parse(value, var, decoration_colors[slot]);
	return 0;
}

/*
 * log-tree.c uses DIFF_OPT_TST for determining whether to use color
 * for showing the commit sha1, use the same check for --decorate
 */
#define decorate_get_color_opt(o, ix) \
	decorate_get_color((o)->use_color, ix)

static void add_name_decoration(enum decoration_type type, const char *name, struct object *obj)
{
	int nlen = strlen(name);
	struct name_decoration *res = xmalloc(sizeof(struct name_decoration) + nlen);
	memcpy(res->name, name, nlen + 1);
	res->type = type;
	res->next = add_decoration(&name_decoration, obj, res);
}

static int add_ref_decoration(const char *refname, const unsigned char *sha1, int flags, void *cb_data)
{
	struct object *obj;
	enum decoration_type type = DECORATION_NONE;

	if (starts_with(refname, "refs/replace/")) {
		unsigned char original_sha1[20];
		if (!check_replace_refs)
			return 0;
		if (get_sha1_hex(refname + 13, original_sha1)) {
			warning("invalid replace ref %s", refname);
			return 0;
		}
		obj = parse_object(original_sha1);
		if (obj)
			add_name_decoration(DECORATION_GRAFTED, "replaced", obj);
		return 0;
	}

	obj = parse_object(sha1);
	if (!obj)
		return 0;

	if (starts_with(refname, "refs/heads/"))
		type = DECORATION_REF_LOCAL;
	else if (starts_with(refname, "refs/remotes/"))
		type = DECORATION_REF_REMOTE;
	else if (starts_with(refname, "refs/tags/"))
		type = DECORATION_REF_TAG;
	else if (!strcmp(refname, "refs/stash"))
		type = DECORATION_REF_STASH;
	else if (!strcmp(refname, "HEAD"))
		type = DECORATION_REF_HEAD;

	if (!cb_data || *(int *)cb_data == DECORATE_SHORT_REFS)
		refname = prettify_refname(refname);
	add_name_decoration(type, refname, obj);
	while (obj->type == OBJ_TAG) {
		obj = ((struct tag *)obj)->tagged;
		if (!obj)
			break;
		if (!obj->parsed)
			parse_object(obj->sha1);
		add_name_decoration(DECORATION_REF_TAG, refname, obj);
	}
	return 0;
}

static int add_graft_decoration(const struct commit_graft *graft, void *cb_data)
{
	struct commit *commit = lookup_commit(graft->sha1);
	if (!commit)
		return 0;
	add_name_decoration(DECORATION_GRAFTED, "grafted", &commit->object);
	return 0;
}

void load_ref_decorations(int flags)
{
	static int loaded;
	if (!loaded) {
		loaded = 1;
		for_each_ref(add_ref_decoration, &flags);
		head_ref(add_ref_decoration, &flags);
		for_each_commit_graft(add_graft_decoration, NULL);
	}
}

static void show_parents(struct commit *commit, int abbrev)
{
	struct commit_list *p;
	for (p = commit->parents; p ; p = p->next) {
		struct commit *parent = p->item;
		printf(" %s", find_unique_abbrev(parent->object.sha1, abbrev));
	}
}

static void show_children(struct rev_info *opt, struct commit *commit, int abbrev)
{
	struct commit_list *p = lookup_decoration(&opt->children, &commit->object);
	for ( ; p; p = p->next) {
		printf(" %s", find_unique_abbrev(p->item->object.sha1, abbrev));
	}
}

/*
 * The caller makes sure there is no funny color before
 * calling. format_decorations makes sure the same after return.
 */
void format_decorations(struct strbuf *sb,
			const struct commit *commit,
			int use_color)
{
	const char *prefix;
	struct name_decoration *decoration;
	const char *color_commit =
		diff_get_color(use_color, DIFF_COMMIT);
	const char *color_reset =
		decorate_get_color(use_color, DECORATION_NONE);

	decoration = lookup_decoration(&name_decoration, &commit->object);
	if (!decoration)
		return;
	prefix = " (";
	while (decoration) {
		strbuf_addstr(sb, color_commit);
		strbuf_addstr(sb, prefix);
		strbuf_addstr(sb, decorate_get_color(use_color, decoration->type));
		if (decoration->type == DECORATION_REF_TAG)
			strbuf_addstr(sb, "tag: ");
		strbuf_addstr(sb, decoration->name);
		strbuf_addstr(sb, color_reset);
		prefix = ", ";
		decoration = decoration->next;
	}
	strbuf_addstr(sb, color_commit);
	strbuf_addch(sb, ')');
	strbuf_addstr(sb, color_reset);
}

void show_decorations(struct rev_info *opt, struct commit *commit)
{
	struct strbuf sb = STRBUF_INIT;

	if (opt->show_source && commit->util)
		printf("\t%s", (char *) commit->util);
	if (!opt->show_decorations)
		return;
	format_decorations(&sb, commit, opt->diffopt.use_color);
	fputs(sb.buf, stdout);
	strbuf_release(&sb);
}

static unsigned int digits_in_number(unsigned int number)
{
	unsigned int i = 10, result = 1;
	while (i <= number) {
		i *= 10;
		result++;
	}
	return result;
}

void fmt_output_subject(struct strbuf *filename,
			const char *subject,
			struct rev_info *info)
{
	const char *suffix = info->patch_suffix;
	int nr = info->nr;
	int start_len = filename->len;
	int max_len = start_len + FORMAT_PATCH_NAME_MAX - (strlen(suffix) + 1);

	if (0 < info->reroll_count)
		strbuf_addf(filename, "v%d-", info->reroll_count);
	strbuf_addf(filename, "%04d-%s", nr, subject);

	if (max_len < filename->len)
		strbuf_setlen(filename, max_len);
	strbuf_addstr(filename, suffix);
}

void fmt_output_commit(struct strbuf *filename,
		       struct commit *commit,
		       struct rev_info *info)
{
	struct pretty_print_context ctx = {0};
	struct strbuf subject = STRBUF_INIT;

	format_commit_message(commit, "%f", &subject, &ctx);
	fmt_output_subject(filename, subject.buf, info);
	strbuf_release(&subject);
}

void log_write_email_headers(struct rev_info *opt, struct commit *commit,
			     const char **subject_p,
			     const char **extra_headers_p,
			     int *need_8bit_cte_p)
{
	const char *subject = NULL;
	const char *extra_headers = opt->extra_headers;
	const char *name = sha1_to_hex(commit->object.sha1);

	*need_8bit_cte_p = 0; /* unknown */
	if (opt->total > 0) {
		static char buffer[64];
		snprintf(buffer, sizeof(buffer),
			 "Subject: [%s%s%0*d/%d] ",
			 opt->subject_prefix,
			 *opt->subject_prefix ? " " : "",
			 digits_in_number(opt->total),
			 opt->nr, opt->total);
		subject = buffer;
	} else if (opt->total == 0 && opt->subject_prefix && *opt->subject_prefix) {
		static char buffer[256];
		snprintf(buffer, sizeof(buffer),
			 "Subject: [%s] ",
			 opt->subject_prefix);
		subject = buffer;
	} else {
		subject = "Subject: ";
	}

	printf("From %s Mon Sep 17 00:00:00 2001\n", name);
	graph_show_oneline(opt->graph);
	if (opt->message_id) {
		printf("Message-Id: <%s>\n", opt->message_id);
		graph_show_oneline(opt->graph);
	}
	if (opt->ref_message_ids && opt->ref_message_ids->nr > 0) {
		int i, n;
		n = opt->ref_message_ids->nr;
		printf("In-Reply-To: <%s>\n", opt->ref_message_ids->items[n-1].string);
		for (i = 0; i < n; i++)
			printf("%s<%s>\n", (i > 0 ? "\t" : "References: "),
			       opt->ref_message_ids->items[i].string);
		graph_show_oneline(opt->graph);
	}
	if (opt->mime_boundary) {
		static char subject_buffer[1024];
		static char buffer[1024];
		struct strbuf filename =  STRBUF_INIT;
		*need_8bit_cte_p = -1; /* NEVER */
		snprintf(subject_buffer, sizeof(subject_buffer) - 1,
			 "%s"
			 "MIME-Version: 1.0\n"
			 "Content-Type: multipart/mixed;"
			 " boundary=\"%s%s\"\n"
			 "\n"
			 "This is a multi-part message in MIME "
			 "format.\n"
			 "--%s%s\n"
			 "Content-Type: text/plain; "
			 "charset=UTF-8; format=fixed\n"
			 "Content-Transfer-Encoding: 8bit\n\n",
			 extra_headers ? extra_headers : "",
			 mime_boundary_leader, opt->mime_boundary,
			 mime_boundary_leader, opt->mime_boundary);
		extra_headers = subject_buffer;

		if (opt->numbered_files)
			strbuf_addf(&filename, "%d", opt->nr);
		else
			fmt_output_commit(&filename, commit, opt);
		snprintf(buffer, sizeof(buffer) - 1,
			 "\n--%s%s\n"
			 "Content-Type: text/x-patch;"
			 " name=\"%s\"\n"
			 "Content-Transfer-Encoding: 8bit\n"
			 "Content-Disposition: %s;"
			 " filename=\"%s\"\n\n",
			 mime_boundary_leader, opt->mime_boundary,
			 filename.buf,
			 opt->no_inline ? "attachment" : "inline",
			 filename.buf);
		opt->diffopt.stat_sep = buffer;
		strbuf_release(&filename);
	}
	*subject_p = subject;
	*extra_headers_p = extra_headers;
}

static void show_sig_lines(struct rev_info *opt, int status, const char *bol)
{
	const char *color, *reset, *eol;

	color = diff_get_color_opt(&opt->diffopt,
				   status ? DIFF_WHITESPACE : DIFF_FRAGINFO);
	reset = diff_get_color_opt(&opt->diffopt, DIFF_RESET);
	while (*bol) {
		eol = strchrnul(bol, '\n');
		printf("%s%.*s%s%s", color, (int)(eol - bol), bol, reset,
		       *eol ? "\n" : "");
		graph_show_oneline(opt->graph);
		bol = (*eol) ? (eol + 1) : eol;
	}
}

static void show_signature(struct rev_info *opt, struct commit *commit)
{
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;
	struct strbuf gpg_output = STRBUF_INIT;
	int status;

	if (parse_signed_commit(commit, &payload, &signature) <= 0)
		goto out;

	status = verify_signed_buffer(payload.buf, payload.len,
				      signature.buf, signature.len,
				      &gpg_output, NULL);
	if (status && !gpg_output.len)
		strbuf_addstr(&gpg_output, "No signature\n");

	show_sig_lines(opt, status, gpg_output.buf);

 out:
	strbuf_release(&gpg_output);
	strbuf_release(&payload);
	strbuf_release(&signature);
}

static int which_parent(const unsigned char *sha1, const struct commit *commit)
{
	int nth;
	const struct commit_list *parent;

	for (nth = 0, parent = commit->parents; parent; parent = parent->next) {
		if (!hashcmp(parent->item->object.sha1, sha1))
			return nth;
		nth++;
	}
	return -1;
}

static int is_common_merge(const struct commit *commit)
{
	return (commit->parents
		&& commit->parents->next
		&& !commit->parents->next->next);
}

static void show_one_mergetag(struct commit *commit,
			      struct commit_extra_header *extra,
			      void *data)
{
	struct rev_info *opt = (struct rev_info *)data;
	unsigned char sha1[20];
	struct tag *tag;
	struct strbuf verify_message;
	int status, nth;
	size_t payload_size, gpg_message_offset;

	hash_sha1_file(extra->value, extra->len, typename(OBJ_TAG), sha1);
	tag = lookup_tag(sha1);
	if (!tag)
		return; /* error message already given */

	strbuf_init(&verify_message, 256);
	if (parse_tag_buffer(tag, extra->value, extra->len))
		strbuf_addstr(&verify_message, "malformed mergetag\n");
	else if (is_common_merge(commit) &&
		 !hashcmp(tag->tagged->sha1,
			  commit->parents->next->item->object.sha1))
		strbuf_addf(&verify_message,
			    "merged tag '%s'\n", tag->tag);
	else if ((nth = which_parent(tag->tagged->sha1, commit)) < 0)
		strbuf_addf(&verify_message, "tag %s names a non-parent %s\n",
				    tag->tag, tag->tagged->sha1);
	else
		strbuf_addf(&verify_message,
			    "parent #%d, tagged '%s'\n", nth + 1, tag->tag);
	gpg_message_offset = verify_message.len;

	payload_size = parse_signature(extra->value, extra->len);
	status = -1;
	if (extra->len > payload_size) {
		/* could have a good signature */
		if (!verify_signed_buffer(extra->value, payload_size,
					  extra->value + payload_size,
					  extra->len - payload_size,
					  &verify_message, NULL))
			status = 0; /* good */
		else if (verify_message.len <= gpg_message_offset)
			strbuf_addstr(&verify_message, "No signature\n");
		/* otherwise we couldn't verify, which is shown as bad */
	}

	show_sig_lines(opt, status, verify_message.buf);
	strbuf_release(&verify_message);
}

static void show_mergetag(struct rev_info *opt, struct commit *commit)
{
	for_each_mergetag(show_one_mergetag, commit, opt);
}

void show_log(struct rev_info *opt)
{
	struct strbuf msgbuf = STRBUF_INIT;
	struct log_info *log = opt->loginfo;
	struct commit *commit = log->commit, *parent = log->parent;
	int abbrev_commit = opt->abbrev_commit ? opt->abbrev : 40;
	const char *extra_headers = opt->extra_headers;
	struct pretty_print_context ctx = {0};

	opt->loginfo = NULL;
	if (!opt->verbose_header) {
		graph_show_commit(opt->graph);

		if (!opt->graph)
			put_revision_mark(opt, commit);
		fputs(find_unique_abbrev(commit->object.sha1, abbrev_commit), stdout);
		if (opt->print_parents)
			show_parents(commit, abbrev_commit);
		if (opt->children.name)
			show_children(opt, commit, abbrev_commit);
		show_decorations(opt, commit);
		if (opt->graph && !graph_is_commit_finished(opt->graph)) {
			putchar('\n');
			graph_show_remainder(opt->graph);
		}
		putchar(opt->diffopt.line_termination);
		return;
	}

	/*
	 * If use_terminator is set, we already handled any record termination
	 * at the end of the last record.
	 * Otherwise, add a diffopt.line_termination character before all
	 * entries but the first.  (IOW, as a separator between entries)
	 */
	if (opt->shown_one && !opt->use_terminator) {
		/*
		 * If entries are separated by a newline, the output
		 * should look human-readable.  If the last entry ended
		 * with a newline, print the graph output before this
		 * newline.  Otherwise it will end up as a completely blank
		 * line and will look like a gap in the graph.
		 *
		 * If the entry separator is not a newline, the output is
		 * primarily intended for programmatic consumption, and we
		 * never want the extra graph output before the entry
		 * separator.
		 */
		if (opt->diffopt.line_termination == '\n' &&
		    !opt->missing_newline)
			graph_show_padding(opt->graph);
		putchar(opt->diffopt.line_termination);
	}
	opt->shown_one = 1;

	/*
	 * If the history graph was requested,
	 * print the graph, up to this commit's line
	 */
	graph_show_commit(opt->graph);

	/*
	 * Print header line of header..
	 */

	if (opt->commit_format == CMIT_FMT_EMAIL) {
		log_write_email_headers(opt, commit, &ctx.subject, &extra_headers,
					&ctx.need_8bit_cte);
	} else if (opt->commit_format != CMIT_FMT_USERFORMAT) {
		fputs(diff_get_color_opt(&opt->diffopt, DIFF_COMMIT), stdout);
		if (opt->commit_format != CMIT_FMT_ONELINE)
			fputs("commit ", stdout);

		if (!opt->graph)
			put_revision_mark(opt, commit);
		fputs(find_unique_abbrev(commit->object.sha1, abbrev_commit),
		      stdout);
		if (opt->print_parents)
			show_parents(commit, abbrev_commit);
		if (opt->children.name)
			show_children(opt, commit, abbrev_commit);
		if (parent)
			printf(" (from %s)",
			       find_unique_abbrev(parent->object.sha1,
						  abbrev_commit));
		fputs(diff_get_color_opt(&opt->diffopt, DIFF_RESET), stdout);
		show_decorations(opt, commit);
		if (opt->commit_format == CMIT_FMT_ONELINE) {
			putchar(' ');
		} else {
			putchar('\n');
			graph_show_oneline(opt->graph);
		}
		if (opt->reflog_info) {
			/*
			 * setup_revisions() ensures that opt->reflog_info
			 * and opt->graph cannot both be set,
			 * so we don't need to worry about printing the
			 * graph info here.
			 */
			show_reflog_message(opt->reflog_info,
					    opt->commit_format == CMIT_FMT_ONELINE,
					    opt->date_mode,
					    opt->date_mode_explicit);
			if (opt->commit_format == CMIT_FMT_ONELINE)
				return;
		}
	}

	if (opt->show_signature) {
		show_signature(opt, commit);
		show_mergetag(opt, commit);
	}

	if (!get_cached_commit_buffer(commit, NULL))
		return;

	if (opt->show_notes) {
		int raw;
		struct strbuf notebuf = STRBUF_INIT;

		raw = (opt->commit_format == CMIT_FMT_USERFORMAT);
		format_display_notes(commit->object.sha1, &notebuf,
				     get_log_output_encoding(), raw);
		ctx.notes_message = notebuf.len
			? strbuf_detach(&notebuf, NULL)
			: xcalloc(1, 1);
	}

	/*
	 * And then the pretty-printed message itself
	 */
	if (ctx.need_8bit_cte >= 0 && opt->add_signoff)
		ctx.need_8bit_cte =
			has_non_ascii(fmt_name(getenv("GIT_COMMITTER_NAME"),
					       getenv("GIT_COMMITTER_EMAIL")));
	ctx.date_mode = opt->date_mode;
	ctx.date_mode_explicit = opt->date_mode_explicit;
	ctx.abbrev = opt->diffopt.abbrev;
	ctx.after_subject = extra_headers;
	ctx.preserve_subject = opt->preserve_subject;
	ctx.reflog_info = opt->reflog_info;
	ctx.fmt = opt->commit_format;
	ctx.mailmap = opt->mailmap;
	ctx.color = opt->diffopt.use_color;
	ctx.output_encoding = get_log_output_encoding();
	if (opt->from_ident.mail_begin && opt->from_ident.name_begin)
		ctx.from_ident = &opt->from_ident;
	pretty_print_commit(&ctx, commit, &msgbuf);

	if (opt->add_signoff)
		append_signoff(&msgbuf, 0, APPEND_SIGNOFF_DEDUP);

	if ((ctx.fmt != CMIT_FMT_USERFORMAT) &&
	    ctx.notes_message && *ctx.notes_message) {
		if (ctx.fmt == CMIT_FMT_EMAIL) {
			strbuf_addstr(&msgbuf, "---\n");
			opt->shown_dashes = 1;
		}
		strbuf_addstr(&msgbuf, ctx.notes_message);
	}

	if (opt->show_log_size) {
		printf("log size %i\n", (int)msgbuf.len);
		graph_show_oneline(opt->graph);
	}

	/*
	 * Set opt->missing_newline if msgbuf doesn't
	 * end in a newline (including if it is empty)
	 */
	if (!msgbuf.len || msgbuf.buf[msgbuf.len - 1] != '\n')
		opt->missing_newline = 1;
	else
		opt->missing_newline = 0;

	if (opt->graph)
		graph_show_commit_msg(opt->graph, &msgbuf);
	else
		fwrite(msgbuf.buf, sizeof(char), msgbuf.len, stdout);
	if (opt->use_terminator && !commit_format_is_empty(opt->commit_format)) {
		if (!opt->missing_newline)
			graph_show_padding(opt->graph);
		putchar(opt->diffopt.line_termination);
	}

	strbuf_release(&msgbuf);
	free(ctx.notes_message);
}

int log_tree_diff_flush(struct rev_info *opt)
{
	opt->shown_dashes = 0;
	diffcore_std(&opt->diffopt);

	if (diff_queue_is_empty()) {
		int saved_fmt = opt->diffopt.output_format;
		opt->diffopt.output_format = DIFF_FORMAT_NO_OUTPUT;
		diff_flush(&opt->diffopt);
		opt->diffopt.output_format = saved_fmt;
		return 0;
	}

	if (opt->loginfo && !opt->no_commit_id) {
		show_log(opt);
		if ((opt->diffopt.output_format & ~DIFF_FORMAT_NO_OUTPUT) &&
		    opt->verbose_header &&
		    opt->commit_format != CMIT_FMT_ONELINE &&
		    !commit_format_is_empty(opt->commit_format)) {
			/*
			 * When showing a verbose header (i.e. log message),
			 * and not in --pretty=oneline format, we would want
			 * an extra newline between the end of log and the
			 * diff/diffstat output for readability.
			 */
			int pch = DIFF_FORMAT_DIFFSTAT | DIFF_FORMAT_PATCH;
			if (opt->diffopt.output_prefix) {
				struct strbuf *msg = NULL;
				msg = opt->diffopt.output_prefix(&opt->diffopt,
					opt->diffopt.output_prefix_data);
				fwrite(msg->buf, msg->len, 1, stdout);
			}

			/*
			 * We may have shown three-dashes line early
			 * between notes and the log message, in which
			 * case we only want a blank line after the
			 * notes without (an extra) three-dashes line.
			 * Otherwise, we show the three-dashes line if
			 * we are showing the patch with diffstat, but
			 * in that case, there is no extra blank line
			 * after the three-dashes line.
			 */
			if (!opt->shown_dashes &&
			    (pch & opt->diffopt.output_format) == pch)
				printf("---");
			putchar('\n');
		}
	}
	diff_flush(&opt->diffopt);
	return 1;
}

static int do_diff_combined(struct rev_info *opt, struct commit *commit)
{
	diff_tree_combined_merge(commit, opt->dense_combined_merges, opt);
	return !opt->loginfo;
}

/*
 * Show the diff of a commit.
 *
 * Return true if we printed any log info messages
 */
static int log_tree_diff(struct rev_info *opt, struct commit *commit, struct log_info *log)
{
	int showed_log;
	struct commit_list *parents;
	unsigned const char *sha1;

	if (!opt->diff && !DIFF_OPT_TST(&opt->diffopt, EXIT_WITH_STATUS))
		return 0;

	parse_commit_or_die(commit);
	sha1 = commit->tree->object.sha1;

	/* Root commit? */
	parents = get_saved_parents(opt, commit);
	if (!parents) {
		if (opt->show_root_diff) {
			diff_root_tree_sha1(sha1, "", &opt->diffopt);
			log_tree_diff_flush(opt);
		}
		return !opt->loginfo;
	}

	/* More than one parent? */
	if (parents && parents->next) {
		if (opt->ignore_merges)
			return 0;
		else if (opt->combine_merges)
			return do_diff_combined(opt, commit);
		else if (opt->first_parent_only) {
			/*
			 * Generate merge log entry only for the first
			 * parent, showing summary diff of the others
			 * we merged _in_.
			 */
			parse_commit_or_die(parents->item);
			diff_tree_sha1(parents->item->tree->object.sha1,
				       sha1, "", &opt->diffopt);
			log_tree_diff_flush(opt);
			return !opt->loginfo;
		}

		/* If we show individual diffs, show the parent info */
		log->parent = parents->item;
	}

	showed_log = 0;
	for (;;) {
		struct commit *parent = parents->item;

		parse_commit_or_die(parent);
		diff_tree_sha1(parent->tree->object.sha1,
			       sha1, "", &opt->diffopt);
		log_tree_diff_flush(opt);

		showed_log |= !opt->loginfo;

		/* Set up the log info for the next parent, if any.. */
		parents = parents->next;
		if (!parents)
			break;
		log->parent = parents->item;
		opt->loginfo = log;
	}
	return showed_log;
}

int log_tree_commit(struct rev_info *opt, struct commit *commit)
{
	struct log_info log;
	int shown;

	log.commit = commit;
	log.parent = NULL;
	opt->loginfo = &log;

	if (opt->line_level_traverse)
		return line_log_print(opt, commit);

	if (opt->track_linear && !opt->linear && !opt->reverse_output_stage)
		printf("\n%s\n", opt->break_bar);
	shown = log_tree_diff(opt, commit, &log);
	if (!shown && opt->loginfo && opt->always_show_header) {
		log.parent = NULL;
		show_log(opt);
		shown = 1;
	}
	if (opt->track_linear && !opt->linear && opt->reverse_output_stage)
		printf("\n%s\n", opt->break_bar);
	opt->loginfo = NULL;
	maybe_flush_or_die(stdout, "stdout");
	return shown;
}
