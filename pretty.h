#ifndef PRETTY_H
#define PRETTY_H

#include "date.h"
#include "string-list.h"

struct commit;
struct repository;
struct strbuf;
struct process_trailer_options;

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

struct pretty_print_describe_status {
	unsigned int max_invocations;
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
	unsigned encode_email_headers:1;
	struct pretty_print_describe_status *describe_status;

	/*
	 * Fields below here are manipulated internally by pp_* functions and
	 * should not be counted on by callers.
	 */
	struct string_list in_body_headers;
	int graph_width;
};

/* Check whether commit format is mail. */
static inline int cmit_fmt_is_mail(enum cmit_fmt fmt)
{
	return (fmt == CMIT_FMT_EMAIL || fmt == CMIT_FMT_MBOXRD);
}

/*
 * Examine the user-specified format given by "fmt" (or if NULL, the global one
 * previously saved by get_commit_format()), and set flags based on which items
 * the format will need when it is expanded.
 */
struct userformat_want {
	unsigned notes:1;
	unsigned source:1;
	unsigned decorate:1;
};
void userformat_find_requirements(const char *fmt, struct userformat_want *w);

/*
 * Shortcut for invoking pretty_print_commit if we do not have any context.
 * Context would be set empty except "fmt".
 */
void pp_commit_easy(enum cmit_fmt fmt, const struct commit *commit,
			struct strbuf *sb);

/*
 * Get information about user and date from "line", format it and
 * put it into "sb".
 * Format of "line" must be readable for split_ident_line function.
 * The resulting format is "what: name <email> date".
 */
void pp_user_info(struct pretty_print_context *pp, const char *what,
			struct strbuf *sb, const char *line,
			const char *encoding);

/*
 * Format title line of commit message taken from "msg_p" and
 * put it into "sb".
 * First line of "msg_p" is also affected.
 */
void pp_title_line(struct pretty_print_context *pp, const char **msg_p,
			struct strbuf *sb, const char *encoding,
			int need_8bit_cte);

/*
 * Get current state of commit message from "msg_p" and continue formatting
 * by adding indentation and '>' signs. Put result into "sb".
 */
void pp_remainder(struct pretty_print_context *pp, const char **msg_p,
			struct strbuf *sb, int indent);

/*
 * Create a text message about commit using given "format" and "context".
 * Put the result to "sb".
 * Please use this function for custom formats.
 */
void repo_format_commit_message(struct repository *r,
			const struct commit *commit,
			const char *format, struct strbuf *sb,
			const struct pretty_print_context *context);

/*
 * Parse given arguments from "arg", check it for correctness and
 * fill struct rev_info.
 */
void get_commit_format(const char *arg, struct rev_info *);

/*
 * Make a commit message with all rules from given "pp"
 * and put it into "sb".
 * Please use this function if you have a context (candidate for "pp").
 */
void pretty_print_commit(struct pretty_print_context *pp,
			const struct commit *commit,
			struct strbuf *sb);

/*
 * Change line breaks in "msg" to "line_separator" and put it into "sb".
 * Return "msg" itself.
 */
const char *format_subject(struct strbuf *sb, const char *msg,
			const char *line_separator);

/* Check if "cmit_fmt" will produce an empty output. */
int commit_format_is_empty(enum cmit_fmt);

/* Make subject of commit message suitable for filename */
void format_sanitized_subject(struct strbuf *sb, const char *msg, size_t len);

int has_non_ascii(const char *text);

/*
 * Set values of fields in "struct process_trailer_options"
 * according to trailers arguments.
 */
int format_set_trailers_options(struct process_trailer_options *opts,
			struct string_list *filter_list,
			struct strbuf *sepbuf,
			struct strbuf *kvsepbuf,
			const char **arg,
			char **invalid_arg);

/*
 * Like show_date, but pull the timestamp and tz parameters from
 * the ident_split. It will also sanity-check the values and produce
 * a well-known sentinel date if they appear bogus.
 */
const char *show_ident_date(const struct ident_split *id,
			    const struct date_mode *mode);


#endif /* PRETTY_H */
