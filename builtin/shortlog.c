#include "builtin.h"
#include "cache.h"
#include "config.h"
#include "commit.h"
#include "diff.h"
#include "string-list.h"
#include "revision.h"
#include "utf8.h"
#include "mailmap.h"
#include "shortlog.h"
#include "parse-options.h"
#include "trailer.h"
#include "strmap.h"

static char const * const shortlog_usage[] = {
	N_("git shortlog [<options>] [<revision-range>] [[--] <path>...]"),
	N_("git log --pretty=short | git shortlog [<options>]"),
	NULL
};

/*
 * The util field of our string_list_items will contain one of two things:
 *
 *   - if --summary is not in use, it will point to a string list of the
 *     oneline subjects assigned to this author
 *
 *   - if --summary is in use, we don't need that list; we only need to know
 *     its size. So we abuse the pointer slot to store our integer counter.
 *
 *  This macro accesses the latter.
 */
#define UTIL_TO_INT(x) ((intptr_t)(x)->util)

static int compare_by_counter(const void *a1, const void *a2)
{
	const struct string_list_item *i1 = a1, *i2 = a2;
	return UTIL_TO_INT(i2) - UTIL_TO_INT(i1);
}

static int compare_by_list(const void *a1, const void *a2)
{
	const struct string_list_item *i1 = a1, *i2 = a2;
	const struct string_list *l1 = i1->util, *l2 = i2->util;

	if (l1->nr < l2->nr)
		return 1;
	else if (l1->nr == l2->nr)
		return 0;
	else
		return -1;
}

static void insert_one_record(struct shortlog *log,
			      const char *ident,
			      const char *oneline)
{
	struct string_list_item *item;

	item = string_list_insert(&log->list, ident);

	if (log->summary)
		item->util = (void *)(UTIL_TO_INT(item) + 1);
	else {
		char *buffer;
		struct strbuf subject = STRBUF_INIT;
		const char *eol;

		/* Skip any leading whitespace, including any blank lines. */
		while (*oneline && isspace(*oneline))
			oneline++;
		eol = strchr(oneline, '\n');
		if (!eol)
			eol = oneline + strlen(oneline);
		if (starts_with(oneline, "[PATCH")) {
			char *eob = strchr(oneline, ']');
			if (eob && (!eol || eob < eol))
				oneline = eob + 1;
		}
		while (*oneline && isspace(*oneline) && *oneline != '\n')
			oneline++;
		format_subject(&subject, oneline, " ");
		buffer = strbuf_detach(&subject, NULL);

		if (!item->util) {
			item->util = xmalloc(sizeof(struct string_list));
			string_list_init_nodup(item->util);
		}
		string_list_append(item->util, buffer);
	}
}

static int parse_ident(struct shortlog *log,
		       struct strbuf *out, const char *in)
{
	const char *mailbuf, *namebuf;
	size_t namelen, maillen;
	struct ident_split ident;

	if (split_ident_line(&ident, in, strlen(in)))
		return -1;

	namebuf = ident.name_begin;
	mailbuf = ident.mail_begin;
	namelen = ident.name_end - ident.name_begin;
	maillen = ident.mail_end - ident.mail_begin;

	map_user(&log->mailmap, &mailbuf, &maillen, &namebuf, &namelen);
	strbuf_add(out, namebuf, namelen);
	if (log->email)
		strbuf_addf(out, " <%.*s>", (int)maillen, mailbuf);

	return 0;
}

static void read_from_stdin(struct shortlog *log)
{
	struct strbuf ident = STRBUF_INIT;
	struct strbuf mapped_ident = STRBUF_INIT;
	struct strbuf oneline = STRBUF_INIT;
	static const char *author_match[2] = { "Author: ", "author " };
	static const char *committer_match[2] = { "Commit: ", "committer " };
	const char **match;

	if (HAS_MULTI_BITS(log->groups))
		die(_("using multiple --group options with stdin is not supported"));

	switch (log->groups) {
	case SHORTLOG_GROUP_AUTHOR:
		match = author_match;
		break;
	case SHORTLOG_GROUP_COMMITTER:
		match = committer_match;
		break;
	case SHORTLOG_GROUP_TRAILER:
		die(_("using %s with stdin is not supported"), "--group=trailer");
	case SHORTLOG_GROUP_FORMAT:
		die(_("using %s with stdin is not supported"), "--group=format");
	default:
		BUG("unhandled shortlog group");
	}

	while (strbuf_getline_lf(&ident, stdin) != EOF) {
		const char *v;
		if (!skip_prefix(ident.buf, match[0], &v) &&
		    !skip_prefix(ident.buf, match[1], &v))
			continue;
		while (strbuf_getline_lf(&oneline, stdin) != EOF &&
		       oneline.len)
			; /* discard headers */
		while (strbuf_getline_lf(&oneline, stdin) != EOF &&
		       !oneline.len)
			; /* discard blanks */

		strbuf_reset(&mapped_ident);
		if (parse_ident(log, &mapped_ident, v) < 0)
			continue;

		insert_one_record(log, mapped_ident.buf, oneline.buf);
	}
	strbuf_release(&ident);
	strbuf_release(&mapped_ident);
	strbuf_release(&oneline);
}

static void insert_records_from_trailers(struct shortlog *log,
					 struct strset *dups,
					 struct commit *commit,
					 struct pretty_print_context *ctx,
					 const char *oneline)
{
	struct trailer_iterator iter;
	const char *commit_buffer, *body;
	struct strbuf ident = STRBUF_INIT;

	if (!log->trailers.nr)
		return;

	/*
	 * Using repo_format_commit_message("%B") would be simpler here, but
	 * this saves us copying the message.
	 */
	commit_buffer = repo_logmsg_reencode(the_repository, commit, NULL,
					     ctx->output_encoding);
	body = strstr(commit_buffer, "\n\n");
	if (!body)
		return;

	trailer_iterator_init(&iter, body);
	while (trailer_iterator_advance(&iter)) {
		const char *value = iter.val.buf;

		if (!string_list_has_string(&log->trailers, iter.key.buf))
			continue;

		strbuf_reset(&ident);
		if (!parse_ident(log, &ident, value))
			value = ident.buf;

		if (!strset_add(dups, value))
			continue;
		insert_one_record(log, value, oneline);
	}
	trailer_iterator_release(&iter);

	strbuf_release(&ident);
	repo_unuse_commit_buffer(the_repository, commit, commit_buffer);
}

static int shortlog_needs_dedup(const struct shortlog *log)
{
	return HAS_MULTI_BITS(log->groups) || log->format.nr > 1 || log->trailers.nr;
}

static void insert_records_from_format(struct shortlog *log,
				       struct strset *dups,
				       struct commit *commit,
				       struct pretty_print_context *ctx,
				       const char *oneline)
{
	struct strbuf buf = STRBUF_INIT;
	struct string_list_item *item;

	for_each_string_list_item(item, &log->format) {
		strbuf_reset(&buf);

		repo_format_commit_message(the_repository, commit,
					   item->string, &buf, ctx);

		if (!shortlog_needs_dedup(log) || strset_add(dups, buf.buf))
			insert_one_record(log, buf.buf, oneline);
	}

	strbuf_release(&buf);
}

void shortlog_add_commit(struct shortlog *log, struct commit *commit)
{
	struct strbuf oneline = STRBUF_INIT;
	struct strset dups = STRSET_INIT;
	struct pretty_print_context ctx = {0};
	const char *oneline_str;

	ctx.fmt = CMIT_FMT_USERFORMAT;
	ctx.abbrev = log->abbrev;
	ctx.print_email_subject = 1;
	ctx.date_mode = log->date_mode;
	ctx.output_encoding = get_log_output_encoding();

	if (!log->summary) {
		if (log->user_format)
			pretty_print_commit(&ctx, commit, &oneline);
		else
			repo_format_commit_message(the_repository, commit,
						   "%s", &oneline, &ctx);
	}
	oneline_str = oneline.len ? oneline.buf : "<none>";

	insert_records_from_trailers(log, &dups, commit, &ctx, oneline_str);
	insert_records_from_format(log, &dups, commit, &ctx, oneline_str);

	strset_clear(&dups);
	strbuf_release(&oneline);
}

static void get_from_rev(struct rev_info *rev, struct shortlog *log)
{
	struct commit *commit;

	if (prepare_revision_walk(rev))
		die(_("revision walk setup failed"));
	while ((commit = get_revision(rev)) != NULL)
		shortlog_add_commit(log, commit);
}

static int parse_uint(char const **arg, int comma, int defval)
{
	unsigned long ul;
	int ret;
	char *endp;

	ul = strtoul(*arg, &endp, 10);
	if (*endp && *endp != comma)
		return -1;
	if (ul > INT_MAX)
		return -1;
	ret = *arg == endp ? defval : (int)ul;
	*arg = *endp ? endp + 1 : endp;
	return ret;
}

static const char wrap_arg_usage[] = "-w[<width>[,<indent1>[,<indent2>]]]";
#define DEFAULT_WRAPLEN 76
#define DEFAULT_INDENT1 6
#define DEFAULT_INDENT2 9

static int parse_wrap_args(const struct option *opt, const char *arg, int unset)
{
	struct shortlog *log = opt->value;

	log->wrap_lines = !unset;
	if (unset)
		return 0;
	if (!arg) {
		log->wrap = DEFAULT_WRAPLEN;
		log->in1 = DEFAULT_INDENT1;
		log->in2 = DEFAULT_INDENT2;
		return 0;
	}

	log->wrap = parse_uint(&arg, ',', DEFAULT_WRAPLEN);
	log->in1 = parse_uint(&arg, ',', DEFAULT_INDENT1);
	log->in2 = parse_uint(&arg, '\0', DEFAULT_INDENT2);
	if (log->wrap < 0 || log->in1 < 0 || log->in2 < 0)
		return error(wrap_arg_usage);
	if (log->wrap &&
	    ((log->in1 && log->wrap <= log->in1) ||
	     (log->in2 && log->wrap <= log->in2)))
		return error(wrap_arg_usage);
	return 0;
}

static int parse_group_option(const struct option *opt, const char *arg, int unset)
{
	struct shortlog *log = opt->value;
	const char *field;

	if (unset) {
		log->groups = 0;
		string_list_clear(&log->trailers, 0);
		string_list_clear(&log->format, 0);
	} else if (!strcasecmp(arg, "author"))
		log->groups |= SHORTLOG_GROUP_AUTHOR;
	else if (!strcasecmp(arg, "committer"))
		log->groups |= SHORTLOG_GROUP_COMMITTER;
	else if (skip_prefix(arg, "trailer:", &field)) {
		log->groups |= SHORTLOG_GROUP_TRAILER;
		string_list_append(&log->trailers, field);
	} else if (skip_prefix(arg, "format:", &field)) {
		log->groups |= SHORTLOG_GROUP_FORMAT;
		string_list_append(&log->format, field);
	} else if (strchr(arg, '%')) {
		log->groups |= SHORTLOG_GROUP_FORMAT;
		string_list_append(&log->format, arg);
	} else {
		return error(_("unknown group type: %s"), arg);
	}

	return 0;
}


void shortlog_init(struct shortlog *log)
{
	memset(log, 0, sizeof(*log));

	read_mailmap(&log->mailmap);

	log->list.strdup_strings = 1;
	log->wrap = DEFAULT_WRAPLEN;
	log->in1 = DEFAULT_INDENT1;
	log->in2 = DEFAULT_INDENT2;
	log->trailers.strdup_strings = 1;
	log->trailers.cmp = strcasecmp;
	log->format.strdup_strings = 1;
}

void shortlog_finish_setup(struct shortlog *log)
{
	if (log->groups & SHORTLOG_GROUP_AUTHOR)
		string_list_append(&log->format,
				   log->email ? "%aN <%aE>" : "%aN");
	if (log->groups & SHORTLOG_GROUP_COMMITTER)
		string_list_append(&log->format,
				   log->email ? "%cN <%cE>" : "%cN");

	string_list_sort(&log->trailers);
}

int cmd_shortlog(int argc, const char **argv, const char *prefix)
{
	struct shortlog log = { STRING_LIST_INIT_NODUP };
	struct rev_info rev;
	int nongit = !startup_info->have_repository;

	const struct option options[] = {
		OPT_BIT('c', "committer", &log.groups,
			N_("group by committer rather than author"),
			SHORTLOG_GROUP_COMMITTER),
		OPT_BOOL('n', "numbered", &log.sort_by_number,
			 N_("sort output according to the number of commits per author")),
		OPT_BOOL('s', "summary", &log.summary,
			 N_("suppress commit descriptions, only provides commit count")),
		OPT_BOOL('e', "email", &log.email,
			 N_("show the email address of each author")),
		OPT_CALLBACK_F('w', NULL, &log, N_("<w>[,<i1>[,<i2>]]"),
			N_("linewrap output"), PARSE_OPT_OPTARG,
			&parse_wrap_args),
		OPT_CALLBACK(0, "group", &log, N_("field"),
			N_("group by field"), parse_group_option),
		OPT_END(),
	};

	struct parse_opt_ctx_t ctx;

	git_config(git_default_config, NULL);
	shortlog_init(&log);
	repo_init_revisions(the_repository, &rev, prefix);
	parse_options_start(&ctx, argc, argv, prefix, options,
			    PARSE_OPT_KEEP_DASHDASH | PARSE_OPT_KEEP_ARGV0);

	for (;;) {
		switch (parse_options_step(&ctx, options, shortlog_usage)) {
		case PARSE_OPT_NON_OPTION:
		case PARSE_OPT_UNKNOWN:
			break;
		case PARSE_OPT_HELP:
		case PARSE_OPT_ERROR:
		case PARSE_OPT_SUBCOMMAND:
			exit(129);
		case PARSE_OPT_COMPLETE:
			exit(0);
		case PARSE_OPT_DONE:
			goto parse_done;
		}
		parse_revision_opt(&rev, &ctx, options, shortlog_usage);
	}
parse_done:
	revision_opts_finish(&rev);
	argc = parse_options_end(&ctx);

	if (nongit && argc > 1) {
		error(_("too many arguments given outside repository"));
		usage_with_options(shortlog_usage, options);
	}

	if (setup_revisions(argc, argv, &rev, NULL) != 1) {
		error(_("unrecognized argument: %s"), argv[1]);
		usage_with_options(shortlog_usage, options);
	}

	log.user_format = rev.commit_format == CMIT_FMT_USERFORMAT;
	log.abbrev = rev.abbrev;
	log.file = rev.diffopt.file;
	log.date_mode = rev.date_mode;

	if (!log.groups)
		log.groups = SHORTLOG_GROUP_AUTHOR;
	shortlog_finish_setup(&log);

	/* assume HEAD if from a tty */
	if (!nongit && !rev.pending.nr && isatty(0))
		add_head_to_pending(&rev);
	if (rev.pending.nr == 0) {
		if (isatty(0))
			fprintf(stderr, _("(reading log message from standard input)\n"));
		read_from_stdin(&log);
	}
	else
		get_from_rev(&rev, &log);

	release_revisions(&rev);

	shortlog_output(&log);
	if (log.file != stdout)
		fclose(log.file);
	return 0;
}

static void add_wrapped_shortlog_msg(struct strbuf *sb, const char *s,
				     const struct shortlog *log)
{
	strbuf_add_wrapped_text(sb, s, log->in1, log->in2, log->wrap);
	strbuf_addch(sb, '\n');
}

void shortlog_output(struct shortlog *log)
{
	size_t i, j;
	struct strbuf sb = STRBUF_INIT;

	if (log->sort_by_number)
		STABLE_QSORT(log->list.items, log->list.nr,
		      log->summary ? compare_by_counter : compare_by_list);
	for (i = 0; i < log->list.nr; i++) {
		const struct string_list_item *item = &log->list.items[i];
		if (log->summary) {
			fprintf(log->file, "%6d\t%s\n",
				(int)UTIL_TO_INT(item), item->string);
		} else {
			struct string_list *onelines = item->util;
			fprintf(log->file, "%s (%"PRIuMAX"):\n",
				item->string, (uintmax_t)onelines->nr);
			for (j = onelines->nr; j >= 1; j--) {
				const char *msg = onelines->items[j - 1].string;

				if (log->wrap_lines) {
					strbuf_reset(&sb);
					add_wrapped_shortlog_msg(&sb, msg, log);
					fwrite(sb.buf, sb.len, 1, log->file);
				}
				else
					fprintf(log->file, "      %s\n", msg);
			}
			putc('\n', log->file);
			onelines->strdup_strings = 1;
			string_list_clear(onelines, 0);
			free(onelines);
		}

		log->list.items[i].util = NULL;
	}

	strbuf_release(&sb);
	log->list.strdup_strings = 1;
	string_list_clear(&log->list, 1);
	clear_mailmap(&log->mailmap);
	string_list_clear(&log->format, 0);
}
