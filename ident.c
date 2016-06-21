/*
 * ident.c
 *
 * create git identifier lines of the form "name <email> date"
 *
 * Copyright (C) 2005 Linus Torvalds
 */
#include "cache.h"

#include <pwd.h>
#include <time.h>
#include <ctype.h>

static char real_email[1000];
static char real_name[1000];
static char real_date[50];

int setup_ident(void)
{
	int len;
	struct passwd *pw = getpwuid(getuid());

	if (!pw)
		die("You don't exist. Go away!");

	/* Get the name ("gecos") */
	len = strlen(pw->pw_gecos);
	if (len >= sizeof(real_name))
		die("Your parents must have hated you!");
	memcpy(real_name, pw->pw_gecos, len+1);

	/* Make up a fake email address (name + '@' + hostname [+ '.' + domainname]) */
	len = strlen(pw->pw_name);
	if (len > sizeof(real_email)/2)
		die("Your sysadmin must hate you!");
	memcpy(real_email, pw->pw_name, len);
	real_email[len++] = '@';
	gethostname(real_email + len, sizeof(real_email) - len);
	if (!strchr(real_email+len, '.')) {
		len = strlen(real_email);
		real_email[len++] = '.';
		getdomainname(real_email+len, sizeof(real_email)-len);
	}

	/* And set the default date */
	datestamp(real_date, sizeof(real_date));
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
	static const char crud_array[256] = {
		[0 ... 31] = 1,
		[' '] = 1,
		['.'] = 1, [','] = 1,
		[':'] = 1, [';'] = 1,
		['<'] = 1, ['>'] = 1,
		['"'] = 1, ['\''] = 1,
	};
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
	 * characters '\n' '<' and '>' that act as delimeters on
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

char *get_ident(const char *name, const char *email, const char *date_str)
{
	static char buffer[1000];
	char date[50];
	int i;

	if (!name)
		name = real_name;
	if (!email)
		email = real_email;
	strcpy(date, real_date);
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

char *git_author_info(void)
{
	return get_ident(gitenv("GIT_AUTHOR_NAME"), gitenv("GIT_AUTHOR_EMAIL"), gitenv("GIT_AUTHOR_DATE"));
}

char *git_committer_info(void)
{
	return get_ident(gitenv("GIT_COMMITTER_NAME"), gitenv("GIT_COMMITTER_EMAIL"), gitenv("GIT_COMMITTER_DATE"));
}
