#include "tag.h"
#include "commit.h"
#include "cache.h"

const char *commit_type = "commit";

static struct commit *check_commit(struct object *obj, unsigned char *sha1)
{
	if (obj->type != commit_type) {
		error("Object %s is a %s, not a commit", 
		      sha1_to_hex(sha1), obj->type);
		return NULL;
	}
	return (struct commit *) obj;
}

struct commit *lookup_commit_reference(unsigned char *sha1)
{
	struct object *obj = parse_object(sha1);

	if (!obj)
		return NULL;
	if (obj->type == tag_type)
		obj = ((struct tag *)obj)->tagged;
	return check_commit(obj, sha1);
}

struct commit *lookup_commit(unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj) {
		struct commit *ret = xmalloc(sizeof(struct commit));
		memset(ret, 0, sizeof(struct commit));
		created_object(sha1, &ret->object);
		ret->object.type = commit_type;
		return ret;
	}
	if (!obj->type)
		obj->type = commit_type;
	return check_commit(obj, sha1);
}

static unsigned long parse_commit_date(const char *buf)
{
	unsigned long date;

	if (memcmp(buf, "author", 6))
		return 0;
	while (*buf++ != '\n')
		/* nada */;
	if (memcmp(buf, "committer", 9))
		return 0;
	while (*buf++ != '>')
		/* nada */;
	date = strtoul(buf, NULL, 10);
	if (date == ULONG_MAX)
		date = 0;
	return date;
}

int parse_commit_buffer(struct commit *item, void *buffer, unsigned long size)
{
	void *bufptr = buffer;
	unsigned char parent[20];

	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	get_sha1_hex(bufptr + 5, parent);
	item->tree = lookup_tree(parent);
	if (item->tree)
		add_ref(&item->object, &item->tree->object);
	bufptr += 46; /* "tree " + "hex sha1" + "\n" */
	while (!memcmp(bufptr, "parent ", 7) &&
	       !get_sha1_hex(bufptr + 7, parent)) {
		struct commit *new_parent = lookup_commit(parent);
		if (new_parent) {
			commit_list_insert(new_parent, &item->parents);
			add_ref(&item->object, &new_parent->object);
		}
		bufptr += 48;
	}
	item->date = parse_commit_date(bufptr);
	return 0;
}

int parse_commit(struct commit *item)
{
	char type[20];
	void *buffer;
	unsigned long size;
	int ret;

	if (item->object.parsed)
		return 0;
	buffer = read_sha1_file(item->object.sha1, type, &size);
	if (!buffer)
		return error("Could not read %s",
			     sha1_to_hex(item->object.sha1));
	if (strcmp(type, commit_type)) {
		free(buffer);
		return error("Object %s not a commit",
			     sha1_to_hex(item->object.sha1));
	}
	ret = parse_commit_buffer(item, buffer, size);
	if (!ret) {
		item->buffer = buffer;
		return 0;
	}
	free(buffer);
	return ret;
}

struct commit_list *commit_list_insert(struct commit *item, struct commit_list **list_p)
{
	struct commit_list *new_list = xmalloc(sizeof(struct commit_list));
	new_list->item = item;
	new_list->next = *list_p;
	*list_p = new_list;
	return new_list;
}

void free_commit_list(struct commit_list *list)
{
	while (list) {
		struct commit_list *temp = list;
		list = temp->next;
		free(temp);
	}
}

static void insert_by_date(struct commit_list **list, struct commit *item)
{
	struct commit_list **pp = list;
	struct commit_list *p;
	while ((p = *pp) != NULL) {
		if (p->item->date < item->date) {
			break;
		}
		pp = &p->next;
	}
	commit_list_insert(item, pp);
}

	
void sort_by_date(struct commit_list **list)
{
	struct commit_list *ret = NULL;
	while (*list) {
		insert_by_date(&ret, (*list)->item);
		*list = (*list)->next;
	}
	*list = ret;
}

struct commit *pop_most_recent_commit(struct commit_list **list,
				      unsigned int mark)
{
	struct commit *ret = (*list)->item;
	struct commit_list *parents = ret->parents;
	struct commit_list *old = *list;

	*list = (*list)->next;
	free(old);

	while (parents) {
		struct commit *commit = parents->item;
		parse_commit(commit);
		if (!(commit->object.flags & mark)) {
			commit->object.flags |= mark;
			insert_by_date(list, commit);
		}
		parents = parents->next;
	}
	return ret;
}

/*
 * Generic support for pretty-printing the header
 */
static int get_one_line(const char *msg, unsigned long len)
{
	int ret = 0;

	while (len--) {
		char c = *msg++;
		ret++;
		if (c == '\n')
			break;
		if (!c)
			return 0;
	}
	return ret;
}

static int add_author_info(char *buf, const char *line, int len)
{
	char *date;
	unsigned int namelen;
	unsigned long time;
	int tz;

	line += strlen("author ");
	date = strchr(line, '>');
	if (!date)
		return 0;
	namelen = ++date - line;
	time = strtoul(date, &date, 10);
	tz = strtol(date, NULL, 10);

	return sprintf(buf, "Author: %.*s\nDate:   %s\n",
		namelen, line,
		show_date(time, tz));
}

unsigned long pretty_print_commit(const char *msg, unsigned long len, char *buf, unsigned long space)
{
	int hdr = 1;
	unsigned long offset = 0;

	for (;;) {
		const char *line = msg;
		int linelen = get_one_line(msg, len);

		if (!linelen)
			break;

		/*
		 * We want some slop for indentation and a possible
		 * final "...". Thus the "+ 20".
		 */
		if (offset + linelen + 20 > space) {
			memcpy(buf + offset, "    ...\n", 8);
			offset += 8;
			break;
		}

		msg += linelen;
		len -= linelen;
		if (linelen == 1)
			hdr = 0;
		if (hdr) {
			if (!memcmp(line, "author ", 7))
				offset += add_author_info(buf + offset, line, linelen);
			continue;
		}
		memset(buf + offset, ' ', 4);
		memcpy(buf + offset + 4, line, linelen);
		offset += linelen + 4;
	}
	/* Make sure there is an EOLN */
	if (buf[offset - 1] != '\n')
		buf[offset++] = '\n';
	buf[offset] = '\0';
	return offset;
}
