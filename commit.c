#include <ctype.h>
#include "tag.h"
#include "commit.h"
#include "cache.h"

struct sort_node
{
	/*
         * the number of children of the associated commit
         * that also occur in the list being sorted.
         */
	unsigned int indegree;

	/*
         * reference to original list item that we will re-use
         * on output.
         */
	struct commit_list * list_item;

};

const char *commit_type = "commit";

enum cmit_fmt get_commit_format(const char *arg)
{
	if (!*arg)
		return CMIT_FMT_DEFAULT;
	if (!strcmp(arg, "=raw"))
		return CMIT_FMT_RAW;
	if (!strcmp(arg, "=medium"))
		return CMIT_FMT_MEDIUM;
	if (!strcmp(arg, "=short"))
		return CMIT_FMT_SHORT;
	if (!strcmp(arg, "=full"))
		return CMIT_FMT_FULL;
	die("invalid --pretty format");
}

static struct commit *check_commit(struct object *obj, const unsigned char *sha1)
{
	if (obj->type != commit_type) {
		error("Object %s is a %s, not a commit", 
		      sha1_to_hex(sha1), obj->type);
		return NULL;
	}
	return (struct commit *) obj;
}

struct commit *lookup_commit_reference(const unsigned char *sha1)
{
	struct object *obj = parse_object(sha1);

	if (!obj)
		return NULL;
	if (obj->type == tag_type)
		obj = ((struct tag *)obj)->tagged;
	return check_commit(obj, sha1);
}

struct commit *lookup_commit(const unsigned char *sha1)
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
	struct commit_list **pptr;

	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	get_sha1_hex(bufptr + 5, parent);
	item->tree = lookup_tree(parent);
	if (item->tree)
		add_ref(&item->object, &item->tree->object);
	bufptr += 46; /* "tree " + "hex sha1" + "\n" */
	pptr = &item->parents;
	while (!memcmp(bufptr, "parent ", 7) &&
	       !get_sha1_hex(bufptr + 7, parent)) {
		struct commit *new_parent = lookup_commit(parent);
		if (new_parent) {
			pptr = &commit_list_insert(new_parent, pptr)->next;
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

struct commit_list * insert_by_date(struct commit *item, struct commit_list **list)
{
	struct commit_list **pp = list;
	struct commit_list *p;
	while ((p = *pp) != NULL) {
		if (p->item->date < item->date) {
			break;
		}
		pp = &p->next;
	}
	return commit_list_insert(item, pp);
}

	
void sort_by_date(struct commit_list **list)
{
	struct commit_list *ret = NULL;
	while (*list) {
		insert_by_date((*list)->item, &ret);
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
			insert_by_date(commit, list);
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

static int add_user_info(const char *what, enum cmit_fmt fmt, char *buf, const char *line)
{
	char *date;
	unsigned int namelen;
	unsigned long time;
	int tz, ret;

	date = strchr(line, '>');
	if (!date)
		return 0;
	namelen = ++date - line;
	time = strtoul(date, &date, 10);
	tz = strtol(date, NULL, 10);

	ret = sprintf(buf, "%s: %.*s\n", what, namelen, line);
	if (fmt == CMIT_FMT_MEDIUM)
		ret += sprintf(buf + ret, "Date:   %s\n", show_date(time, tz));
	return ret;
}

static int is_empty_line(const char *line, int len)
{
	while (len && isspace(line[len-1]))
		len--;
	return !len;
}

static int add_parent_info(enum cmit_fmt fmt, char *buf, const char *line, int parents)
{
	int offset = 0;
	switch (parents) {
	case 1:
		break;
	case 2:
		/* Go back to the previous line: 40 characters of previous parent, and one '\n' */
		offset = sprintf(buf, "Merge: %.40s\n", line-41);
		/* Fallthrough */
	default:
		/* Replace the previous '\n' with a space */
		buf[offset-1] = ' ';
		offset += sprintf(buf + offset, "%.40s\n", line+7);
	}
	return offset;
}

unsigned long pretty_print_commit(enum cmit_fmt fmt, const char *msg, unsigned long len, char *buf, unsigned long space)
{
	int hdr = 1, body = 0;
	unsigned long offset = 0;
	int parents = 0;

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
		if (hdr) {
			if (linelen == 1) {
				hdr = 0;
				buf[offset++] = '\n';
				continue;
			}
			if (fmt == CMIT_FMT_RAW) {
				memcpy(buf + offset, line, linelen);
				offset += linelen;
				continue;
			}
			if (!memcmp(line, "parent ", 7)) {
				if (linelen != 48)
					die("bad parent line in commit");
				offset += add_parent_info(fmt, buf + offset, line, ++parents);
			}
			if (!memcmp(line, "author ", 7))
				offset += add_user_info("Author", fmt, buf + offset, line + 7);
			if (fmt == CMIT_FMT_FULL) {
				if (!memcmp(line, "committer ", 10))
					offset += add_user_info("Commit", fmt, buf + offset, line + 10);
			}
			continue;
		}

		if (is_empty_line(line, linelen)) {
			if (!body)
				continue;
			if (fmt == CMIT_FMT_SHORT)
				break;
		} else {
			body = 1;
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

struct commit *pop_commit(struct commit_list **stack)
{
	struct commit_list *top = *stack;
	struct commit *item = top ? top->item : NULL;

	if (top) {
		*stack = top->next;
		free(top);
	}
	return item;
}

int count_parents(struct commit * commit)
{
        int count = 0;
        struct commit_list * parents = commit->parents;
        for (count=0;parents; parents=parents->next,count++)
          ;
        return count;
}

/*
 * Performs an in-place topological sort on the list supplied.
 */
void sort_in_topological_order(struct commit_list ** list)
{
	struct commit_list * next = *list;
	struct commit_list * work = NULL;
	struct commit_list ** pptr = list;
	struct sort_node * nodes;
	struct sort_node * next_nodes;
	int count = 0;

	/* determine the size of the list */
	while (next) {
		next = next->next;
		count++;
	}
	/* allocate an array to help sort the list */
	nodes = xcalloc(count, sizeof(*nodes));
	/* link the list to the array */
	next_nodes = nodes;
	next=*list;
	while (next) {
		next_nodes->list_item = next;
		next->item->object.util = next_nodes;
		next_nodes++;
		next = next->next;
	}
	/* update the indegree */
	next=*list;
	while (next) {
		struct commit_list * parents = next->item->parents;
		while (parents) {
			struct commit * parent=parents->item;
			struct sort_node * pn = (struct sort_node *)parent->object.util;
			
			if (pn)
				pn->indegree++;
			parents=parents->next;
		}
		next=next->next;
	}
	/* 
         * find the tips
         *
         * tips are nodes not reachable from any other node in the list 
         * 
         * the tips serve as a starting set for the work queue.
         */
	next=*list;
	while (next) {
		struct sort_node * node = (struct sort_node *)next->item->object.util;

		if (node->indegree == 0) {
			commit_list_insert(next->item, &work);
		}
		next=next->next;
	}
	/* process the list in topological order */
	while (work) {
		struct commit * work_item = pop_commit(&work);
		struct sort_node * work_node = (struct sort_node *)work_item->object.util;
		struct commit_list * parents = work_item->parents;

		while (parents) {
			struct commit * parent=parents->item;
			struct sort_node * pn = (struct sort_node *)parent->object.util;
			
			if (pn) {
				/* 
				 * parents are only enqueued for emission 
                                 * when all their children have been emitted thereby
                                 * guaranteeing topological order.
                                 */
				pn->indegree--;
				if (!pn->indegree) 
					commit_list_insert(parent, &work);
			}
			parents=parents->next;
		}
		/*
                 * work_item is a commit all of whose children
                 * have already been emitted. we can emit it now.
                 */
		*pptr = work_node->list_item;
		pptr = &(*pptr)->next;
		*pptr = NULL;
		work_item->object.util = NULL;
	}
	free(nodes);
}
