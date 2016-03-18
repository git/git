#include "builtin.h"
#include "cache.h"
#include "commit.h"
#include "diff.h"
#include "string-list.h"
#include "revision.h"
#include "utf8.h"
#include "mailmap.h"
#include "shortlog.h"
#include "parse-options.h"

static char const * const shortlog_usage[] = {
	N_("git shortlog [<options>] [<revision-range>] [[--] [<path>...]]"),
	NULL
};

static int compare_by_number(const void *a1, const void *a2)
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
			      const char *author,
			      const char *oneline)
{
	const char *dot3 = log->common_repo_prefix;
	char *buffer, *p;
	struct string_list_item *item;
	const char *mailbuf, *namebuf;
	size_t namelen, maillen;
	const char *eol;
	struct strbuf subject = STRBUF_INIT;
	struct strbuf namemailbuf = STRBUF_INIT;
	struct ident_split ident;

	if (split_ident_line(&ident, author, strlen(author)))
		return;

	namebuf = ident.name_begin;
	mailbuf = ident.mail_begin;
	namelen = ident.name_end - ident.name_begin;
	maillen = ident.mail_end - ident.mail_begin;

	map_user(&log->mailmap, &mailbuf, &maillen, &namebuf, &namelen);
	strbuf_add(&namemailbuf, namebuf, namelen);

	if (log->email)
		strbuf_addf(&namemailbuf, " <%.*s>", (int)maillen, mailbuf);

	item = string_list_insert(&log->list, namemailbuf.buf);
	if (item->util == NULL)
		item->util = xcalloc(1, sizeof(struct string_list));

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

	if (dot3) {
		int dot3len = strlen(dot3);
		if (dot3len > 5) {
			while ((p = strstr(buffer, dot3)) != NULL) {
				int taillen = strlen(p) - dot3len;
				memcpy(p, "/.../", 5);
				memmove(p + 5, p + dot3len, taillen + 1);
			}
		}
	}

	string_list_append(item->util, buffer);
}

static void read_from_stdin(struct shortlog *log)
{
	char author[1024], oneline[1024];

	while (fgets(author, sizeof(author), stdin) != NULL) {
		if (!(author[0] == 'A' || author[0] == 'a') ||
		    !starts_with(author + 1, "uthor: "))
			continue;
		while (fgets(oneline, sizeof(oneline), stdin) &&
		       oneline[0] != '\n')
			; /* discard headers */
		while (fgets(oneline, sizeof(oneline), stdin) &&
		       oneline[0] == '\n')
			; /* discard blanks */
		insert_one_record(log, author + 8, oneline);
	}
}

void shortlog_add_commit(struct shortlog *log, struct commit *commit)
{
	const char *author = NULL, *buffer;
	struct strbuf buf = STRBUF_INIT;
	struct strbuf ufbuf = STRBUF_INIT;

	pp_commit_easy(CMIT_FMT_RAW, commit, &buf);
	buffer = buf.buf;
	while (*buffer && *buffer != '\n') {
		const char *eol = strchr(buffer, '\n');

		if (eol == NULL)
			eol = buffer + strlen(buffer);
		else
			eol++;

		if (starts_with(buffer, "author "))
			author = buffer + 7;
		buffer = eol;
	}
	if (!author) {
		warning(_("Missing author: %s"),
		    sha1_to_hex(commit->object.sha1));
		return;
	}
	if (log->user_format) {
		struct pretty_print_context ctx = {0};
		ctx.fmt = CMIT_FMT_USERFORMAT;
		ctx.abbrev = log->abbrev;
		ctx.subject = "";
		ctx.after_subject = "";
		ctx.date_mode.type = DATE_NORMAL;
		ctx.output_encoding = get_log_output_encoding();
		pretty_print_commit(&ctx, commit, &ufbuf);
		buffer = ufbuf.buf;
	} else if (*buffer) {
		buffer++;
	}
	insert_one_record(log, author, !*buffer ? "<none>" : buffer);
	strbuf_release(&ufbuf);
	strbuf_release(&buf);
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

void shortlog_init(struct shortlog *log)
{
	memset(log, 0, sizeof(*log));

	read_mailmap(&log->mailmap, &log->common_repo_prefix);

	log->list.strdup_strings = 1;
	log->wrap = DEFAULT_WRAPLEN;
	log->in1 = DEFAULT_INDENT1;
	log->in2 = DEFAULT_INDENT2;
}

int cmd_shortlog(int argc, const char **argv, const char *prefix)
{
	static struct shortlog log;
	static struct rev_info rev;
	int nongit = !startup_info->have_repository;

	static const struct option options[] = {
		OPT_BOOL('n', "numbered", &log.sort_by_number,
			 N_("sort output according to the number of commits per author")),
		OPT_BOOL('s', "summary", &log.summary,
			 N_("Suppress commit descriptions, only provides commit count")),
		OPT_BOOL('e', "email", &log.email,
			 N_("Show the email address of each author")),
		{ OPTION_CALLBACK, 'w', NULL, &log, N_("w[,i1[,i2]]"),
			N_("Linewrap output"), PARSE_OPT_OPTARG, &parse_wrap_args },
		OPT_END(),
	};

	struct parse_opt_ctx_t ctx;

	git_config(git_default_config, NULL);
	shortlog_init(&log);
	init_revisions(&rev, prefix);
	parse_options_start(&ctx, argc, argv, prefix, options,
			    PARSE_OPT_KEEP_DASHDASH | PARSE_OPT_KEEP_ARGV0);

	for (;;) {
		switch (parse_options_step(&ctx, options, shortlog_usage)) {
		case PARSE_OPT_HELP:
			exit(129);
		case PARSE_OPT_DONE:
			goto parse_done;
		}
		parse_revision_opt(&rev, &ctx, options, shortlog_usage);
	}
parse_done:
	argc = parse_options_end(&ctx);

	if (setup_revisions(argc, argv, &rev, NULL) != 1) {
		error(_("unrecognized argument: %s"), argv[1]);
		usage_with_options(shortlog_usage, options);
	}

	log.user_format = rev.commit_format == CMIT_FMT_USERFORMAT;
	log.abbrev = rev.abbrev;

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

	shortlog_output(&log);
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
	int i, j;
	struct strbuf sb = STRBUF_INIT;

	if (log->sort_by_number)
		qsort(log->list.items, log->list.nr, sizeof(struct string_list_item),
			compare_by_number);
	for (i = 0; i < log->list.nr; i++) {
		struct string_list *onelines = log->list.items[i].util;

		if (log->summary) {
			printf("%6d\t%s\n", onelines->nr, log->list.items[i].string);
		} else {
			printf("%s (%d):\n", log->list.items[i].string, onelines->nr);
			for (j = onelines->nr - 1; j >= 0; j--) {
				const char *msg = onelines->items[j].string;

				if (log->wrap_lines) {
					strbuf_reset(&sb);
					add_wrapped_shortlog_msg(&sb, msg, log);
					fwrite(sb.buf, sb.len, 1, stdout);
				}
				else
					printf("      %s\n", msg);
			}
			putchar('\n');
		}

		onelines->strdup_strings = 1;
		string_list_clear(onelines, 0);
		free(onelines);
		log->list.items[i].util = NULL;
	}

	strbuf_release(&sb);
	log->list.strdup_strings = 1;
	string_list_clear(&log->list, 1);
	clear_mailmap(&log->mailmap);
}
