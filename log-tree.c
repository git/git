#include "cache.h"
#include "diff.h"
#include "commit.h"
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

static void show_decorations(struct commit *commit)
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

static int append_signoff(char *buf, int buf_sz, int at, const char *signoff)
{
	static const char signed_off_by[] = "Signed-off-by: ";
	int signoff_len = strlen(signoff);
	int has_signoff = 0;
	char *cp = buf;

	/* Do we have enough space to add it? */
	if (buf_sz - at <= strlen(signed_off_by) + signoff_len + 3)
		return at;

	/* First see if we already have the sign-off by the signer */
	while ((cp = strstr(cp, signed_off_by))) {

		has_signoff = 1;

		cp += strlen(signed_off_by);
		if (cp + signoff_len >= buf + at)
			break;
		if (strncmp(cp, signoff, signoff_len))
			continue;
		if (!isspace(cp[signoff_len]))
			continue;
		/* we already have him */
		return at;
	}

	if (!has_signoff)
		has_signoff = detect_any_signoff(buf, at);

	if (!has_signoff)
		buf[at++] = '\n';

	strcpy(buf + at, signed_off_by);
	at += strlen(signed_off_by);
	strcpy(buf + at, signoff);
	at += signoff_len;
	buf[at++] = '\n';
	buf[at] = 0;
	return at;
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

void show_log(struct rev_info *opt, const char *sep)
{
	static char this_header[16384];
	struct log_info *log = opt->loginfo;
	struct commit *commit = log->commit, *parent = log->parent;
	int abbrev = opt->diffopt.abbrev;
	int abbrev_commit = opt->abbrev_commit ? opt->abbrev : 40;
	const char *extra;
	int len;
	const char *subject = NULL, *extra_headers = opt->extra_headers;

	opt->loginfo = NULL;
	if (!opt->verbose_header) {
		if (opt->left_right) {
			if (commit->object.flags & BOUNDARY)
				putchar('-');
			else if (commit->object.flags & SYMMETRIC_LEFT)
				putchar('<');
			else
				putchar('>');
		}
		fputs(diff_unique_abbrev(commit->object.sha1, abbrev_commit), stdout);
		if (opt->parents)
			show_parents(commit, abbrev_commit);
		show_decorations(commit);
		putchar(opt->diffopt.line_termination);
		return;
	}

	/*
	 * The "oneline" format has several special cases:
	 *  - The pretty-printed commit lacks a newline at the end
	 *    of the buffer, but we do want to make sure that we
	 *    have a newline there. If the separator isn't already
	 *    a newline, add an extra one.
	 *  - unlike other log messages, the one-line format does
	 *    not have an empty line between entries.
	 */
	extra = "";
	if (*sep != '\n' && opt->commit_format == CMIT_FMT_ONELINE)
		extra = "\n";
	if (opt->shown_one && opt->commit_format != CMIT_FMT_ONELINE)
		putchar(opt->diffopt.line_termination);
	opt->shown_one = 1;

	/*
	 * Print header line of header..
	 */

	if (opt->commit_format == CMIT_FMT_EMAIL) {
		char *sha1 = sha1_to_hex(commit->object.sha1);
		if (opt->total > 0) {
			static char buffer[64];
			snprintf(buffer, sizeof(buffer),
					"Subject: [%s %0*d/%d] ",
					opt->subject_prefix,
					digits_in_number(opt->total),
					opt->nr, opt->total);
			subject = buffer;
		} else if (opt->total == 0) {
			static char buffer[256];
			snprintf(buffer, sizeof(buffer),
					"Subject: [%s] ",
					opt->subject_prefix);
			subject = buffer;
		} else {
			subject = "Subject: ";
		}

		printf("From %s Mon Sep 17 00:00:00 2001\n", sha1);
		if (opt->message_id)
			printf("Message-Id: <%s>\n", opt->message_id);
		if (opt->ref_message_id)
			printf("In-Reply-To: <%s>\nReferences: <%s>\n",
			       opt->ref_message_id, opt->ref_message_id);
		if (opt->mime_boundary) {
			static char subject_buffer[1024];
			static char buffer[1024];
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
				 "--%s%s\n"
				 "Content-Type: text/x-patch;"
				 " name=\"%s.diff\"\n"
				 "Content-Transfer-Encoding: 8bit\n"
				 "Content-Disposition: %s;"
				 " filename=\"%s.diff\"\n\n",
				 mime_boundary_leader, opt->mime_boundary,
				 sha1,
				 opt->no_inline ? "attachment" : "inline",
				 sha1);
			opt->diffopt.stat_sep = buffer;
		}
	} else if (opt->commit_format != CMIT_FMT_USERFORMAT) {
		fputs(diff_get_color(opt->diffopt.color_diff, DIFF_COMMIT),
		      stdout);
		if (opt->commit_format != CMIT_FMT_ONELINE)
			fputs("commit ", stdout);
		if (commit->object.flags & BOUNDARY)
			putchar('-');
		else if (opt->left_right) {
			if (commit->object.flags & SYMMETRIC_LEFT)
				putchar('<');
			else
				putchar('>');
		}
		fputs(diff_unique_abbrev(commit->object.sha1, abbrev_commit),
		      stdout);
		if (opt->parents)
			show_parents(commit, abbrev_commit);
		if (parent)
			printf(" (from %s)",
			       diff_unique_abbrev(parent->object.sha1,
						  abbrev_commit));
		show_decorations(commit);
		printf("%s",
		       diff_get_color(opt->diffopt.color_diff, DIFF_RESET));
		putchar(opt->commit_format == CMIT_FMT_ONELINE ? ' ' : '\n');
		if (opt->reflog_info) {
			show_reflog_message(opt->reflog_info,
				    opt->commit_format == CMIT_FMT_ONELINE,
				    opt->date_mode);
			if (opt->commit_format == CMIT_FMT_ONELINE) {
				printf("%s", sep);
				return;
			}
		}
	}

	/*
	 * And then the pretty-printed message itself
	 */
	len = pretty_print_commit(opt->commit_format, commit, ~0u, this_header,
				  sizeof(this_header), abbrev, subject,
				  extra_headers, opt->date_mode);

	if (opt->add_signoff)
		len = append_signoff(this_header, sizeof(this_header), len,
				     opt->add_signoff);
	printf("%s%s%s", this_header, extra, sep);
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
		show_log(opt, opt->diffopt.msg_sep);
		if (opt->verbose_header &&
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
		show_log(opt, "");
		shown = 1;
	}
	opt->loginfo = NULL;
	return shown;
}
