#include "cache.h"
#include "path-list.h"
#include "mailmap.h"

int read_mailmap(struct path_list *map, const char *filename, char **repo_abbrev)
{
	char buffer[1024];
	FILE *f = fopen(filename, "r");

	if (f == NULL)
		return 1;
	while (fgets(buffer, sizeof(buffer), f) != NULL) {
		char *end_of_name, *left_bracket, *right_bracket;
		char *name, *email;
		int i;
		if (buffer[0] == '#') {
			static const char abbrev[] = "# repo-abbrev:";
			int abblen = sizeof(abbrev) - 1;
			int len = strlen(buffer);

			if (!repo_abbrev)
				continue;

			if (len && buffer[len - 1] == '\n')
				buffer[--len] = 0;
			if (!strncmp(buffer, abbrev, abblen)) {
				char *cp;

				if (repo_abbrev)
					free(*repo_abbrev);
				*repo_abbrev = xmalloc(len);

				for (cp = buffer + abblen; isspace(*cp); cp++)
					; /* nothing */
				strcpy(*repo_abbrev, cp);
			}
			continue;
		}
		if ((left_bracket = strchr(buffer, '<')) == NULL)
			continue;
		if ((right_bracket = strchr(left_bracket + 1, '>')) == NULL)
			continue;
		if (right_bracket == left_bracket + 1)
			continue;
		for (end_of_name = left_bracket; end_of_name != buffer
				&& isspace(end_of_name[-1]); end_of_name--)
			/* keep on looking */
		if (end_of_name == buffer)
			continue;
		name = xmalloc(end_of_name - buffer + 1);
		strlcpy(name, buffer, end_of_name - buffer + 1);
		email = xmalloc(right_bracket - left_bracket);
		for (i = 0; i < right_bracket - left_bracket - 1; i++)
			email[i] = tolower(left_bracket[i + 1]);
		email[right_bracket - left_bracket - 1] = '\0';
		path_list_insert(email, map)->util = name;
	}
	fclose(f);
	return 0;
}

int map_email(struct path_list *map, const char *email, char *name, int maxlen)
{
	char *p;
	struct path_list_item *item;
	char buf[1024], *mailbuf;
	int i;

	/* autocomplete common developers */
	p = strchr(email, '>');
	if (!p)
		return 0;
	if (p - email + 1 < sizeof(buf))
		mailbuf = buf;
	else
		mailbuf = xmalloc(p - email + 1);

	/* downcase the email address */
	for (i = 0; i < p - email; i++)
		mailbuf[i] = tolower(email[i]);
	mailbuf[i] = 0;
	item = path_list_lookup(mailbuf, map);
	if (mailbuf != buf)
		free(mailbuf);
	if (item != NULL) {
		const char *realname = (const char *)item->util;
		strlcpy(name, realname, maxlen);
		return 1;
	}
	return 0;
}

