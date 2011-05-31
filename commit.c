#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "pkt-line.h"
#include "utf8.h"
#include "diff.h"
#include "revision.h"
#include "notes.h"

int save_commit_buffer = 1;

const char *commit_type = "commit";

static struct commit *check_commit(struct object *obj,
				   const unsigned char *sha1,
				   int quiet)
{
	if (obj->type != OBJ_COMMIT) {
		if (!quiet)
			error("Object %s is a %s, not a commit",
			      sha1_to_hex(sha1), typename(obj->type));
		return NULL;
	}
	return (struct commit *) obj;
}

struct commit *lookup_commit_reference_gently(const unsigned char *sha1,
					      int quiet)
{
	struct object *obj = deref_tag(parse_object(sha1), NULL, 0);

	if (!obj)
		return NULL;
	return check_commit(obj, sha1, quiet);
}

struct commit *lookup_commit_reference(const unsigned char *sha1)
{
	return lookup_commit_reference_gently(sha1, 0);
}

struct commit *lookup_commit(const unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj)
		return create_object(sha1, OBJ_COMMIT, alloc_commit_node());
	if (!obj->type)
		obj->type = OBJ_COMMIT;
	return check_commit(obj, sha1, 0);
}

struct commit *lookup_commit_reference_by_name(const char *name)
{
	unsigned char sha1[20];
	struct commit *commit;

	if (get_sha1(name, sha1))
		return NULL;
	commit = lookup_commit_reference(sha1);
	if (!commit || parse_commit(commit))
		return NULL;
	return commit;
}

static unsigned long parse_commit_date(const char *buf, const char *tail)
{
	const char *dateptr;

	if (buf + 6 >= tail)
		return 0;
	if (memcmp(buf, "author", 6))
		return 0;
	while (buf < tail && *buf++ != '\n')
		/* nada */;
	if (buf + 9 >= tail)
		return 0;
	if (memcmp(buf, "committer", 9))
		return 0;
	while (buf < tail && *buf++ != '>')
		/* nada */;
	if (buf >= tail)
		return 0;
	dateptr = buf;
	while (buf < tail && *buf++ != '\n')
		/* nada */;
	if (buf >= tail)
		return 0;
	/* dateptr < buf && buf[-1] == '\n', so strtoul will stop at buf-1 */
	return strtoul(dateptr, NULL, 10);
}

static struct commit_graft **commit_graft;
static int commit_graft_alloc, commit_graft_nr;

static int commit_graft_pos(const unsigned char *sha1)
{
	int lo, hi;
	lo = 0;
	hi = commit_graft_nr;
	while (lo < hi) {
		int mi = (lo + hi) / 2;
		struct commit_graft *graft = commit_graft[mi];
		int cmp = hashcmp(sha1, graft->sha1);
		if (!cmp)
			return mi;
		if (cmp < 0)
			hi = mi;
		else
			lo = mi + 1;
	}
	return -lo - 1;
}

int register_commit_graft(struct commit_graft *graft, int ignore_dups)
{
	int pos = commit_graft_pos(graft->sha1);

	if (0 <= pos) {
		if (ignore_dups)
			free(graft);
		else {
			free(commit_graft[pos]);
			commit_graft[pos] = graft;
		}
		return 1;
	}
	pos = -pos - 1;
	if (commit_graft_alloc <= ++commit_graft_nr) {
		commit_graft_alloc = alloc_nr(commit_graft_alloc);
		commit_graft = xrealloc(commit_graft,
					sizeof(*commit_graft) *
					commit_graft_alloc);
	}
	if (pos < commit_graft_nr)
		memmove(commit_graft + pos + 1,
			commit_graft + pos,
			(commit_graft_nr - pos - 1) *
			sizeof(*commit_graft));
	commit_graft[pos] = graft;
	return 0;
}

struct commit_graft *read_graft_line(char *buf, int len)
{
	/* The format is just "Commit Parent1 Parent2 ...\n" */
	int i;
	struct commit_graft *graft = NULL;

	while (len && isspace(buf[len-1]))
		buf[--len] = '\0';
	if (buf[0] == '#' || buf[0] == '\0')
		return NULL;
	if ((len + 1) % 41)
		goto bad_graft_data;
	i = (len + 1) / 41 - 1;
	graft = xmalloc(sizeof(*graft) + 20 * i);
	graft->nr_parent = i;
	if (get_sha1_hex(buf, graft->sha1))
		goto bad_graft_data;
	for (i = 40; i < len; i += 41) {
		if (buf[i] != ' ')
			goto bad_graft_data;
		if (get_sha1_hex(buf + i + 1, graft->parent[i/41]))
			goto bad_graft_data;
	}
	return graft;

bad_graft_data:
	error("bad graft data: %s", buf);
	free(graft);
	return NULL;
}

static int read_graft_file(const char *graft_file)
{
	FILE *fp = fopen(graft_file, "r");
	char buf[1024];
	if (!fp)
		return -1;
	while (fgets(buf, sizeof(buf), fp)) {
		/* The format is just "Commit Parent1 Parent2 ...\n" */
		int len = strlen(buf);
		struct commit_graft *graft = read_graft_line(buf, len);
		if (!graft)
			continue;
		if (register_commit_graft(graft, 1))
			error("duplicate graft data: %s", buf);
	}
	fclose(fp);
	return 0;
}

static void prepare_commit_graft(void)
{
	static int commit_graft_prepared;
	char *graft_file;

	if (commit_graft_prepared)
		return;
	graft_file = get_graft_file();
	read_graft_file(graft_file);
	/* make sure shallows are read */
	is_repository_shallow();
	commit_graft_prepared = 1;
}

struct commit_graft *lookup_commit_graft(const unsigned char *sha1)
{
	int pos;
	prepare_commit_graft();
	pos = commit_graft_pos(sha1);
	if (pos < 0)
		return NULL;
	return commit_graft[pos];
}

int write_shallow_commits(struct strbuf *out, int use_pack_protocol)
{
	int i, count = 0;
	for (i = 0; i < commit_graft_nr; i++)
		if (commit_graft[i]->nr_parent < 0) {
			const char *hex =
				sha1_to_hex(commit_graft[i]->sha1);
			count++;
			if (use_pack_protocol)
				packet_buf_write(out, "shallow %s", hex);
			else {
				strbuf_addstr(out, hex);
				strbuf_addch(out, '\n');
			}
		}
	return count;
}

int unregister_shallow(const unsigned char *sha1)
{
	int pos = commit_graft_pos(sha1);
	if (pos < 0)
		return -1;
	if (pos + 1 < commit_graft_nr)
		memmove(commit_graft + pos, commit_graft + pos + 1,
				sizeof(struct commit_graft *)
				* (commit_graft_nr - pos - 1));
	commit_graft_nr--;
	return 0;
}

int parse_commit_buffer(struct commit *item, const void *buffer, unsigned long size)
{
	const char *tail = buffer;
	const char *bufptr = buffer;
	unsigned char parent[20];
	struct commit_list **pptr;
	struct commit_graft *graft;

	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	tail += size;
	if (tail <= bufptr + 46 || memcmp(bufptr, "tree ", 5) || bufptr[45] != '\n')
		return error("bogus commit object %s", sha1_to_hex(item->object.sha1));
	if (get_sha1_hex(bufptr + 5, parent) < 0)
		return error("bad tree pointer in commit %s",
			     sha1_to_hex(item->object.sha1));
	item->tree = lookup_tree(parent);
	bufptr += 46; /* "tree " + "hex sha1" + "\n" */
	pptr = &item->parents;

	graft = lookup_commit_graft(item->object.sha1);
	while (bufptr + 48 < tail && !memcmp(bufptr, "parent ", 7)) {
		struct commit *new_parent;

		if (tail <= bufptr + 48 ||
		    get_sha1_hex(bufptr + 7, parent) ||
		    bufptr[47] != '\n')
			return error("bad parents in commit %s", sha1_to_hex(item->object.sha1));
		bufptr += 48;
		/*
		 * The clone is shallow if nr_parent < 0, and we must
		 * not traverse its real parents even when we unhide them.
		 */
		if (graft && (graft->nr_parent < 0 || grafts_replace_parents))
			continue;
		new_parent = lookup_commit(parent);
		if (new_parent)
			pptr = &commit_list_insert(new_parent, pptr)->next;
	}
	if (graft) {
		int i;
		struct commit *new_parent;
		for (i = 0; i < graft->nr_parent; i++) {
			new_parent = lookup_commit(graft->parent[i]);
			if (!new_parent)
				continue;
			pptr = &commit_list_insert(new_parent, pptr)->next;
		}
	}
	item->date = parse_commit_date(bufptr, tail);

	return 0;
}

int parse_commit(struct commit *item)
{
	enum object_type type;
	void *buffer;
	unsigned long size;
	int ret;

	if (!item)
		return -1;
	if (item->object.parsed)
		return 0;
	buffer = read_sha1_file(item->object.sha1, &type, &size);
	if (!buffer)
		return error("Could not read %s",
			     sha1_to_hex(item->object.sha1));
	if (type != OBJ_COMMIT) {
		free(buffer);
		return error("Object %s not a commit",
			     sha1_to_hex(item->object.sha1));
	}
	ret = parse_commit_buffer(item, buffer, size);
	if (save_commit_buffer && !ret) {
		item->buffer = buffer;
		return 0;
	}
	free(buffer);
	return ret;
}

int find_commit_subject(const char *commit_buffer, const char **subject)
{
	const char *eol;
	const char *p = commit_buffer;

	while (*p && (*p != '\n' || p[1] != '\n'))
		p++;
	if (*p) {
		p += 2;
		for (eol = p; *eol && *eol != '\n'; eol++)
			; /* do nothing */
	} else
		eol = p;

	*subject = p;

	return eol - p;
}

struct commit_list *commit_list_insert(struct commit *item, struct commit_list **list_p)
{
	struct commit_list *new_list = xmalloc(sizeof(struct commit_list));
	new_list->item = item;
	new_list->next = *list_p;
	*list_p = new_list;
	return new_list;
}

unsigned commit_list_count(const struct commit_list *l)
{
	unsigned c = 0;
	for (; l; l = l->next )
		c++;
	return c;
}

void free_commit_list(struct commit_list *list)
{
	while (list) {
		struct commit_list *temp = list;
		list = temp->next;
		free(temp);
	}
}

struct commit_list * commit_list_insert_by_date(struct commit *item, struct commit_list **list)
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


void commit_list_sort_by_date(struct commit_list **list)
{
	struct commit_list *ret = NULL;
	while (*list) {
		commit_list_insert_by_date((*list)->item, &ret);
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
		if (!parse_commit(commit) && !(commit->object.flags & mark)) {
			commit->object.flags |= mark;
			commit_list_insert_by_date(commit, list);
		}
		parents = parents->next;
	}
	return ret;
}

void clear_commit_marks(struct commit *commit, unsigned int mark)
{
	while (commit) {
		struct commit_list *parents;

		if (!(mark & commit->object.flags))
			return;

		commit->object.flags &= ~mark;

		parents = commit->parents;
		if (!parents)
			return;

		while ((parents = parents->next))
			clear_commit_marks(parents->item, mark);

		commit = commit->parents->item;
	}
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

/*
 * Performs an in-place topological sort on the list supplied.
 */
void sort_in_topological_order(struct commit_list ** list, int lifo)
{
	struct commit_list *next, *orig = *list;
	struct commit_list *work, **insert;
	struct commit_list **pptr;

	if (!orig)
		return;
	*list = NULL;

	/* Mark them and clear the indegree */
	for (next = orig; next; next = next->next) {
		struct commit *commit = next->item;
		commit->indegree = 1;
	}

	/* update the indegree */
	for (next = orig; next; next = next->next) {
		struct commit_list * parents = next->item->parents;
		while (parents) {
			struct commit *parent = parents->item;

			if (parent->indegree)
				parent->indegree++;
			parents = parents->next;
		}
	}

	/*
	 * find the tips
	 *
	 * tips are nodes not reachable from any other node in the list
	 *
	 * the tips serve as a starting set for the work queue.
	 */
	work = NULL;
	insert = &work;
	for (next = orig; next; next = next->next) {
		struct commit *commit = next->item;

		if (commit->indegree == 1)
			insert = &commit_list_insert(commit, insert)->next;
	}

	/* process the list in topological order */
	if (!lifo)
		commit_list_sort_by_date(&work);

	pptr = list;
	*list = NULL;
	while (work) {
		struct commit *commit;
		struct commit_list *parents, *work_item;

		work_item = work;
		work = work_item->next;
		work_item->next = NULL;

		commit = work_item->item;
		for (parents = commit->parents; parents ; parents = parents->next) {
			struct commit *parent=parents->item;

			if (!parent->indegree)
				continue;

			/*
			 * parents are only enqueued for emission
			 * when all their children have been emitted thereby
			 * guaranteeing topological order.
			 */
			if (--parent->indegree == 1) {
				if (!lifo)
					commit_list_insert_by_date(parent, &work);
				else
					commit_list_insert(parent, &work);
			}
		}
		/*
		 * work_item is a commit all of whose children
		 * have already been emitted. we can emit it now.
		 */
		commit->indegree = 0;
		*pptr = work_item;
		pptr = &work_item->next;
	}
}

/* merge-base stuff */

/* bits #0..15 in revision.h */
#define PARENT1		(1u<<16)
#define PARENT2		(1u<<17)
#define STALE		(1u<<18)
#define RESULT		(1u<<19)

static const unsigned all_flags = (PARENT1 | PARENT2 | STALE | RESULT);

static struct commit *interesting(struct commit_list *list)
{
	while (list) {
		struct commit *commit = list->item;
		list = list->next;
		if (commit->object.flags & STALE)
			continue;
		return commit;
	}
	return NULL;
}

static struct commit_list *merge_bases_many(struct commit *one, int n, struct commit **twos)
{
	struct commit_list *list = NULL;
	struct commit_list *result = NULL;
	int i;

	for (i = 0; i < n; i++) {
		if (one == twos[i])
			/*
			 * We do not mark this even with RESULT so we do not
			 * have to clean it up.
			 */
			return commit_list_insert(one, &result);
	}

	if (parse_commit(one))
		return NULL;
	for (i = 0; i < n; i++) {
		if (parse_commit(twos[i]))
			return NULL;
	}

	one->object.flags |= PARENT1;
	commit_list_insert_by_date(one, &list);
	for (i = 0; i < n; i++) {
		twos[i]->object.flags |= PARENT2;
		commit_list_insert_by_date(twos[i], &list);
	}

	while (interesting(list)) {
		struct commit *commit;
		struct commit_list *parents;
		struct commit_list *next;
		int flags;

		commit = list->item;
		next = list->next;
		free(list);
		list = next;

		flags = commit->object.flags & (PARENT1 | PARENT2 | STALE);
		if (flags == (PARENT1 | PARENT2)) {
			if (!(commit->object.flags & RESULT)) {
				commit->object.flags |= RESULT;
				commit_list_insert_by_date(commit, &result);
			}
			/* Mark parents of a found merge stale */
			flags |= STALE;
		}
		parents = commit->parents;
		while (parents) {
			struct commit *p = parents->item;
			parents = parents->next;
			if ((p->object.flags & flags) == flags)
				continue;
			if (parse_commit(p))
				return NULL;
			p->object.flags |= flags;
			commit_list_insert_by_date(p, &list);
		}
	}

	/* Clean up the result to remove stale ones */
	free_commit_list(list);
	list = result; result = NULL;
	while (list) {
		struct commit_list *next = list->next;
		if (!(list->item->object.flags & STALE))
			commit_list_insert_by_date(list->item, &result);
		free(list);
		list = next;
	}
	return result;
}

struct commit_list *get_octopus_merge_bases(struct commit_list *in)
{
	struct commit_list *i, *j, *k, *ret = NULL;
	struct commit_list **pptr = &ret;

	for (i = in; i; i = i->next) {
		if (!ret)
			pptr = &commit_list_insert(i->item, pptr)->next;
		else {
			struct commit_list *new = NULL, *end = NULL;

			for (j = ret; j; j = j->next) {
				struct commit_list *bases;
				bases = get_merge_bases(i->item, j->item, 1);
				if (!new)
					new = bases;
				else
					end->next = bases;
				for (k = bases; k; k = k->next)
					end = k;
			}
			ret = new;
		}
	}
	return ret;
}

struct commit_list *get_merge_bases_many(struct commit *one,
					 int n,
					 struct commit **twos,
					 int cleanup)
{
	struct commit_list *list;
	struct commit **rslt;
	struct commit_list *result;
	int cnt, i, j;

	result = merge_bases_many(one, n, twos);
	for (i = 0; i < n; i++) {
		if (one == twos[i])
			return result;
	}
	if (!result || !result->next) {
		if (cleanup) {
			clear_commit_marks(one, all_flags);
			for (i = 0; i < n; i++)
				clear_commit_marks(twos[i], all_flags);
		}
		return result;
	}

	/* There are more than one */
	cnt = 0;
	list = result;
	while (list) {
		list = list->next;
		cnt++;
	}
	rslt = xcalloc(cnt, sizeof(*rslt));
	for (list = result, i = 0; list; list = list->next)
		rslt[i++] = list->item;
	free_commit_list(result);

	clear_commit_marks(one, all_flags);
	for (i = 0; i < n; i++)
		clear_commit_marks(twos[i], all_flags);
	for (i = 0; i < cnt - 1; i++) {
		for (j = i+1; j < cnt; j++) {
			if (!rslt[i] || !rslt[j])
				continue;
			result = merge_bases_many(rslt[i], 1, &rslt[j]);
			clear_commit_marks(rslt[i], all_flags);
			clear_commit_marks(rslt[j], all_flags);
			for (list = result; list; list = list->next) {
				if (rslt[i] == list->item)
					rslt[i] = NULL;
				if (rslt[j] == list->item)
					rslt[j] = NULL;
			}
		}
	}

	/* Surviving ones in rslt[] are the independent results */
	result = NULL;
	for (i = 0; i < cnt; i++) {
		if (rslt[i])
			commit_list_insert_by_date(rslt[i], &result);
	}
	free(rslt);
	return result;
}

struct commit_list *get_merge_bases(struct commit *one, struct commit *two,
				    int cleanup)
{
	return get_merge_bases_many(one, 1, &two, cleanup);
}

int is_descendant_of(struct commit *commit, struct commit_list *with_commit)
{
	if (!with_commit)
		return 1;
	while (with_commit) {
		struct commit *other;

		other = with_commit->item;
		with_commit = with_commit->next;
		if (in_merge_bases(other, &commit, 1))
			return 1;
	}
	return 0;
}

int in_merge_bases(struct commit *commit, struct commit **reference, int num)
{
	struct commit_list *bases, *b;
	int ret = 0;

	if (num == 1)
		bases = get_merge_bases(commit, *reference, 1);
	else
		die("not yet");
	for (b = bases; b; b = b->next) {
		if (!hashcmp(commit->object.sha1, b->item->object.sha1)) {
			ret = 1;
			break;
		}
	}

	free_commit_list(bases);
	return ret;
}

struct commit_list *reduce_heads(struct commit_list *heads)
{
	struct commit_list *p;
	struct commit_list *result = NULL, **tail = &result;
	struct commit **other;
	size_t num_head, num_other;

	if (!heads)
		return NULL;

	/* Avoid unnecessary reallocations */
	for (p = heads, num_head = 0; p; p = p->next)
		num_head++;
	other = xcalloc(sizeof(*other), num_head);

	/* For each commit, see if it can be reached by others */
	for (p = heads; p; p = p->next) {
		struct commit_list *q, *base;

		/* Do we already have this in the result? */
		for (q = result; q; q = q->next)
			if (p->item == q->item)
				break;
		if (q)
			continue;

		num_other = 0;
		for (q = heads; q; q = q->next) {
			if (p->item == q->item)
				continue;
			other[num_other++] = q->item;
		}
		if (num_other)
			base = get_merge_bases_many(p->item, num_other, other, 1);
		else
			base = NULL;
		/*
		 * If p->item does not have anything common with other
		 * commits, there won't be any merge base.  If it is
		 * reachable from some of the others, p->item will be
		 * the merge base.  If its history is connected with
		 * others, but p->item is not reachable by others, we
		 * will get something other than p->item back.
		 */
		if (!base || (base->item != p->item))
			tail = &(commit_list_insert(p->item, tail)->next);
		free_commit_list(base);
	}
	free(other);
	return result;
}

static const char commit_utf8_warn[] =
"Warning: commit message does not conform to UTF-8.\n"
"You may want to amend it after fixing the message, or set the config\n"
"variable i18n.commitencoding to the encoding your project uses.\n";

int commit_tree(const char *msg, unsigned char *tree,
		struct commit_list *parents, unsigned char *ret,
		const char *author)
{
	int result;
	int encoding_is_utf8;
	struct strbuf buffer;

	assert_sha1_type(tree, OBJ_TREE);

	/* Not having i18n.commitencoding is the same as having utf-8 */
	encoding_is_utf8 = is_encoding_utf8(git_commit_encoding);

	strbuf_init(&buffer, 8192); /* should avoid reallocs for the headers */
	strbuf_addf(&buffer, "tree %s\n", sha1_to_hex(tree));

	/*
	 * NOTE! This ordering means that the same exact tree merged with a
	 * different order of parents will be a _different_ changeset even
	 * if everything else stays the same.
	 */
	while (parents) {
		struct commit_list *next = parents->next;
		strbuf_addf(&buffer, "parent %s\n",
			sha1_to_hex(parents->item->object.sha1));
		free(parents);
		parents = next;
	}

	/* Person/date information */
	if (!author)
		author = git_author_info(IDENT_ERROR_ON_NO_NAME);
	strbuf_addf(&buffer, "author %s\n", author);
	strbuf_addf(&buffer, "committer %s\n", git_committer_info(IDENT_ERROR_ON_NO_NAME));
	if (!encoding_is_utf8)
		strbuf_addf(&buffer, "encoding %s\n", git_commit_encoding);
	strbuf_addch(&buffer, '\n');

	/* And add the comment */
	strbuf_addstr(&buffer, msg);

	/* And check the encoding */
	if (encoding_is_utf8 && !is_utf8(buffer.buf))
		fprintf(stderr, commit_utf8_warn);

	result = write_sha1_file(buffer.buf, buffer.len, commit_type, ret);
	strbuf_release(&buffer);
	return result;
}
