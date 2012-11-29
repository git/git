/*
 * ident.c
 *
 * create git identifier lines of the form "name <email> date"
 *
 * Copyright (C) 2005 Linus Torvalds
 */
#include "cache.h"

static struct strbuf git_default_name = STRBUF_INIT;
static struct strbuf git_default_email = STRBUF_INIT;
static char git_default_date[50];

#define IDENT_NAME_GIVEN 01
#define IDENT_MAIL_GIVEN 02
#define IDENT_ALL_GIVEN (IDENT_NAME_GIVEN|IDENT_MAIL_GIVEN)
static int committer_ident_explicitly_given;
static int author_ident_explicitly_given;

#ifdef NO_GECOS_IN_PWENT
#define get_gecos(ignored) "&"
#else
#define get_gecos(struct_passwd) ((struct_passwd)->pw_gecos)
#endif

static void copy_gecos(const struct passwd *w, struct strbuf *name)
{
	char *src;

	/* Traditionally GECOS field had office phone numbers etc, separated
	 * with commas.  Also & stands for capitalized form of the login name.
	 */

	for (src = get_gecos(w); *src && *src != ','; src++) {
		int ch = *src;
		if (ch != '&')
			strbuf_addch(name, ch);
		else {
			/* Sorry, Mr. McDonald... */
			strbuf_addch(name, toupper(*w->pw_name));
			strbuf_addstr(name, w->pw_name + 1);
		}
	}
}

static int add_mailname_host(struct strbuf *buf)
{
	FILE *mailname;

	mailname = fopen("/etc/mailname", "r");
	if (!mailname) {
		if (errno != ENOENT)
			warning("cannot open /etc/mailname: %s",
				strerror(errno));
		return -1;
	}
	if (strbuf_getline(buf, mailname, '\n') == EOF) {
		if (ferror(mailname))
			warning("cannot read /etc/mailname: %s",
				strerror(errno));
		fclose(mailname);
		return -1;
	}
	/* success! */
	fclose(mailname);
	return 0;
}

static void add_domainname(struct strbuf *out)
{
	char buf[1024];
	struct hostent *he;

	if (gethostname(buf, sizeof(buf))) {
		warning("cannot get host name: %s", strerror(errno));
		strbuf_addstr(out, "(none)");
		return;
	}
	if (strchr(buf, '.'))
		strbuf_addstr(out, buf);
	else if ((he = gethostbyname(buf)) && strchr(he->h_name, '.'))
		strbuf_addstr(out, he->h_name);
	else
		strbuf_addf(out, "%s.(none)", buf);
}

static void copy_email(const struct passwd *pw, struct strbuf *email)
{
	/*
	 * Make up a fake email address
	 * (name + '@' + hostname [+ '.' + domainname])
	 */
	strbuf_addstr(email, pw->pw_name);
	strbuf_addch(email, '@');

	if (!add_mailname_host(email))
		return;	/* read from "/etc/mailname" (Debian) */
	add_domainname(email);
}

static const char *ident_default_name(void)
{
	if (!git_default_name.len) {
		copy_gecos(xgetpwuid_self(), &git_default_name);
		strbuf_trim(&git_default_name);
	}
	return git_default_name.buf;
}

const char *ident_default_email(void)
{
	if (!git_default_email.len) {
		const char *email = getenv("EMAIL");

		if (email && email[0]) {
			strbuf_addstr(&git_default_email, email);
			committer_ident_explicitly_given |= IDENT_MAIL_GIVEN;
			author_ident_explicitly_given |= IDENT_MAIL_GIVEN;
		} else
			copy_email(xgetpwuid_self(), &git_default_email);
		strbuf_trim(&git_default_email);
	}
	return git_default_email.buf;
}

static const char *ident_default_date(void)
{
	if (!git_default_date[0])
		datestamp(git_default_date, sizeof(git_default_date));
	return git_default_date;
}

static int crud(unsigned char c)
{
	return  c <= 32  ||
		c == '.' ||
		c == ',' ||
		c == ':' ||
		c == ';' ||
		c == '<' ||
		c == '>' ||
		c == '"' ||
		c == '\\' ||
		c == '\'';
}

/*
 * Copy over a string to the destination, but avoid special
 * characters ('\n', '<' and '>') and remove crud at the end
 */
static void strbuf_addstr_without_crud(struct strbuf *sb, const char *src)
{
	size_t i, len;
	unsigned char c;

	/* Remove crud from the beginning.. */
	while ((c = *src) != 0) {
		if (!crud(c))
			break;
		src++;
	}

	/* Remove crud from the end.. */
	len = strlen(src);
	while (len > 0) {
		c = src[len-1];
		if (!crud(c))
			break;
		--len;
	}

	/*
	 * Copy the rest to the buffer, but avoid the special
	 * characters '\n' '<' and '>' that act as delimiters on
	 * an identification line. We can only remove crud, never add it,
	 * so 'len' is our maximum.
	 */
	strbuf_grow(sb, len);
	for (i = 0; i < len; i++) {
		c = *src++;
		switch (c) {
		case '\n': case '<': case '>':
			continue;
		}
		sb->buf[sb->len++] = c;
	}
	sb->buf[sb->len] = '\0';
}

/*
 * Reverse of fmt_ident(); given an ident line, split the fields
 * to allow the caller to parse it.
 * Signal a success by returning 0, but date/tz fields of the result
 * can still be NULL if the input line only has the name/email part
 * (e.g. reading from a reflog entry).
 */
int split_ident_line(struct ident_split *split, const char *line, int len)
{
	const char *cp;
	size_t span;
	int status = -1;

	memset(split, 0, sizeof(*split));

	split->name_begin = line;
	for (cp = line; *cp && cp < line + len; cp++)
		if (*cp == '<') {
			split->mail_begin = cp + 1;
			break;
		}
	if (!split->mail_begin)
		return status;

	for (cp = split->mail_begin - 2; line <= cp; cp--)
		if (!isspace(*cp)) {
			split->name_end = cp + 1;
			break;
		}
	if (!split->name_end) {
		/* no human readable name */
		split->name_end = split->name_begin;
	}

	for (cp = split->mail_begin; cp < line + len; cp++)
		if (*cp == '>') {
			split->mail_end = cp;
			break;
		}
	if (!split->mail_end)
		return status;

	for (cp = split->mail_end + 1; cp < line + len && isspace(*cp); cp++)
		;
	if (line + len <= cp)
		goto person_only;
	split->date_begin = cp;
	span = strspn(cp, "0123456789");
	if (!span)
		goto person_only;
	split->date_end = split->date_begin + span;
	for (cp = split->date_end; cp < line + len && isspace(*cp); cp++)
		;
	if (line + len <= cp || (*cp != '+' && *cp != '-'))
		goto person_only;
	split->tz_begin = cp;
	span = strspn(cp + 1, "0123456789");
	if (!span)
		goto person_only;
	split->tz_end = split->tz_begin + 1 + span;
	return 0;

person_only:
	split->date_begin = NULL;
	split->date_end = NULL;
	split->tz_begin = NULL;
	split->tz_end = NULL;
	return 0;
}

static const char *env_hint =
"\n"
"*** Please tell me who you are.\n"
"\n"
"Run\n"
"\n"
"  git config --global user.email \"you@example.com\"\n"
"  git config --global user.name \"Your Name\"\n"
"\n"
"to set your account\'s default identity.\n"
"Omit --global to set the identity only in this repository.\n"
"\n";

const char *fmt_ident(const char *name, const char *email,
		      const char *date_str, int flag)
{
	static struct strbuf ident = STRBUF_INIT;
	char date[50];
	int strict = (flag & IDENT_STRICT);
	int want_date = !(flag & IDENT_NO_DATE);
	int want_name = !(flag & IDENT_NO_NAME);

	if (want_name && !name)
		name = ident_default_name();
	if (!email)
		email = ident_default_email();

	if (want_name && !*name) {
		struct passwd *pw;

		if (strict) {
			if (name == git_default_name.buf)
				fputs(env_hint, stderr);
			die("empty ident name (for <%s>) not allowed", email);
		}
		pw = xgetpwuid_self();
		name = pw->pw_name;
	}

	if (strict && email == git_default_email.buf &&
	    strstr(email, "(none)")) {
		fputs(env_hint, stderr);
		die("unable to auto-detect email address (got '%s')", email);
	}

	if (want_date) {
		if (date_str && date_str[0]) {
			if (parse_date(date_str, date, sizeof(date)) < 0)
				die("invalid date format: %s", date_str);
		}
		else
			strcpy(date, ident_default_date());
	}

	strbuf_reset(&ident);
	if (want_name) {
		strbuf_addstr_without_crud(&ident, name);
		strbuf_addstr(&ident, " <");
	}
	strbuf_addstr_without_crud(&ident, email);
	if (want_name)
			strbuf_addch(&ident, '>');
	if (want_date) {
		strbuf_addch(&ident, ' ');
		strbuf_addstr_without_crud(&ident, date);
	}
	return ident.buf;
}

const char *fmt_name(const char *name, const char *email)
{
	return fmt_ident(name, email, NULL, IDENT_STRICT | IDENT_NO_DATE);
}

const char *git_author_info(int flag)
{
	if (getenv("GIT_AUTHOR_NAME"))
		author_ident_explicitly_given |= IDENT_NAME_GIVEN;
	if (getenv("GIT_AUTHOR_EMAIL"))
		author_ident_explicitly_given |= IDENT_MAIL_GIVEN;
	return fmt_ident(getenv("GIT_AUTHOR_NAME"),
			 getenv("GIT_AUTHOR_EMAIL"),
			 getenv("GIT_AUTHOR_DATE"),
			 flag);
}

const char *git_committer_info(int flag)
{
	if (getenv("GIT_COMMITTER_NAME"))
		committer_ident_explicitly_given |= IDENT_NAME_GIVEN;
	if (getenv("GIT_COMMITTER_EMAIL"))
		committer_ident_explicitly_given |= IDENT_MAIL_GIVEN;
	return fmt_ident(getenv("GIT_COMMITTER_NAME"),
			 getenv("GIT_COMMITTER_EMAIL"),
			 getenv("GIT_COMMITTER_DATE"),
			 flag);
}

static int ident_is_sufficient(int user_ident_explicitly_given)
{
#ifndef WINDOWS
	return (user_ident_explicitly_given & IDENT_MAIL_GIVEN);
#else
	return (user_ident_explicitly_given == IDENT_ALL_GIVEN);
#endif
}

int committer_ident_sufficiently_given(void)
{
	return ident_is_sufficient(committer_ident_explicitly_given);
}

int author_ident_sufficiently_given(void)
{
	return ident_is_sufficient(author_ident_explicitly_given);
}

int git_ident_config(const char *var, const char *value, void *data)
{
	if (!strcmp(var, "user.name")) {
		if (!value)
			return config_error_nonbool(var);
		strbuf_reset(&git_default_name);
		strbuf_addstr(&git_default_name, value);
		committer_ident_explicitly_given |= IDENT_NAME_GIVEN;
		author_ident_explicitly_given |= IDENT_NAME_GIVEN;
		return 0;
	}

	if (!strcmp(var, "user.email")) {
		if (!value)
			return config_error_nonbool(var);
		strbuf_reset(&git_default_email);
		strbuf_addstr(&git_default_email, value);
		committer_ident_explicitly_given |= IDENT_MAIL_GIVEN;
		author_ident_explicitly_given |= IDENT_MAIL_GIVEN;
		return 0;
	}

	return 0;
}
