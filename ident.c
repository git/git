/*
 * ident.c
 *
 * create git identifier lines of the form "name <email> date"
 *
 * Copyright (C) 2005 Linus Torvalds
 */
#include "cache.h"

#include <pwd.h>
#include <netdb.h>

static char git_default_date[50];

static void copy_gecos(struct passwd *w, char *name, int sz)
{
	char *src, *dst;
	int len, nlen;

	nlen = strlen(w->pw_name);

	/* Traditionally GECOS field had office phone numbers etc, separated
	 * with commas.  Also & stands for capitalized form of the login name.
	 */

	for (len = 0, dst = name, src = w->pw_gecos; len < sz; src++) {
		int ch = *src;
		if (ch != '&') {
			*dst++ = ch;
			if (ch == 0 || ch == ',')
				break;
			len++;
			continue;
		}
		if (len + nlen < sz) {
			/* Sorry, Mr. McDonald... */
			*dst++ = toupper(*w->pw_name);
			memcpy(dst, w->pw_name + 1, nlen - 1);
			dst += nlen - 1;
		}
	}
	if (len < sz)
		name[len] = 0;
	else
		die("Your parents must have hated you!");

}

int setup_ident(void)
{
	int len;
	struct passwd *pw = getpwuid(getuid());

	if (!pw)
		die("You don't exist. Go away!");

	/* Get the name ("gecos") */
	copy_gecos(pw, git_default_name, sizeof(git_default_name));

	/* Make up a fake email address (name + '@' + hostname [+ '.' + domainname]) */
	len = strlen(pw->pw_name);
	if (len > sizeof(git_default_email)/2)
		die("Your sysadmin must hate you!");
	memcpy(git_default_email, pw->pw_name, len);
	git_default_email[len++] = '@';
	gethostname(git_default_email + len, sizeof(git_default_email) - len);
	if (!strchr(git_default_email+len, '.')) {
		struct hostent *he = gethostbyname(git_default_email + len);
		char *domainname;

		len = strlen(git_default_email);
		git_default_email[len++] = '.';
		if (he && (domainname = strchr(he->h_name, '.')))
			strlcpy(git_default_email + len, domainname + 1, sizeof(git_default_email) - len);
		else
			strlcpy(git_default_email + len, "(none)", sizeof(git_default_email) - len);
	}
	/* And set the default date */
	datestamp(git_default_date, sizeof(git_default_date));
	return 0;
}

static int add_raw(char *buf, int size, int offset, const char *str)
{
	int len = strlen(str);
	if (offset + len > size)
		return size;
	memcpy(buf + offset, str, len);
	return offset + len;
}

static int crud(unsigned char c)
{
	static char crud_array[256];
	static int crud_array_initialized = 0;

	if (!crud_array_initialized) {
		int k;

		for (k = 0; k <= 31; ++k) crud_array[k] = 1;
		crud_array[' '] = 1;
		crud_array['.'] = 1;
		crud_array[','] = 1;
		crud_array[':'] = 1;
		crud_array[';'] = 1;
		crud_array['<'] = 1;
		crud_array['>'] = 1;
		crud_array['"'] = 1;
		crud_array['\''] = 1;
		crud_array_initialized = 1;
	}
	return crud_array[c];
}

/*
 * Copy over a string to the destination, but avoid special
 * characters ('\n', '<' and '>') and remove crud at the end
 */
static int copy(char *buf, int size, int offset, const char *src)
{
	int i, len;
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
	 * a identification line
	 */
	for (i = 0; i < len; i++) {
		c = *src++;
		switch (c) {
		case '\n': case '<': case '>':
			continue;
		}
		if (offset >= size)
			return size;
		buf[offset++] = c;
	}
	return offset;
}

static const char au_env[] = "GIT_AUTHOR_NAME";
static const char co_env[] = "GIT_COMMITTER_NAME";
static const char *env_hint =
"\n*** Environment problem:\n"
"*** Your name cannot be determined from your system services (gecos).\n"
"*** You would need to set %s and %s\n"
"*** environment variables; otherwise you won't be able to perform\n"
"*** certain operations because of \"empty ident\" errors.\n"
"*** Alternatively, you can use user.name configuration variable.\n\n";

static const char *get_ident(const char *name, const char *email,
			     const char *date_str, int error_on_no_name)
{
	static char buffer[1000];
	char date[50];
	int i;

	if (!name)
		name = git_default_name;
	if (!email)
		email = git_default_email;

	if (!*name) {
		if (name == git_default_name && env_hint) {
			fprintf(stderr, env_hint, au_env, co_env);
			env_hint = NULL; /* warn only once, for "git-var -l" */
		}
		if (error_on_no_name)
			die("empty ident %s <%s> not allowed", name, email);
	}

	strcpy(date, git_default_date);
	if (date_str)
		parse_date(date_str, date, sizeof(date));

	i = copy(buffer, sizeof(buffer), 0, name);
	i = add_raw(buffer, sizeof(buffer), i, " <");
	i = copy(buffer, sizeof(buffer), i, email);
	i = add_raw(buffer, sizeof(buffer), i, "> ");
	i = copy(buffer, sizeof(buffer), i, date);
	if (i >= sizeof(buffer))
		die("Impossibly long personal identifier");
	buffer[i] = 0;
	return buffer;
}

const char *git_author_info(int error_on_no_name)
{
	return get_ident(getenv("GIT_AUTHOR_NAME"),
			 getenv("GIT_AUTHOR_EMAIL"),
			 getenv("GIT_AUTHOR_DATE"),
			 error_on_no_name);
}

const char *git_committer_info(int error_on_no_name)
{
	return get_ident(getenv("GIT_COMMITTER_NAME"),
			 getenv("GIT_COMMITTER_EMAIL"),
			 getenv("GIT_COMMITTER_DATE"),
			 error_on_no_name);
}
