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

struct decoration name_decoration = { "object names" };

enum decoration_type {
	DECORATION_NONE = 0,
	DECORATION_REF_LOCAL,
	DECORATION_REF_REMOTE,
	DECORATION_REF_TAG,
	DECORATION_REF_STASH,
	DECORATION_REF_HEAD,
};

static char decoration_colors[][COLOR_MAXLEN] = {
	GIT_COLOR_RESET,
	GIT_COLOR_BOLD_GREEN,	/* REF_LOCAL */
	GIT_COLOR_BOLD_RED,	/* REF_REMOTE */
	GIT_COLOR_BOLD_YELLOW,	/* REF_TAG */
	GIT_COLOR_BOLD_MAGENTA,	/* REF_STASH */
	GIT_COLOR_BOLD_CYAN,	/* REF_HEAD */
};

static const char *decorate_get_color(int decorate_use_color, enum decoration_type ix)
{
	if (decorate_use_color)
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
	decorate_get_color(DIFF_OPT_TST((o), COLOR_DIFF), ix)

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
	struct object *obj = parse_object(sha1);
	enum decoration_type type = DECORATION_NONE;
	if (!obj)
		return 0;

	if (!prefixcmp(refname, "refs/heads"))
		type = DECORATION_REF_LOCAL;
	else if (!prefixcmp(refname, "refs/remotes"))
		type = DECORATION_REF_REMOTE;
	else if (!prefixcmp(refname, "refs/tags"))
		type = DECORATION_REF_TAG;
	else if (!prefixcmp(refname, "refs/stash"))
		type = DECORATION_REF_STASH;
	else if (!prefixcmp(refname, "HEAD"))
		type = DECORATION_REF_HEAD;

	if (!cb_data || *(int *)cb_data == DECORATE_SHORT_REFS)
		refname = prettify_refname(refname);
	add_name_decoration(type, refname, obj);
	while (obj->type == OBJ_TAG) {
		obj = ((struct tag *)obj)->tagged;
		if (!obj)
			break;
		add_name_decoration(DECORATION_REF_TAG, refname, obj);
	}
	return 0;
}

void load_ref_decorations(int flags)
{
	static int loaded;
	if (!loaded) {
		loaded = 1;
		for_each_ref(add_ref_decoration, &flags);
		head_ref(add_ref_decoration, &flags);
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

void show_decorations(struct rev_info *opt, struct commit *commit)
{
	const char *prefix;
	struct name_decoration *decoration;
	const char *color_commit =
		diff_get_color_opt(&opt->diffopt, DIFF_COMMIT);
	const char *color_reset =
		decorate_get_color_opt(&opt->diffopt, DECORATION_NONE);

	if (opt->show_source && commit->util)
		printf("\t%s", (char *) commit->util);
	if (!opt->show_decorations)
		return;
	decoration = lookup_decoration(&name_decoration, &commit->object);
	if (!decoration)
		return;
	prefix = " (";
	while (decoration) {
		printf("%s", prefix);
		fputs(decorate_get_color_opt(&opt->diffopt, decoration->type),
		      stdout);
		if (decoration->type == DECORATION_REF_TAG)
			fputs("tag: ", stdout);
		printf("%s", decoration->name);
		fputs(color_reset, stdout);
		fputs(color_commit, stdout);
		prefix = ", ";
		decoration = decoration->next;
	}
	putchar(')');
}

/*
 * Search for "^[-A-Za-z]+: [^@]+@" pattern. It usually matches
 * Signed-off-by: and Acked-by: lines.
 */
static int detect_any_signoff(char *letter, int size)
{
	char *cp;
	int seen_colon = 0;
	int seen_at = 0;
	int seen_name = 0;
	int seen_head = 0;

	cp = letter + size;
	while (letter <= --cp && *cp == '\n')
		continue;

	while (letter <= cp) {
		char ch = *cp--;
		if (ch == '\n')
			break;

		if (!seen_at) {
			if (ch == '@')
				seen_at = 1;
			continue;
		}
		if (!seen_colon) {
			if (ch == '@')
				return 0;
			else if (ch == ':')
				seen_colon = 1;
			else
				seen_name = 1;
			continue;
		}
		if (('A' <= ch && ch <= 'Z') ||
		    ('a' <= ch && ch <= 'z') ||
		    ch == '-') {
			seen_head = 1;
			continue;
		}
		/* no empty last line doesn't match */
		return 0;
	}
	return seen_head && seen_name;
}

static void append_signoff(struct strbuf *sb, const char *signoff)
{
	static const char signed_off_by[] = "Signed-off-by: ";
	size_t signoff_len = strlen(signoff);
	int has_signoff = 0;
	char *cp;

	cp = sb->buf;

	/* First see if we already have the sign-off by the signer */
	while ((cp = strstr(cp, signed_off_by))) {

		has_signoff = 1;

		cp += strlen(signed_off_by);
		if (cp + signoff_len >= sb->buf + sb->len)
			break;
		if (strncmp(cp, signoff, signoff_len))
			continue;
		if (!isspace(cp[signoff_len]))
			continue;
		/* we already have him */
		return;
	}

	if (!has_signoff)
		has_signoff = detect_any_signoff(sb->buf, sb->len);

	if (!has_signoff)
		strbuf_addch(sb, '\n');

	strbuf_addstr(sb, signed_off_by);
	strbuf_add(sb, signoff, signoff_len);
	strbuf_addch(sb, '\n');
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

void get_patch_filename(struct commit *commit, int nr, const char *suffix,
			struct strbuf *buf)
{
	int suffix_len = strlen(suffix) + 1;
	int start_len = buf->len;

	strbuf_addf(buf, commit ? "%04d-" : "%d", nr);
	if (commit) {
		int max_len = start_len + FORMAT_PATCH_NAME_MAX - suffix_len;
		struct pretty_print_context ctx = {0};
		ctx.date_mode = DATE_NORMAL;

		format_commit_message(commit, "%f", buf, &ctx);
		if (max_len < buf->len)
			strbuf_setlen(buf, max_len);
		strbuf_addstr(buf, suffix);
	}
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
			 "Subject: [%s %0*d/%d] ",
			 opt->subject_prefix,
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

		get_patch_filename(opt->numbered_files ? NULL : commit, opt->nr,
				    opt->patch_suffix, &filename);
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

void show_log(struct rev_info *opt)
{
	struct strbuf msgbuf = STRBUF_INIT;
	struct log_info *log = opt->loginfo;
	struct commit *commit = log->commit, *parent = log->parent;
	int abbrev_commit = opt->abbrev_commit ? opt->abbrev : 40;
	const char *extra_headers = opt->extra_headers;
	struct pretty_print_context ctx = {0};

	opt->loginfo = NULL;
	ctx.show_notes = opt->show_notes;
	if (!opt->verbose_header) {
		graph_show_commit(opt->graph);

		if (!opt->graph) {
			if (commit->object.flags & BOUNDARY)
				putchar('-');
			else if (commit->object.flags & UNINTERESTING)
				putchar('^');
			else if (opt->left_right) {
				if (commit->object.flags & SYMMETRIC_LEFT)
					putchar('<');
				else
					putchar('>');
			}
		}
		fputs(find_unique_abbrev(commit->object.sha1, abbrev_commit), stdout);
		if (opt->print_parents)
			show_parents(commit, abbrev_commit);
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

		if (!opt->graph) {
			if (commit->object.flags & BOUNDARY)
				putchar('-');
			else if (commit->object.flags & UNINTERESTING)
				putchar('^');
			else if (opt->left_right) {
				if (commit->object.flags & SYMMETRIC_LEFT)
					putchar('<');
				else
					putchar('>');
			}
		}
		fputs(find_unique_abbrev(commit->object.sha1, abbrev_commit),
		      stdout);
		if (opt->print_parents)
			show_parents(commit, abbrev_commit);
		if (parent)
			printf(" (from %s)",
			       find_unique_abbrev(parent->object.sha1,
						  abbrev_commit));
		show_decorations(opt, commit);
		printf("%s", diff_get_color_opt(&opt->diffopt, DIFF_RESET));
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
				    opt->date_mode_explicit ?
					opt->date_mode :
					DATE_NORMAL);
			if (opt->commit_format == CMIT_FMT_ONELINE)
				return;
		}
	}

	if (!commit->buffer)
		return;

	/*
	 * And then the pretty-printed message itself
	 */
	if (ctx.need_8bit_cte >= 0)
		ctx.need_8bit_cte = has_non_ascii(opt->add_signoff);
	ctx.date_mode = opt->date_mode;
	ctx.abbrev = opt->diffopt.abbrev;
	ctx.after_subject = extra_headers;
	ctx.reflog_info = opt->reflog_info;
	pretty_print_commit(opt->commit_format, commit, &msgbuf, &ctx);

	if (opt->add_signoff)
		append_signoff(&msgbuf, opt->add_signoff);
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
	if (opt->use_terminator) {
		if (!opt->missing_newline)
			graph_show_padding(opt->graph);
		putchar('\n');
	}

	strbuf_release(&msgbuf);
}

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

	if (opt->loginfo && !opt->no_commit_id) {
		/* When showing a verbose header (i.e. log message),
		 * and not in --pretty=oneline format, we would want
		 * an extra newline between the end of log and the
		 * output for readability.
		 */
		show_log(opt);
		if ((opt->diffopt.output_format & ~DIFF_FORMAT_NO_OUTPUT) &&
		    opt->verbose_header &&
		    opt->commit_format != CMIT_FMT_ONELINE) {
			int pch = DIFF_FORMAT_DIFFSTAT | DIFF_FORMAT_PATCH;
			if ((pch & opt->diffopt.output_format) == pch)
				printf("---");
			if (opt->diffopt.output_prefix) {
				struct strbuf *msg = NULL;
				msg = opt->diffopt.output_prefix(&opt->diffopt,
					opt->diffopt.output_prefix_data);
				fwrite(msg->buf, msg->len, 1, stdout);
			}
			putchar('\n');
		}
	}
	diff_flush(&opt->diffopt);
	return 1;
}

static int do_diff_combined(struct rev_info *opt, struct commit *commit)
{
	unsigned const char *sha1 = commit->object.sha1;

	diff_tree_combined_merge(sha1, opt->dense_combined_merges, opt);
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
	unsigned const char *sha1 = commit->object.sha1;

	if (!opt->diff && !DIFF_OPT_TST(&opt->diffopt, EXIT_WITH_STATUS))
		return 0;

	/* Root commit? */
	parents = commit->parents;
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
			diff_tree_sha1(parents->item->object.sha1, sha1, "", &opt->diffopt);
			log_tree_diff_flush(opt);
			return !opt->loginfo;
		}

		/* If we show individual diffs, show the parent info */
		log->parent = parents->item;
	}

	showed_log = 0;
	for (;;) {
		struct commit *parent = parents->item;

		diff_tree_sha1(parent->object.sha1, sha1, "", &opt->diffopt);
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

	shown = log_tree_diff(opt, commit, &log);
	if (!shown && opt->loginfo && opt->always_show_header) {
		log.parent = NULL;
		show_log(opt);
		shown = 1;
	}
	opt->loginfo = NULL;
	maybe_flush_or_die(stdout, "stdout");
	return shown;
}
