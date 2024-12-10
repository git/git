#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "environment.h"
#include "string-list.h"
#include "mailmap.h"
#include "object-name.h"
#include "object-store-ll.h"
#include "setup.h"

char *git_mailmap_file;
char *git_mailmap_blob;

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

static void free_mailmap_info(void *p, const char *s UNUSED)
{
	struct mailmap_info *mi = (struct mailmap_info *)p;
	free(mi->name);
	free(mi->email);
	free(mi);
}

static void free_mailmap_entry(void *p, const char *s UNUSED)
{
	struct mailmap_entry *me = (struct mailmap_entry *)p;

	free(me->name);
	free(me->email);

	me->namemap.strdup_strings = 1;
	string_list_clear_func(&me->namemap, free_mailmap_info);
	free(me);
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

	if (!old_email) {
		old_email = new_email;
		new_email = NULL;
	}

	item = string_list_insert(map, old_email);
	if (item->util) {
		me = (struct mailmap_entry *)item->util;
	} else {
		CALLOC_ARRAY(me, 1);
		me->namemap.strdup_strings = 1;
		me->namemap.cmp = namemap_cmp;
		item->util = me;
	}

	if (!old_name) {
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
		mi->name = xstrdup_or_null(new_name);
		mi->email = xstrdup_or_null(new_email);
		string_list_insert(&me->namemap, old_name)->util = mi;
	}
}

static char *parse_name_and_email(char *buffer, char **name,
				  char **email, int allow_empty_email)
{
	char *left, *right, *nstart, *nend;
	*name = *email = NULL;

	if (!(left = strchr(buffer, '<')))
		return NULL;
	if (!(right = strchr(left + 1, '>')))
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

static void read_mailmap_line(struct string_list *map, char *buffer)
{
	char *name1 = NULL, *email1 = NULL, *name2 = NULL, *email2 = NULL;

	if (buffer[0] == '#')
		return;

	if ((name2 = parse_name_and_email(buffer, &name1, &email1, 0)))
		parse_name_and_email(name2, &name2, &email2, 1);

	if (email1)
		add_mapping(map, name1, email1, name2, email2);
}

int read_mailmap_file(struct string_list *map, const char *filename,
		      unsigned flags)
{
	char buffer[1024];
	FILE *f;
	int fd;

	if (!filename)
		return 0;

	if (flags & MAILMAP_NOFOLLOW)
		fd = open_nofollow(filename, O_RDONLY);
	else
		fd = open(filename, O_RDONLY);

	if (fd < 0) {
		if (errno == ENOENT)
			return 0;
		return error_errno("unable to open mailmap at %s", filename);
	}
	f = xfdopen(fd, "r");

	while (fgets(buffer, sizeof(buffer), f) != NULL)
		read_mailmap_line(map, buffer);
	fclose(f);
	return 0;
}

static void read_mailmap_string(struct string_list *map, char *buf)
{
	while (*buf) {
		char *end = strchrnul(buf, '\n');

		if (*end)
			*end++ = '\0';

		read_mailmap_line(map, buf);
		buf = end;
	}
}

int read_mailmap_blob(struct string_list *map, const char *name)
{
	struct object_id oid;
	char *buf;
	unsigned long size;
	enum object_type type;

	if (!name)
		return 0;
	if (repo_get_oid(the_repository, name, &oid) < 0)
		return 0;

	buf = repo_read_object_file(the_repository, &oid, &type, &size);
	if (!buf)
		return error("unable to read mailmap object at %s", name);
	if (type != OBJ_BLOB) {
		free(buf);
		return error("mailmap is not a blob: %s", name);
	}

	read_mailmap_string(map, buf);

	free(buf);
	return 0;
}

int read_mailmap(struct string_list *map)
{
	int err = 0;

	map->strdup_strings = 1;
	map->cmp = namemap_cmp;

	if (!git_mailmap_blob && is_bare_repository())
		git_mailmap_blob = xstrdup("HEAD:.mailmap");

	if (!startup_info->have_repository || !is_bare_repository())
		err |= read_mailmap_file(map, ".mailmap",
					 startup_info->have_repository ?
					 MAILMAP_NOFOLLOW : 0);
	if (startup_info->have_repository)
		err |= read_mailmap_blob(map, git_mailmap_blob);
	err |= read_mailmap_file(map, git_mailmap_file, 0);
	return err;
}

void clear_mailmap(struct string_list *map)
{
	map->strdup_strings = 1;
	string_list_clear_func(map, free_mailmap_entry);
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

	item = lookup_prefix(map, *email, *emaillen);
	if (item) {
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
	if (item) {
		struct mailmap_info *mi = (struct mailmap_info *)item->util;
		if (mi->name == NULL && mi->email == NULL)
			return 0;
		if (mi->email) {
				*email = mi->email;
				*emaillen = strlen(*email);
		}
		if (mi->name) {
				*name = mi->name;
				*namelen = strlen(*name);
		}
		return 1;
	}
	return 0;
}
