#include "cache.h"
#include "diff.h"
#include "commit.h"
#include "graph.h"
#include "log-tree.h"
#include "reflog-walk.h"

struct decoration name_decoration = { "object names" };

static void show_parents(struct commit *commit, int abbrev)
{
	struct commit_list *p;
	for (p = commit->parents; p ; p = p->next) {
		struct commit *parent = p->item;
		printf(" %s", diff_unique_abbrev(parent->object.sha1, abbrev));
	}
}

void show_decorations(struct commit *commit)
{
	const char *prefix;
	struct name_decoration *decoration;

	decoration = lookup_decoration(&name_decoration, &commit->object);
	if (!decoration)
		return;
	prefix = " (";
	while (decoration) {
		printf("%s%s", prefix, decoration->name);
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
	char ch, *cp;
	int seen_colon = 0;
	int seen_at = 0;
	int seen_name = 0;
	int seen_head = 0;

	cp = letter + size;
	while (letter <= --cp && (ch = *cp) == '\n')
		continue;

	while (letter <= cp) {
		ch = *cp--;
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

static int has_non_ascii(const char *s)
{
	int ch;
	if (!s)
		return 0;
	while ((ch = *s++) != '\0') {
		if (non_ascii(ch))
			return 1;
	}
	return 0;
}

void log_write_email_headers(struct rev_info *opt, const char *name,
			     const char **subject_p,
			     const char **extra_headers_p,
			     int *need_8bit_cte_p)
{
	const char *subject = NULL;
	const char *extra_headers = opt->extra_headers;

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
	if (opt->ref_message_id) {
		printf("In-Reply-To: <%s>\nReferences: <%s>\n",
		       opt->ref_message_id, opt->ref_message_id);
		graph_show_oneline(opt->graph);
	}
	if (opt->mime_boundary) {
		static char subject_buffer[1024];
		static char buffer[1024];
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

		snprintf(buffer, sizeof(buffer) - 1,
			 "\n--%s%s\n"
			 "Content-Type: text/x-patch;"
			 " name=\"%s.diff\"\n"
			 "Content-Transfer-Encoding: 8bit\n"
			 "Content-Disposition: %s;"
			 " filename=\"%s.diff\"\n\n",
			 mime_boundary_leader, opt->mime_boundary,
			 name,
			 opt->no_inline ? "attachment" : "inline",
			 name);
		opt->diffopt.stat_sep = buffer;
	}
	*subject_p = subject;
	*extra_headers_p = extra_headers;
}

void show_log(struct rev_info *opt)
{
	struct strbuf msgbuf;
	struct log_info *log = opt->loginfo;
	struct commit *commit = log->commit, *parent = log->parent;
	int abbrev = opt->diffopt.abbrev;
	int abbrev_commit = opt->abbrev_commit ? opt->abbrev : 40;
	const char *subject = NULL, *extra_headers = opt->extra_headers;
	int need_8bit_cte = 0;

	opt->loginfo = NULL;
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
		fputs(diff_unique_abbrev(commit->object.sha1, abbrev_commit), stdout);
		if (opt->print_parents)
			show_parents(commit, abbrev_commit);
		show_decorations(commit);
		if (opt->graph && !graph_is_commit_finished(opt->graph)) {
			putchar('\n');
			graph_show_remainder(opt->graph);
		}
		putchar(opt->diffopt.line_termination);
		return;
	}

	/*
	 * If use_terminator is set, add a newline at the end of the entry.
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
		log_write_email_headers(opt, sha1_to_hex(commit->object.sha1),
					&subject, &extra_headers,
					&need_8bit_cte);
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
		fputs(diff_unique_abbrev(commit->object.sha1, abbrev_commit),
		      stdout);
		if (opt->print_parents)
			show_parents(commit, abbrev_commit);
		if (parent)
			printf(" (from %s)",
			       diff_unique_abbrev(parent->object.sha1,
						  abbrev_commit));
		show_decorations(commit);
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
				    opt->date_mode);
			if (opt->commit_format == CMIT_FMT_ONELINE)
				return;
		}
	}

	if (!commit->buffer)
		return;

	/*
	 * And then the pretty-printed message itself
	 */
	strbuf_init(&msgbuf, 0);
	if (need_8bit_cte >= 0)
		need_8bit_cte = has_non_ascii(opt->add_signoff);
	pretty_print_commit(opt->commit_format, commit, &msgbuf,
			    abbrev, subject, extra_headers, opt->date_mode,
			    need_8bit_cte);

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

	if (!opt->diff)
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
