#include "cache.h"
#include "string-list.h"
#include "mailmap.h"

#define DEBUG_MAILMAP 0
#if DEBUG_MAILMAP
#define debug_mm(...) fprintf(stderr, __VA_ARGS__)
#else
static inline void debug_mm(const char *format, ...) {}
#endif

const char *git_mailmap_file;

struct mailmap_info {
	char *name;
	char *email;
};

struct mailmap_entry {
	/* name and email for the simple mail-only case */
	char *name;
	char *email;

	/* name and email for the complex mail and name matching case */
	struct string_list namemap;
};

static void free_mailmap_info(void *p, const char *s)
{
	struct mailmap_info *mi = (struct mailmap_info *)p;
	debug_mm("mailmap: -- complex: '%s' -> '%s' <%s>\n", s, mi->name, mi->email);
	free(mi->name);
	free(mi->email);
}

static void free_mailmap_entry(void *p, const char *s)
{
	struct mailmap_entry *me = (struct mailmap_entry *)p;
	debug_mm("mailmap: removing entries for <%s>, with %d sub-entries\n", s, me->namemap.nr);
	debug_mm("mailmap: - simple: '%s' <%s>\n", me->name, me->email);
	free(me->name);
	free(me->email);

	me->namemap.strdup_strings = 1;
	string_list_clear_func(&me->namemap, free_mailmap_info);
}

static void add_mapping(struct string_list *map,
			char *new_name, char *new_email, char *old_name, char *old_email)
{
	struct mailmap_entry *me;
	int index;
	char *p;

	if (old_email)
		for (p = old_email; *p; p++)
			*p = tolower(*p);
	if (new_email)
		for (p = new_email; *p; p++)
			*p = tolower(*p);

	if (old_email == NULL) {
		old_email = new_email;
		new_email = NULL;
	}

	if ((index = string_list_find_insert_index(map, old_email, 1)) < 0) {
		/* mailmap entry exists, invert index value */
		index = -1 - index;
	} else {
		/* create mailmap entry */
		struct string_list_item *item = string_list_insert_at_index(map, index, old_email);
		item->util = xcalloc(1, sizeof(struct mailmap_entry));
		((struct mailmap_entry *)item->util)->namemap.strdup_strings = 1;
	}
	me = (struct mailmap_entry *)map->items[index].util;

	if (old_name == NULL) {
		debug_mm("mailmap: adding (simple) entry for %s at index %d\n", old_email, index);
		/* Replace current name and new email for simple entry */
		if (new_name) {
			free(me->name);
			me->name = xstrdup(new_name);
		}
		if (new_email) {
			free(me->email);
			me->email = xstrdup(new_email);
		}
	} else {
		struct mailmap_info *mi = xcalloc(1, sizeof(struct mailmap_info));
		debug_mm("mailmap: adding (complex) entry for %s at index %d\n", old_email, index);
		if (new_name)
			mi->name = xstrdup(new_name);
		if (new_email)
			mi->email = xstrdup(new_email);
		string_list_insert(&me->namemap, old_name)->util = mi;
	}

	debug_mm("mailmap:  '%s' <%s> -> '%s' <%s>\n",
		 old_name, old_email, new_name, new_email);
}

static char *parse_name_and_email(char *buffer, char **name,
		char **email, int allow_empty_email)
{
	char *left, *right, *nstart, *nend;
	*name = *email = NULL;

	if ((left = strchr(buffer, '<')) == NULL)
		return NULL;
	if ((right = strchr(left+1, '>')) == NULL)
		return NULL;
	if (!allow_empty_email && (left+1 == right))
		return NULL;

	/* remove whitespace from beginning and end of name */
	nstart = buffer;
	while (isspace(*nstart) && nstart < left)
		++nstart;
	nend = left-1;
	while (nend > nstart && isspace(*nend))
		--nend;

	*name = (nstart < nend ? nstart : NULL);
	*email = left+1;
	*(nend+1) = '\0';
	*right++ = '\0';

	return (*right == '\0' ? NULL : right);
}

static int read_single_mailmap(struct string_list *map, const char *filename, char **repo_abbrev)
{
	char buffer[1024];
	FILE *f = (filename == NULL ? NULL : fopen(filename, "r"));

	if (f == NULL)
		return 1;
	while (fgets(buffer, sizeof(buffer), f) != NULL) {
		char *name1 = NULL, *email1 = NULL, *name2 = NULL, *email2 = NULL;
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
		if ((name2 = parse_name_and_email(buffer, &name1, &email1, 0)) != NULL)
			parse_name_and_email(name2, &name2, &email2, 1);

		if (email1)
			add_mapping(map, name1, email1, name2, email2);
	}
	fclose(f);
	return 0;
}

int read_mailmap(struct string_list *map, char **repo_abbrev)
{
	map->strdup_strings = 1;
	/* each failure returns 1, so >1 means both calls failed */
	return read_single_mailmap(map, ".mailmap", repo_abbrev) +
	       read_single_mailmap(map, git_mailmap_file, repo_abbrev) > 1;
}

void clear_mailmap(struct string_list *map)
{
	debug_mm("mailmap: clearing %d entries...\n", map->nr);
	map->strdup_strings = 1;
	string_list_clear_func(map, free_mailmap_entry);
	debug_mm("mailmap: cleared\n");
}

int map_user(struct string_list *map,
	     char *email, int maxlen_email, char *name, int maxlen_name)
{
	char *end_of_email;
	struct string_list_item *item;
	struct mailmap_entry *me;
	char buf[1024], *mailbuf;
	int i;

	/* figure out space requirement for email */
	end_of_email = strchr(email, '>');
	if (!end_of_email) {
		/* email passed in might not be wrapped in <>, but end with a \0 */
		end_of_email = memchr(email, '\0', maxlen_email);
		if (!end_of_email)
			return 0;
	}
	if (end_of_email - email + 1 < sizeof(buf))
		mailbuf = buf;
	else
		mailbuf = xmalloc(end_of_email - email + 1);

	/* downcase the email address */
	for (i = 0; i < end_of_email - email; i++)
		mailbuf[i] = tolower(email[i]);
	mailbuf[i] = 0;

	debug_mm("map_user: map '%s' <%s>\n", name, mailbuf);
	item = string_list_lookup(map, mailbuf);
	if (item != NULL) {
		me = (struct mailmap_entry *)item->util;
		if (me->namemap.nr) {
			/* The item has multiple items, so we'll look up on name too */
			/* If the name is not found, we choose the simple entry      */
			struct string_list_item *subitem = string_list_lookup(&me->namemap, name);
			if (subitem)
				item = subitem;
		}
	}
	if (mailbuf != buf)
		free(mailbuf);
	if (item != NULL) {
		struct mailmap_info *mi = (struct mailmap_info *)item->util;
		if (mi->name == NULL && (mi->email == NULL || maxlen_email == 0)) {
			debug_mm("map_user:  -- (no simple mapping)\n");
			return 0;
		}
		if (maxlen_email && mi->email)
			strlcpy(email, mi->email, maxlen_email);
		else
			*end_of_email = '\0';
		if (maxlen_name && mi->name)
			strlcpy(name, mi->name, maxlen_name);
		debug_mm("map_user:  to '%s' <%s>\n", name, mi->email ? mi->email : "");
		return 1;
	}
	debug_mm("map_user:  --\n");
	return 0;
}
