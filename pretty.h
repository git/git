#ifndef PRETTY_H
#define PRETTY_H

struct commit;

/* Commit formats */
enum cmit_fmt {
	CMIT_FMT_RAW,
	CMIT_FMT_MEDIUM,
	CMIT_FMT_DEFAULT = CMIT_FMT_MEDIUM,
	CMIT_FMT_SHORT,
	CMIT_FMT_FULL,
	CMIT_FMT_FULLER,
	CMIT_FMT_ONELINE,
	CMIT_FMT_EMAIL,
	CMIT_FMT_MBOXRD,
	CMIT_FMT_USERFORMAT,

	CMIT_FMT_UNSPECIFIED
};

struct pretty_print_context {
	/*
	 * Callers should tweak these to change the behavior of pp_* functions.
	 */
	enum cmit_fmt fmt;
	int abbrev;
	const char *after_subject;
	int preserve_subject;
	struct date_mode date_mode;
	unsigned date_mode_explicit:1;
	int print_email_subject;
	int expand_tabs_in_log;
	int need_8bit_cte;
	char *notes_message;
	struct reflog_walk_info *reflog_info;
	struct rev_info *rev;
	const char *output_encoding;
	struct string_list *mailmap;
	int color;
	struct ident_split *from_ident;

	/*
	 * Fields below here are manipulated internally by pp_* functions and
	 * should not be counted on by callers.
	 */
	struct string_list in_body_headers;
	int graph_width;
};

static inline int cmit_fmt_is_mail(enum cmit_fmt fmt)
{
	return (fmt == CMIT_FMT_EMAIL || fmt == CMIT_FMT_MBOXRD);
}

struct userformat_want {
	unsigned notes:1;
};

void userformat_find_requirements(const char *fmt, struct userformat_want *w);
void pp_commit_easy(enum cmit_fmt fmt, const struct commit *commit,
			struct strbuf *sb);
void pp_user_info(struct pretty_print_context *pp, const char *what,
			struct strbuf *sb, const char *line,
			const char *encoding);
void pp_title_line(struct pretty_print_context *pp, const char **msg_p,
			struct strbuf *sb, const char *encoding,
			int need_8bit_cte);
void pp_remainder(struct pretty_print_context *pp, const char **msg_p,
			struct strbuf *sb, int indent);

void format_commit_message(const struct commit *commit,
			const char *format, struct strbuf *sb,
			const struct pretty_print_context *context);

void get_commit_format(const char *arg, struct rev_info *);

void pretty_print_commit(struct pretty_print_context *pp,
			const struct commit *commit,
			struct strbuf *sb);

const char *format_subject(struct strbuf *sb, const char *msg,
			const char *line_separator);

int commit_format_is_empty(enum cmit_fmt);

#endif /* PRETTY_H */
