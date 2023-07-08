#ifndef IDENT_H
#define IDENT_H

#include "string-list.h"

struct ident_split {
	const char *name_begin;
	const char *name_end;
	const char *mail_begin;
	const char *mail_end;
	const char *date_begin;
	const char *date_end;
	const char *tz_begin;
	const char *tz_end;
};

#define IDENT_STRICT	       1
#define IDENT_NO_DATE	       2
#define IDENT_NO_NAME	       4

enum want_ident {
	WANT_BLANK_IDENT,
	WANT_AUTHOR_IDENT,
	WANT_COMMITTER_IDENT
};

const char *ident_default_name(void);
const char *ident_default_email(void);
/*
 * Prepare an ident to fall back on if the user didn't configure it.
 */
void prepare_fallback_ident(const char *name, const char *email);
void reset_ident_date(void);
/*
 * Signals an success with 0, but time part of the result may be NULL
 * if the input lacks timestamp and zone
 */
int split_ident_line(struct ident_split *, const char *, int);

/*
 * Given a commit or tag object buffer and the commit or tag headers, replaces
 * the idents in the headers with their canonical versions using the mailmap mechanism.
 */
void apply_mailmap_to_header(struct strbuf *, const char **, struct string_list *);

/*
 * Compare split idents for equality or strict ordering. Note that we
 * compare only the ident part of the line, ignoring any timestamp.
 *
 * Because there are two fields, we must choose one as the primary key; we
 * currently arbitrarily pick the email.
 */
int ident_cmp(const struct ident_split *, const struct ident_split *);

const char *git_author_info(int);
const char *git_committer_info(int);
const char *fmt_ident(const char *name, const char *email,
		      enum want_ident whose_ident,
		      const char *date_str, int);
const char *fmt_name(enum want_ident);

int committer_ident_sufficiently_given(void);
int author_ident_sufficiently_given(void);

struct config_context;
int git_ident_config(const char *, const char *, const struct config_context *,
		     void *);

#endif
