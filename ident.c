/*
 * ident.c
 *
 * create git identifier lines of the form "name <email> date"
 *
 * Copyright (C) 2005 Linus Torvalds
 */
#include "git-compat-util.h"
#include "ident.h"
#include "config.h"
#include "date.h"
#include "gettext.h"
#include "mailmap.h"
#include "strbuf.h"

static struct strbuf git_default_name = STRBUF_INIT;
static struct strbuf git_default_email = STRBUF_INIT;
static struct strbuf git_default_date = STRBUF_INIT;
static struct strbuf git_author_name = STRBUF_INIT;
static struct strbuf git_author_email = STRBUF_INIT;
static struct strbuf git_committer_name = STRBUF_INIT;
static struct strbuf git_committer_email = STRBUF_INIT;
static int default_email_is_bogus;
static int default_name_is_bogus;

static int ident_use_config_only;

#define IDENT_NAME_GIVEN 01
#define IDENT_MAIL_GIVEN 02
#define IDENT_ALL_GIVEN (IDENT_NAME_GIVEN|IDENT_MAIL_GIVEN)
static int committer_ident_explicitly_given;
static int author_ident_explicitly_given;
static int ident_config_given;

#ifdef NO_GECOS_IN_PWENT
#define get_gecos(ignored) "&"
#else
#define get_gecos(struct_passwd) ((struct_passwd)->pw_gecos)
#endif

static struct passwd *xgetpwuid_self(int *is_bogus)
{
	struct passwd *pw;

	errno = 0;
	pw = getpwuid(getuid());
	if (!pw) {
		static struct passwd fallback;
		fallback.pw_name = (char *) "unknown";
#ifndef NO_GECOS_IN_PWENT
		fallback.pw_gecos = (char *) "Unknown";
#endif
		pw = &fallback;
		if (is_bogus)
			*is_bogus = 1;
	}
	return pw;
}

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
	struct strbuf mailnamebuf = STRBUF_INIT;

	mailname = fopen_or_warn("/etc/mailname", "r");
	if (!mailname)
		return -1;

	if (strbuf_getline(&mailnamebuf, mailname) == EOF) {
		if (ferror(mailname))
			warning_errno("cannot read /etc/mailname");
		strbuf_release(&mailnamebuf);
		fclose(mailname);
		return -1;
	}
	/* success! */
	strbuf_addbuf(buf, &mailnamebuf);
	strbuf_release(&mailnamebuf);
	fclose(mailname);
	return 0;
}

static int canonical_name(const char *host, struct strbuf *out)
{
	int status = -1;

#ifndef NO_IPV6
	struct addrinfo hints, *ai;
	memset (&hints, '\0', sizeof (hints));
	hints.ai_flags = AI_CANONNAME;
	if (!getaddrinfo(host, NULL, &hints, &ai)) {
		if (ai && ai->ai_canonname && strchr(ai->ai_canonname, '.')) {
			strbuf_addstr(out, ai->ai_canonname);
			status = 0;
		}
		freeaddrinfo(ai);
	}
#else
	struct hostent *he = gethostbyname(host);
	if (he && strchr(he->h_name, '.')) {
		strbuf_addstr(out, he->h_name);
		status = 0;
	}
#endif /* NO_IPV6 */

	return status;
}

static void add_domainname(struct strbuf *out, int *is_bogus)
{
	char buf[HOST_NAME_MAX + 1];

	if (xgethostname(buf, sizeof(buf))) {
		warning_errno("cannot get host name");
		strbuf_addstr(out, "(none)");
		*is_bogus = 1;
		return;
	}
	if (strchr(buf, '.'))
		strbuf_addstr(out, buf);
	else if (canonical_name(buf, out) < 0) {
		strbuf_addf(out, "%s.(none)", buf);
		*is_bogus = 1;
	}
}

static void copy_email(const struct passwd *pw, struct strbuf *email,
		       int *is_bogus)
{
	/*
	 * Make up a fake email address
	 * (name + '@' + hostname [+ '.' + domainname])
	 */
	strbuf_addstr(email, pw->pw_name);
	strbuf_addch(email, '@');

	if (!add_mailname_host(email))
		return;	/* read from "/etc/mailname" (Debian) */
	add_domainname(email, is_bogus);
}

const char *ident_default_name(void)
{
	if (!(ident_config_given & IDENT_NAME_GIVEN) && !git_default_name.len) {
		copy_gecos(xgetpwuid_self(&default_name_is_bogus), &git_default_name);
		strbuf_trim(&git_default_name);
	}
	return git_default_name.buf;
}

const char *ident_default_email(void)
{
	if (!(ident_config_given & IDENT_MAIL_GIVEN) && !git_default_email.len) {
		const char *email = getenv("EMAIL");

		if (email && email[0]) {
			strbuf_addstr(&git_default_email, email);
			committer_ident_explicitly_given |= IDENT_MAIL_GIVEN;
			author_ident_explicitly_given |= IDENT_MAIL_GIVEN;
		} else if ((email = query_user_email()) && email[0]) {
			strbuf_addstr(&git_default_email, email);
			free((char *)email);
		} else
			copy_email(xgetpwuid_self(&default_email_is_bogus),
				   &git_default_email, &default_email_is_bogus);
		strbuf_trim(&git_default_email);
	}
	return git_default_email.buf;
}

static const char *ident_default_date(void)
{
	if (!git_default_date.len)
		datestamp(&git_default_date);
	return git_default_date.buf;
}

void reset_ident_date(void)
{
	strbuf_reset(&git_default_date);
}

static int crud(unsigned char c)
{
	return  c <= 32  ||
		c == ',' ||
		c == ':' ||
		c == ';' ||
		c == '<' ||
		c == '>' ||
		c == '"' ||
		c == '\\' ||
		c == '\'';
}

static int has_non_crud(const char *str)
{
	for (; *str; str++) {
		if (!crud(*str))
			return 1;
	}
	return 0;
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

	/*
	 * Look from the end-of-line to find the trailing ">" of the mail
	 * address, even though we should already know it as split->mail_end.
	 * This can help in cases of broken idents with an extra ">" somewhere
	 * in the email address.  Note that we are assuming the timestamp will
	 * never have a ">" in it.
	 *
	 * Note that we will always find some ">" before going off the front of
	 * the string, because will always hit the split->mail_end closing
	 * bracket.
	 */
	for (cp = line + len - 1; *cp != '>'; cp--)
		;

	for (cp = cp + 1; cp < line + len && isspace(*cp); cp++)
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

/*
 * Returns the difference between the new and old length of the ident line.
 */
static ssize_t rewrite_ident_line(const char *person, size_t len,
				   struct strbuf *buf,
				   struct string_list *mailmap)
{
	size_t namelen, maillen;
	const char *name;
	const char *mail;
	struct ident_split ident;

	if (split_ident_line(&ident, person, len))
		return 0;

	mail = ident.mail_begin;
	maillen = ident.mail_end - ident.mail_begin;
	name = ident.name_begin;
	namelen = ident.name_end - ident.name_begin;

	if (map_user(mailmap, &mail, &maillen, &name, &namelen)) {
		struct strbuf namemail = STRBUF_INIT;
		size_t newlen;

		strbuf_addf(&namemail, "%.*s <%.*s>",
			    (int)namelen, name, (int)maillen, mail);

		strbuf_splice(buf, ident.name_begin - buf->buf,
			      ident.mail_end - ident.name_begin + 1,
			      namemail.buf, namemail.len);
		newlen = namemail.len;

		strbuf_release(&namemail);

		return newlen - (ident.mail_end - ident.name_begin);
	}

	return 0;
}

void apply_mailmap_to_header(struct strbuf *buf, const char **header,
			       struct string_list *mailmap)
{
	size_t buf_offset = 0;

	if (!mailmap)
		return;

	for (;;) {
		const char *person, *line;
		size_t i;
		int found_header = 0;

		line = buf->buf + buf_offset;
		if (!*line || *line == '\n')
			return; /* End of headers */

		for (i = 0; header[i]; i++)
			if (skip_prefix(line, header[i], &person)) {
				const char *endp = strchrnul(person, '\n');
				found_header = 1;
				buf_offset += endp - line;
				buf_offset += rewrite_ident_line(person, endp - person, buf, mailmap);
				break;
			}

		if (!found_header) {
			buf_offset = strchrnul(line, '\n') - buf->buf;
			if (buf->buf[buf_offset] == '\n')
				buf_offset++;
		}
	}
}

static void ident_env_hint(enum want_ident whose_ident)
{
	switch (whose_ident) {
	case WANT_AUTHOR_IDENT:
		fputs(_("Author identity unknown\n"), stderr);
		break;
	case WANT_COMMITTER_IDENT:
		fputs(_("Committer identity unknown\n"), stderr);
		break;
	default:
		break;
	}

	fputs(_("\n"
		"*** Please tell me who you are.\n"
		"\n"
		"Run\n"
		"\n"
		"  git config --global user.email \"you@example.com\"\n"
		"  git config --global user.name \"Your Name\"\n"
		"\n"
		"to set your account\'s default identity.\n"
		"Omit --global to set the identity only in this repository.\n"
		"\n"), stderr);
}

const char *fmt_ident(const char *name, const char *email,
		      enum want_ident whose_ident, const char *date_str, int flag)
{
	static int index;
	static struct strbuf ident_pool[2] = { STRBUF_INIT, STRBUF_INIT };
	int strict = (flag & IDENT_STRICT);
	int want_date = !(flag & IDENT_NO_DATE);
	int want_name = !(flag & IDENT_NO_NAME);

	struct strbuf *ident = &ident_pool[index];
	index = (index + 1) % ARRAY_SIZE(ident_pool);

	if (!email) {
		if (whose_ident == WANT_AUTHOR_IDENT && git_author_email.len)
			email = git_author_email.buf;
		else if (whose_ident == WANT_COMMITTER_IDENT && git_committer_email.len)
			email = git_committer_email.buf;
	}
	if (!email) {
		if (strict && ident_use_config_only
		    && !(ident_config_given & IDENT_MAIL_GIVEN)) {
			ident_env_hint(whose_ident);
			die(_("no email was given and auto-detection is disabled"));
		}
		email = ident_default_email();
		if (strict && default_email_is_bogus) {
			ident_env_hint(whose_ident);
			die(_("unable to auto-detect email address (got '%s')"), email);
		}
	}

	if (want_name) {
		int using_default = 0;
		if (!name) {
			if (whose_ident == WANT_AUTHOR_IDENT && git_author_name.len)
				name = git_author_name.buf;
			else if (whose_ident == WANT_COMMITTER_IDENT &&
					git_committer_name.len)
				name = git_committer_name.buf;
		}
		if (!name) {
			if (strict && ident_use_config_only
			    && !(ident_config_given & IDENT_NAME_GIVEN)) {
				ident_env_hint(whose_ident);
				die(_("no name was given and auto-detection is disabled"));
			}
			name = ident_default_name();
			using_default = 1;
			if (strict && default_name_is_bogus) {
				ident_env_hint(whose_ident);
				die(_("unable to auto-detect name (got '%s')"), name);
			}
		}
		if (!*name) {
			struct passwd *pw;
			if (strict) {
				if (using_default)
					ident_env_hint(whose_ident);
				die(_("empty ident name (for <%s>) not allowed"), email);
			}
			pw = xgetpwuid_self(NULL);
			name = pw->pw_name;
		}
		if (strict && !has_non_crud(name))
			die(_("name consists only of disallowed characters: %s"), name);
	}

	strbuf_reset(ident);
	if (want_name) {
		strbuf_addstr_without_crud(ident, name);
		strbuf_addstr(ident, " <");
	}
	strbuf_addstr_without_crud(ident, email);
	if (want_name)
		strbuf_addch(ident, '>');
	if (want_date) {
		strbuf_addch(ident, ' ');
		if (date_str && date_str[0]) {
			if (parse_date(date_str, ident) < 0)
				die(_("invalid date format: %s"), date_str);
		}
		else
			strbuf_addstr(ident, ident_default_date());
	}

	return ident->buf;
}

const char *fmt_name(enum want_ident whose_ident)
{
	char *name = NULL;
	char *email = NULL;

	switch (whose_ident) {
	case WANT_BLANK_IDENT:
		break;
	case WANT_AUTHOR_IDENT:
		name = getenv("GIT_AUTHOR_NAME");
		email = getenv("GIT_AUTHOR_EMAIL");
		break;
	case WANT_COMMITTER_IDENT:
		name = getenv("GIT_COMMITTER_NAME");
		email = getenv("GIT_COMMITTER_EMAIL");
		break;
	}
	return fmt_ident(name, email, whose_ident, NULL,
			IDENT_STRICT | IDENT_NO_DATE);
}

const char *git_author_info(int flag)
{
	if (getenv("GIT_AUTHOR_NAME"))
		author_ident_explicitly_given |= IDENT_NAME_GIVEN;
	if (getenv("GIT_AUTHOR_EMAIL"))
		author_ident_explicitly_given |= IDENT_MAIL_GIVEN;
	return fmt_ident(getenv("GIT_AUTHOR_NAME"),
			 getenv("GIT_AUTHOR_EMAIL"),
			 WANT_AUTHOR_IDENT,
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
			 WANT_COMMITTER_IDENT,
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

static int set_ident(const char *var, const char *value)
{
	if (!strcmp(var, "author.name")) {
		if (!value)
			return config_error_nonbool(var);
		strbuf_reset(&git_author_name);
		strbuf_addstr(&git_author_name, value);
		author_ident_explicitly_given |= IDENT_NAME_GIVEN;
		ident_config_given |= IDENT_NAME_GIVEN;
		return 0;
	}

	if (!strcmp(var, "author.email")) {
		if (!value)
			return config_error_nonbool(var);
		strbuf_reset(&git_author_email);
		strbuf_addstr(&git_author_email, value);
		author_ident_explicitly_given |= IDENT_MAIL_GIVEN;
		ident_config_given |= IDENT_MAIL_GIVEN;
		return 0;
	}

	if (!strcmp(var, "committer.name")) {
		if (!value)
			return config_error_nonbool(var);
		strbuf_reset(&git_committer_name);
		strbuf_addstr(&git_committer_name, value);
		committer_ident_explicitly_given |= IDENT_NAME_GIVEN;
		ident_config_given |= IDENT_NAME_GIVEN;
		return 0;
	}

	if (!strcmp(var, "committer.email")) {
		if (!value)
			return config_error_nonbool(var);
		strbuf_reset(&git_committer_email);
		strbuf_addstr(&git_committer_email, value);
		committer_ident_explicitly_given |= IDENT_MAIL_GIVEN;
		ident_config_given |= IDENT_MAIL_GIVEN;
		return 0;
	}

	if (!strcmp(var, "user.name")) {
		if (!value)
			return config_error_nonbool(var);
		strbuf_reset(&git_default_name);
		strbuf_addstr(&git_default_name, value);
		committer_ident_explicitly_given |= IDENT_NAME_GIVEN;
		author_ident_explicitly_given |= IDENT_NAME_GIVEN;
		ident_config_given |= IDENT_NAME_GIVEN;
		return 0;
	}

	if (!strcmp(var, "user.email")) {
		if (!value)
			return config_error_nonbool(var);
		strbuf_reset(&git_default_email);
		strbuf_addstr(&git_default_email, value);
		committer_ident_explicitly_given |= IDENT_MAIL_GIVEN;
		author_ident_explicitly_given |= IDENT_MAIL_GIVEN;
		ident_config_given |= IDENT_MAIL_GIVEN;
		return 0;
	}

	return 0;
}

int git_ident_config(const char *var, const char *value,
		     const struct config_context *ctx UNUSED,
		     void *data UNUSED)
{
	if (!strcmp(var, "user.useconfigonly")) {
		ident_use_config_only = git_config_bool(var, value);
		return 0;
	}

	return set_ident(var, value);
}

static void set_env_if(const char *key, const char *value, int *given, int bit)
{
	if ((*given & bit) || getenv(key))
		return; /* nothing to do */
	setenv(key, value, 0);
	*given |= bit;
}

void prepare_fallback_ident(const char *name, const char *email)
{
	set_env_if("GIT_AUTHOR_NAME", name,
		   &author_ident_explicitly_given, IDENT_NAME_GIVEN);
	set_env_if("GIT_AUTHOR_EMAIL", email,
		   &author_ident_explicitly_given, IDENT_MAIL_GIVEN);
	set_env_if("GIT_COMMITTER_NAME", name,
		   &committer_ident_explicitly_given, IDENT_NAME_GIVEN);
	set_env_if("GIT_COMMITTER_EMAIL", email,
		   &committer_ident_explicitly_given, IDENT_MAIL_GIVEN);
}

static int buf_cmp(const char *a_begin, const char *a_end,
		   const char *b_begin, const char *b_end)
{
	int a_len = a_end - a_begin;
	int b_len = b_end - b_begin;
	int min = a_len < b_len ? a_len : b_len;
	int cmp;

	cmp = memcmp(a_begin, b_begin, min);
	if (cmp)
		return cmp;

	return a_len - b_len;
}

int ident_cmp(const struct ident_split *a,
	      const struct ident_split *b)
{
	int cmp;

	cmp = buf_cmp(a->mail_begin, a->mail_end,
		      b->mail_begin, b->mail_end);
	if (cmp)
		return cmp;

	return buf_cmp(a->name_begin, a->name_end,
		       b->name_begin, b->name_end);
}
