#include "cache.h"
#include "string-list.h"
#include "mailmap.h"

#define DEBUG_MAILMAP 0
#if DEBUG_MAILMAP
#define debug_mm(...) fprintf(stderr, __VA_ARGS__)
#define debug_str(X) ((X) ? (X) : "(none)")
#else
static inline void debug_mm(const char *format, ...) {}
static inline const char *debug_str(const char *s) { return s; }
#endif

const char *git_mailmap_file;
const char *git_mailmap_blob;

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
	debug_mm("mailmap: -- complex: '%s' -> '%s' <%s>\n",
		 s, debug_str(mi->name), debug_str(mi->email));
	free(mi->name);
	free(mi->email);
}

static void free_mailmap_entry(void *p, const char *s)
{
	struct mailmap_entry *me = (struct mailmap_entry *)p;
	debug_mm("mailmap: removing entries for <%s>, with %d sub-entries\n",
		 s, me->namemap.nr);
	debug_mm("mailmap: - simple: '%s' <%s>\n",
		 debug_str(me->name), debug_str(me->email));

	free(me->name);
	free(me->email);

	me->namemap.strdup_strings = 1;
	string_list_clear_func(&me->namemap, free_mailmap_info);
}

/*
 * On some systems (e.g. MinGW 4.0), string.h has _only_ inline
 * definition of strcasecmp and no non-inline implementation is
 * supplied anywhere, which is, eh, "unusual"; we cannot take an
 * address of such a function to store it in namemap.cmp.  This is
 * here as a workaround---do not assign strcasecmp directly to
 * namemap.cmp until we know no systems that matter have such an
 * "unusual" string.h.
 */
static int namemap_cmp(const char *a, const char *b)
{
	return strcasecmp(a, b);
}

static void add_mapping(struct string_list *map,
			char *new_name, char *new_email,
			char *old_name, char *old_email)
{
	struct mailmap_entry *me;
	struct string_list_item *item;

	if (old_email == NULL) {
		old_email = new_email;
		new_email = NULL;
	}

	item = string_list_insert(map, old_email);
	if (item->util) {
		me = (struct mailmap_entry *)item->util;
	} else {
		me = xcalloc(1, sizeof(struct mailmap_entry));
		me->namemap.strdup_strings = 1;
		me->namemap.cmp = namemap_cmp;
		item->util = me;
	}

	if (old_name == NULL) {
		debug_mm("mailmap: adding (simple) entry for '%s'\n", old_email);

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
		debug_mm("mailmap: adding (complex) entry for '%s'\n", old_email);
		if (new_name)
			mi->name = xstrdup(new_name);
		if (new_email)
			mi->email = xstrdup(new_email);
		string_list_insert(&me->namemap, old_name)->util = mi;
	}

	debug_mm("mailmap:  '%s' <%s> -> '%s' <%s>\n",
		 debug_str(old_name), old_email,
		 debug_str(new_name), debug_str(new_email));
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

	*name = (nstart <= nend ? nstart : NULL);
	*email = left+1;
	*(nend+1) = '\0';
	*right++ = '\0';

	return (*right == '\0' ? NULL : right);
}

static void read_mailmap_line(struct string_list *map, char *buffer,
			      char **repo_abbrev)
{
	char *name1 = NULL, *email1 = NULL, *name2 = NULL, *email2 = NULL;
	if (buffer[0] == '#') {
		static const char abbrev[] = "# repo-abbrev:";
		int abblen = sizeof(abbrev) - 1;
		int len = strlen(buffer);

		if (!repo_abbrev)
			return;

		if (len && buffer[len - 1] == '\n')
			buffer[--len] = 0;
		if (!strncmp(buffer, abbrev, abblen)) {
			char *cp;

			free(*repo_abbrev);
			*repo_abbrev = xmalloc(len);

			for (cp = buffer + abblen; isspace(*cp); cp++)
				; /* nothing */
			strcpy(*repo_abbrev, cp);
		}
		return;
	}
	if ((name2 = parse_name_and_email(buffer, &name1, &email1, 0)) != NULL)
		parse_name_and_email(name2, &name2, &email2, 1);

	if (email1)
		add_mapping(map, name1, email1, name2, email2);
}

static int read_mailmap_file(struct string_list *map, const char *filename,
			     char **repo_abbrev)
{
	char buffer[1024];
	FILE *f;

	if (!filename)
		return 0;

	f = fopen(filename, "r");
	if (!f) {
		if (errno == ENOENT)
			return 0;
		return error("unable to open mailmap at %s: %s",
			     filename, strerror(errno));
	}

	while (fgets(buffer, sizeof(buffer), f) != NULL)
		read_mailmap_line(map, buffer, repo_abbrev);
	fclose(f);
	return 0;
}

static void read_mailmap_string(struct string_list *map, char *buf,
				char **repo_abbrev)
{
	while (*buf) {
		char *end = strchrnul(buf, '\n');

		if (*end)
			*end++ = '\0';

		read_mailmap_line(map, buf, repo_abbrev);
		buf = end;
	}
}

static int read_mailmap_blob(struct string_list *map,
			     const char *name,
			     char **repo_abbrev)
{
	unsigned char sha1[20];
	char *buf;
	unsigned long size;
	enum object_type type;

	if (!name)
		return 0;
	if (get_sha1(name, sha1) < 0)
		return 0;

	buf = read_sha1_file(sha1, &type, &size);
	if (!buf)
		return error("unable to read mailmap object at %s", name);
	if (type != OBJ_BLOB)
		return error("mailmap is not a blob: %s", name);

	read_mailmap_string(map, buf, repo_abbrev);

	free(buf);
	return 0;
}

int read_mailmap(struct string_list *map, char **repo_abbrev)
{
	int err = 0;

	map->strdup_strings = 1;
	map->cmp = namemap_cmp;

	if (!git_mailmap_blob && is_bare_repository())
		git_mailmap_blob = "HEAD:.mailmap";

	err |= read_mailmap_file(map, ".mailmap", repo_abbrev);
	err |= read_mailmap_blob(map, git_mailmap_blob, repo_abbrev);
	err |= read_mailmap_file(map, git_mailmap_file, repo_abbrev);
	return err;
}

void clear_mailmap(struct string_list *map)
{
	debug_mm("mailmap: clearing %d entries...\n", map->nr);
	map->strdup_strings = 1;
	string_list_clear_func(map, free_mailmap_entry);
	debug_mm("mailmap: cleared\n");
}

/*
 * Look for an entry in map that match string[0:len]; string[len]
 * does not have to be NUL (but it could be).
 */
static struct string_list_item *lookup_prefix(struct string_list *map,
					      const char *string, size_t len)
{
	int i = string_list_find_insert_index(map, string, 1);
	if (i < 0) {
		/* exact match */
		i = -1 - i;
		if (!string[len])
			return &map->items[i];
		/*
		 * that map entry matches exactly to the string, including
		 * the cruft at the end beyond "len".  That is not a match
		 * with string[0:len] that we are looking for.
		 */
	} else if (!string[len]) {
		/*
		 * asked with the whole string, and got nothing.  No
		 * matching entry can exist in the map.
		 */
		return NULL;
	}

	/*
	 * i is at the exact match to an overlong key, or location the
	 * overlong key would be inserted, which must come after the
	 * real location of the key if one exists.
	 */
	while (0 <= --i && i < map->nr) {
		int cmp = strncasecmp(map->items[i].string, string, len);
		if (cmp < 0)
			/*
			 * "i" points at a key definitely below the prefix;
			 * the map does not have string[0:len] in it.
			 */
			break;
		else if (!cmp && !map->items[i].string[len])
			/* found it */
			return &map->items[i];
		/*
		 * otherwise, the string at "i" may be string[0:len]
		 * followed by a string that sorts later than string[len:];
		 * keep trying.
		 */
	}
	return NULL;
}

int map_user(struct string_list *map,
	     const char **email, size_t *emaillen,
	     const char **name, size_t *namelen)
{
	struct string_list_item *item;
	struct mailmap_entry *me;

	debug_mm("map_user: map '%.*s' <%.*s>\n",
		 (int)*namelen, debug_str(*name),
		 (int)*emaillen, debug_str(*email));

	item = lookup_prefix(map, *email, *emaillen);
	if (item != NULL) {
		me = (struct mailmap_entry *)item->util;
		if (me->namemap.nr) {
			/*
			 * The item has multiple items, so we'll look up on
			 * name too. If the name is not found, we choose the
			 * simple entry.
			 */
			struct string_list_item *subitem;
			subitem = lookup_prefix(&me->namemap, *name, *namelen);
			if (subitem)
				item = subitem;
		}
	}
	if (item != NULL) {
		struct mailmap_info *mi = (struct mailmap_info *)item->util;
		if (mi->name == NULL && mi->email == NULL) {
			debug_mm("map_user:  -- (no simple mapping)\n");
			return 0;
		}
		if (mi->email) {
				*email = mi->email;
				*emaillen = strlen(*email);
		}
		if (mi->name) {
				*name = mi->name;
				*namelen = strlen(*name);
		}
		debug_mm("map_user:  to '%.*s' <%.*s>\n",
			 (int)*namelen, debug_str(*name),
			 (int)*emaillen, debug_str(*email));
		return 1;
	}
	debug_mm("map_user:  --\n");
	return 0;
}
