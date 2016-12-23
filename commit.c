#include "cache.h"
#include "tag.h"
#include "commit.h"
#include "pkt-line.h"
#include "utf8.h"
#include "diff.h"
#include "revision.h"
#include "notes.h"
#include "gpg-interface.h"
#include "mergesort.h"
#include "commit-slab.h"
#include "prio-queue.h"
#include "sha1-lookup.h"

static struct commit_extra_header *read_commit_extra_header_lines(const char *buf, size_t len, const char **);

int save_commit_buffer = 1;

const char *commit_type = "commit";

struct commit *lookup_commit_reference_gently(const unsigned char *sha1,
					      int quiet)
{
	struct object *obj = deref_tag(parse_object(sha1), NULL, 0);

	if (!obj)
		return NULL;
	return object_as_type(obj, OBJ_COMMIT, quiet);
}

struct commit *lookup_commit_reference(const unsigned char *sha1)
{
	return lookup_commit_reference_gently(sha1, 0);
}

struct commit *lookup_commit_or_die(const unsigned char *sha1, const char *ref_name)
{
	struct commit *c = lookup_commit_reference(sha1);
	if (!c)
		die(_("could not parse %s"), ref_name);
	if (hashcmp(sha1, c->object.oid.hash)) {
		warning(_("%s %s is not a commit!"),
			ref_name, sha1_to_hex(sha1));
	}
	return c;
}

struct commit *lookup_commit(const unsigned char *sha1)
{
	struct object *obj = lookup_object(sha1);
	if (!obj)
		return create_object(sha1, alloc_commit_node());
	return object_as_type(obj, OBJ_COMMIT, 0);
}

struct commit *lookup_commit_reference_by_name(const char *name)
{
	struct object_id oid;
	struct commit *commit;

	if (get_sha1_committish(name, oid.hash))
		return NULL;
	commit = lookup_commit_reference(oid.hash);
	if (parse_commit(commit))
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

static const unsigned char *commit_graft_sha1_access(size_t index, void *table)
{
	struct commit_graft **commit_graft_table = table;
	return commit_graft_table[index]->oid.hash;
}

static int commit_graft_pos(const unsigned char *sha1)
{
	return sha1_pos(sha1, commit_graft, commit_graft_nr,
			commit_graft_sha1_access);
}

int register_commit_graft(struct commit_graft *graft, int ignore_dups)
{
	int pos = commit_graft_pos(graft->oid.hash);

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
	ALLOC_GROW(commit_graft, commit_graft_nr + 1, commit_graft_alloc);
	commit_graft_nr++;
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
	const int entry_size = GIT_SHA1_HEXSZ + 1;

	while (len && isspace(buf[len-1]))
		buf[--len] = '\0';
	if (buf[0] == '#' || buf[0] == '\0')
		return NULL;
	if ((len + 1) % entry_size)
		goto bad_graft_data;
	i = (len + 1) / entry_size - 1;
	graft = xmalloc(st_add(sizeof(*graft), st_mult(GIT_SHA1_RAWSZ, i)));
	graft->nr_parent = i;
	if (get_oid_hex(buf, &graft->oid))
		goto bad_graft_data;
	for (i = GIT_SHA1_HEXSZ; i < len; i += entry_size) {
		if (buf[i] != ' ')
			goto bad_graft_data;
		if (get_sha1_hex(buf + i + 1, graft->parent[i/entry_size].hash))
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
	struct strbuf buf = STRBUF_INIT;
	if (!fp)
		return -1;
	while (!strbuf_getwholeline(&buf, fp, '\n')) {
		/* The format is just "Commit Parent1 Parent2 ...\n" */
		struct commit_graft *graft = read_graft_line(buf.buf, buf.len);
		if (!graft)
			continue;
		if (register_commit_graft(graft, 1))
			error("duplicate graft data: %s", buf.buf);
	}
	fclose(fp);
	strbuf_release(&buf);
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

int for_each_commit_graft(each_commit_graft_fn fn, void *cb_data)
{
	int i, ret;
	for (i = ret = 0; i < commit_graft_nr && !ret; i++)
		ret = fn(commit_graft[i], cb_data);
	return ret;
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

struct commit_buffer {
	void *buffer;
	unsigned long size;
};
define_commit_slab(buffer_slab, struct commit_buffer);
static struct buffer_slab buffer_slab = COMMIT_SLAB_INIT(1, buffer_slab);

void set_commit_buffer(struct commit *commit, void *buffer, unsigned long size)
{
	struct commit_buffer *v = buffer_slab_at(&buffer_slab, commit);
	v->buffer = buffer;
	v->size = size;
}

const void *get_cached_commit_buffer(const struct commit *commit, unsigned long *sizep)
{
	struct commit_buffer *v = buffer_slab_peek(&buffer_slab, commit);
	if (!v) {
		if (sizep)
			*sizep = 0;
		return NULL;
	}
	if (sizep)
		*sizep = v->size;
	return v->buffer;
}

const void *get_commit_buffer(const struct commit *commit, unsigned long *sizep)
{
	const void *ret = get_cached_commit_buffer(commit, sizep);
	if (!ret) {
		enum object_type type;
		unsigned long size;
		ret = read_sha1_file(commit->object.oid.hash, &type, &size);
		if (!ret)
			die("cannot read commit object %s",
			    oid_to_hex(&commit->object.oid));
		if (type != OBJ_COMMIT)
			die("expected commit for %s, got %s",
			    oid_to_hex(&commit->object.oid), typename(type));
		if (sizep)
			*sizep = size;
	}
	return ret;
}

void unuse_commit_buffer(const struct commit *commit, const void *buffer)
{
	struct commit_buffer *v = buffer_slab_peek(&buffer_slab, commit);
	if (!(v && v->buffer == buffer))
		free((void *)buffer);
}

void free_commit_buffer(struct commit *commit)
{
	struct commit_buffer *v = buffer_slab_peek(&buffer_slab, commit);
	if (v) {
		free(v->buffer);
		v->buffer = NULL;
		v->size = 0;
	}
}

const void *detach_commit_buffer(struct commit *commit, unsigned long *sizep)
{
	struct commit_buffer *v = buffer_slab_peek(&buffer_slab, commit);
	void *ret;

	if (!v) {
		if (sizep)
			*sizep = 0;
		return NULL;
	}
	ret = v->buffer;
	if (sizep)
		*sizep = v->size;

	v->buffer = NULL;
	v->size = 0;
	return ret;
}

int parse_commit_buffer(struct commit *item, const void *buffer, unsigned long size)
{
	const char *tail = buffer;
	const char *bufptr = buffer;
	struct object_id parent;
	struct commit_list **pptr;
	struct commit_graft *graft;
	const int tree_entry_len = GIT_SHA1_HEXSZ + 5;
	const int parent_entry_len = GIT_SHA1_HEXSZ + 7;

	if (item->object.parsed)
		return 0;
	item->object.parsed = 1;
	tail += size;
	if (tail <= bufptr + tree_entry_len + 1 || memcmp(bufptr, "tree ", 5) ||
			bufptr[tree_entry_len] != '\n')
		return error("bogus commit object %s", oid_to_hex(&item->object.oid));
	if (get_sha1_hex(bufptr + 5, parent.hash) < 0)
		return error("bad tree pointer in commit %s",
			     oid_to_hex(&item->object.oid));
	item->tree = lookup_tree(parent.hash);
	bufptr += tree_entry_len + 1; /* "tree " + "hex sha1" + "\n" */
	pptr = &item->parents;

	graft = lookup_commit_graft(item->object.oid.hash);
	while (bufptr + parent_entry_len < tail && !memcmp(bufptr, "parent ", 7)) {
		struct commit *new_parent;

		if (tail <= bufptr + parent_entry_len + 1 ||
		    get_sha1_hex(bufptr + 7, parent.hash) ||
		    bufptr[parent_entry_len] != '\n')
			return error("bad parents in commit %s", oid_to_hex(&item->object.oid));
		bufptr += parent_entry_len + 1;
		/*
		 * The clone is shallow if nr_parent < 0, and we must
		 * not traverse its real parents even when we unhide them.
		 */
		if (graft && (graft->nr_parent < 0 || grafts_replace_parents))
			continue;
		new_parent = lookup_commit(parent.hash);
		if (new_parent)
			pptr = &commit_list_insert(new_parent, pptr)->next;
	}
	if (graft) {
		int i;
		struct commit *new_parent;
		for (i = 0; i < graft->nr_parent; i++) {
			new_parent = lookup_commit(graft->parent[i].hash);
			if (!new_parent)
				continue;
			pptr = &commit_list_insert(new_parent, pptr)->next;
		}
	}
	item->date = parse_commit_date(bufptr, tail);

	return 0;
}

int parse_commit_gently(struct commit *item, int quiet_on_missing)
{
	enum object_type type;
	void *buffer;
	unsigned long size;
	int ret;

	if (!item)
		return -1;
	if (item->object.parsed)
		return 0;
	buffer = read_sha1_file(item->object.oid.hash, &type, &size);
	if (!buffer)
		return quiet_on_missing ? -1 :
			error("Could not read %s",
			     oid_to_hex(&item->object.oid));
	if (type != OBJ_COMMIT) {
		free(buffer);
		return error("Object %s not a commit",
			     oid_to_hex(&item->object.oid));
	}
	ret = parse_commit_buffer(item, buffer, size);
	if (save_commit_buffer && !ret) {
		set_commit_buffer(item, buffer, size);
		return 0;
	}
	free(buffer);
	return ret;
}

void parse_commit_or_die(struct commit *item)
{
	if (parse_commit(item))
		die("unable to parse commit %s",
		    item ? oid_to_hex(&item->object.oid) : "(null)");
}

int find_commit_subject(const char *commit_buffer, const char **subject)
{
	const char *eol;
	const char *p = commit_buffer;

	while (*p && (*p != '\n' || p[1] != '\n'))
		p++;
	if (*p) {
		p = skip_blank_lines(p + 2);
		eol = strchrnul(p, '\n');
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

struct commit_list *copy_commit_list(struct commit_list *list)
{
	struct commit_list *head = NULL;
	struct commit_list **pp = &head;
	while (list) {
		pp = commit_list_append(list->item, pp);
		list = list->next;
	}
	return head;
}

void free_commit_list(struct commit_list *list)
{
	while (list)
		pop_commit(&list);
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

static int commit_list_compare_by_date(const void *a, const void *b)
{
	unsigned long a_date = ((const struct commit_list *)a)->item->date;
	unsigned long b_date = ((const struct commit_list *)b)->item->date;
	if (a_date < b_date)
		return 1;
	if (a_date > b_date)
		return -1;
	return 0;
}

static void *commit_list_get_next(const void *a)
{
	return ((const struct commit_list *)a)->next;
}

static void commit_list_set_next(void *a, void *next)
{
	((struct commit_list *)a)->next = next;
}

void commit_list_sort_by_date(struct commit_list **list)
{
	*list = llist_mergesort(*list, commit_list_get_next, commit_list_set_next,
				commit_list_compare_by_date);
}

struct commit *pop_most_recent_commit(struct commit_list **list,
				      unsigned int mark)
{
	struct commit *ret = pop_commit(list);
	struct commit_list *parents = ret->parents;

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

static void clear_commit_marks_1(struct commit_list **plist,
				 struct commit *commit, unsigned int mark)
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
			commit_list_insert(parents->item, plist);

		commit = commit->parents->item;
	}
}

void clear_commit_marks_many(int nr, struct commit **commit, unsigned int mark)
{
	struct commit_list *list = NULL;

	while (nr--) {
		commit_list_insert(*commit, &list);
		commit++;
	}
	while (list)
		clear_commit_marks_1(&list, pop_commit(&list), mark);
}

void clear_commit_marks(struct commit *commit, unsigned int mark)
{
	clear_commit_marks_many(1, &commit, mark);
}

void clear_commit_marks_for_object_array(struct object_array *a, unsigned mark)
{
	struct object *object;
	struct commit *commit;
	unsigned int i;

	for (i = 0; i < a->nr; i++) {
		object = a->objects[i].item;
		commit = lookup_commit_reference_gently(object->oid.hash, 1);
		if (commit)
			clear_commit_marks(commit, mark);
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
 * Topological sort support
 */

/* count number of children that have not been emitted */
define_commit_slab(indegree_slab, int);

/* record author-date for each commit object */
define_commit_slab(author_date_slab, unsigned long);

static void record_author_date(struct author_date_slab *author_date,
			       struct commit *commit)
{
	const char *buffer = get_commit_buffer(commit, NULL);
	struct ident_split ident;
	const char *ident_line;
	size_t ident_len;
	char *date_end;
	unsigned long date;

	ident_line = find_commit_header(buffer, "author", &ident_len);
	if (!ident_line)
		goto fail_exit; /* no author line */
	if (split_ident_line(&ident, ident_line, ident_len) ||
	    !ident.date_begin || !ident.date_end)
		goto fail_exit; /* malformed "author" line */

	date = strtoul(ident.date_begin, &date_end, 10);
	if (date_end != ident.date_end)
		goto fail_exit; /* malformed date */
	*(author_date_slab_at(author_date, commit)) = date;

fail_exit:
	unuse_commit_buffer(commit, buffer);
}

static int compare_commits_by_author_date(const void *a_, const void *b_,
					  void *cb_data)
{
	const struct commit *a = a_, *b = b_;
	struct author_date_slab *author_date = cb_data;
	unsigned long a_date = *(author_date_slab_at(author_date, a));
	unsigned long b_date = *(author_date_slab_at(author_date, b));

	/* newer commits with larger date first */
	if (a_date < b_date)
		return 1;
	else if (a_date > b_date)
		return -1;
	return 0;
}

int compare_commits_by_commit_date(const void *a_, const void *b_, void *unused)
{
	const struct commit *a = a_, *b = b_;
	/* newer commits with larger date first */
	if (a->date < b->date)
		return 1;
	else if (a->date > b->date)
		return -1;
	return 0;
}

/*
 * Performs an in-place topological sort on the list supplied.
 */
void sort_in_topological_order(struct commit_list **list, enum rev_sort_order sort_order)
{
	struct commit_list *next, *orig = *list;
	struct commit_list **pptr;
	struct indegree_slab indegree;
	struct prio_queue queue;
	struct commit *commit;
	struct author_date_slab author_date;

	if (!orig)
		return;
	*list = NULL;

	init_indegree_slab(&indegree);
	memset(&queue, '\0', sizeof(queue));

	switch (sort_order) {
	default: /* REV_SORT_IN_GRAPH_ORDER */
		queue.compare = NULL;
		break;
	case REV_SORT_BY_COMMIT_DATE:
		queue.compare = compare_commits_by_commit_date;
		break;
	case REV_SORT_BY_AUTHOR_DATE:
		init_author_date_slab(&author_date);
		queue.compare = compare_commits_by_author_date;
		queue.cb_data = &author_date;
		break;
	}

	/* Mark them and clear the indegree */
	for (next = orig; next; next = next->next) {
		struct commit *commit = next->item;
		*(indegree_slab_at(&indegree, commit)) = 1;
		/* also record the author dates, if needed */
		if (sort_order == REV_SORT_BY_AUTHOR_DATE)
			record_author_date(&author_date, commit);
	}

	/* update the indegree */
	for (next = orig; next; next = next->next) {
		struct commit_list *parents = next->item->parents;
		while (parents) {
			struct commit *parent = parents->item;
			int *pi = indegree_slab_at(&indegree, parent);

			if (*pi)
				(*pi)++;
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
	for (next = orig; next; next = next->next) {
		struct commit *commit = next->item;

		if (*(indegree_slab_at(&indegree, commit)) == 1)
			prio_queue_put(&queue, commit);
	}

	/*
	 * This is unfortunate; the initial tips need to be shown
	 * in the order given from the revision traversal machinery.
	 */
	if (sort_order == REV_SORT_IN_GRAPH_ORDER)
		prio_queue_reverse(&queue);

	/* We no longer need the commit list */
	free_commit_list(orig);

	pptr = list;
	*list = NULL;
	while ((commit = prio_queue_get(&queue)) != NULL) {
		struct commit_list *parents;

		for (parents = commit->parents; parents ; parents = parents->next) {
			struct commit *parent = parents->item;
			int *pi = indegree_slab_at(&indegree, parent);

			if (!*pi)
				continue;

			/*
			 * parents are only enqueued for emission
			 * when all their children have been emitted thereby
			 * guaranteeing topological order.
			 */
			if (--(*pi) == 1)
				prio_queue_put(&queue, parent);
		}
		/*
		 * all children of commit have already been
		 * emitted. we can emit it now.
		 */
		*(indegree_slab_at(&indegree, commit)) = 0;

		pptr = &commit_list_insert(commit, pptr)->next;
	}

	clear_indegree_slab(&indegree);
	clear_prio_queue(&queue);
	if (sort_order == REV_SORT_BY_AUTHOR_DATE)
		clear_author_date_slab(&author_date);
}

/* merge-base stuff */

/* Remember to update object flag allocation in object.h */
#define PARENT1		(1u<<16)
#define PARENT2		(1u<<17)
#define STALE		(1u<<18)
#define RESULT		(1u<<19)

static const unsigned all_flags = (PARENT1 | PARENT2 | STALE | RESULT);

static int queue_has_nonstale(struct prio_queue *queue)
{
	int i;
	for (i = 0; i < queue->nr; i++) {
		struct commit *commit = queue->array[i].data;
		if (!(commit->object.flags & STALE))
			return 1;
	}
	return 0;
}

/* all input commits in one and twos[] must have been parsed! */
static struct commit_list *paint_down_to_common(struct commit *one, int n, struct commit **twos)
{
	struct prio_queue queue = { compare_commits_by_commit_date };
	struct commit_list *result = NULL;
	int i;

	one->object.flags |= PARENT1;
	if (!n) {
		commit_list_append(one, &result);
		return result;
	}
	prio_queue_put(&queue, one);

	for (i = 0; i < n; i++) {
		twos[i]->object.flags |= PARENT2;
		prio_queue_put(&queue, twos[i]);
	}

	while (queue_has_nonstale(&queue)) {
		struct commit *commit = prio_queue_get(&queue);
		struct commit_list *parents;
		int flags;

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
			prio_queue_put(&queue, p);
		}
	}

	clear_prio_queue(&queue);
	return result;
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

	list = paint_down_to_common(one, n, twos);

	while (list) {
		struct commit *commit = pop_commit(&list);
		if (!(commit->object.flags & STALE))
			commit_list_insert_by_date(commit, &result);
	}
	return result;
}

struct commit_list *get_octopus_merge_bases(struct commit_list *in)
{
	struct commit_list *i, *j, *k, *ret = NULL;

	if (!in)
		return ret;

	commit_list_insert(in->item, &ret);

	for (i = in->next; i; i = i->next) {
		struct commit_list *new = NULL, *end = NULL;

		for (j = ret; j; j = j->next) {
			struct commit_list *bases;
			bases = get_merge_bases(i->item, j->item);
			if (!new)
				new = bases;
			else
				end->next = bases;
			for (k = bases; k; k = k->next)
				end = k;
		}
		ret = new;
	}
	return ret;
}

static int remove_redundant(struct commit **array, int cnt)
{
	/*
	 * Some commit in the array may be an ancestor of
	 * another commit.  Move such commit to the end of
	 * the array, and return the number of commits that
	 * are independent from each other.
	 */
	struct commit **work;
	unsigned char *redundant;
	int *filled_index;
	int i, j, filled;

	work = xcalloc(cnt, sizeof(*work));
	redundant = xcalloc(cnt, 1);
	ALLOC_ARRAY(filled_index, cnt - 1);

	for (i = 0; i < cnt; i++)
		parse_commit(array[i]);
	for (i = 0; i < cnt; i++) {
		struct commit_list *common;

		if (redundant[i])
			continue;
		for (j = filled = 0; j < cnt; j++) {
			if (i == j || redundant[j])
				continue;
			filled_index[filled] = j;
			work[filled++] = array[j];
		}
		common = paint_down_to_common(array[i], filled, work);
		if (array[i]->object.flags & PARENT2)
			redundant[i] = 1;
		for (j = 0; j < filled; j++)
			if (work[j]->object.flags & PARENT1)
				redundant[filled_index[j]] = 1;
		clear_commit_marks(array[i], all_flags);
		for (j = 0; j < filled; j++)
			clear_commit_marks(work[j], all_flags);
		free_commit_list(common);
	}

	/* Now collect the result */
	COPY_ARRAY(work, array, cnt);
	for (i = filled = 0; i < cnt; i++)
		if (!redundant[i])
			array[filled++] = work[i];
	for (j = filled, i = 0; i < cnt; i++)
		if (redundant[i])
			array[j++] = work[i];
	free(work);
	free(redundant);
	free(filled_index);
	return filled;
}

static struct commit_list *get_merge_bases_many_0(struct commit *one,
						  int n,
						  struct commit **twos,
						  int cleanup)
{
	struct commit_list *list;
	struct commit **rslt;
	struct commit_list *result;
	int cnt, i;

	result = merge_bases_many(one, n, twos);
	for (i = 0; i < n; i++) {
		if (one == twos[i])
			return result;
	}
	if (!result || !result->next) {
		if (cleanup) {
			clear_commit_marks(one, all_flags);
			clear_commit_marks_many(n, twos, all_flags);
		}
		return result;
	}

	/* There are more than one */
	cnt = commit_list_count(result);
	rslt = xcalloc(cnt, sizeof(*rslt));
	for (list = result, i = 0; list; list = list->next)
		rslt[i++] = list->item;
	free_commit_list(result);

	clear_commit_marks(one, all_flags);
	clear_commit_marks_many(n, twos, all_flags);

	cnt = remove_redundant(rslt, cnt);
	result = NULL;
	for (i = 0; i < cnt; i++)
		commit_list_insert_by_date(rslt[i], &result);
	free(rslt);
	return result;
}

struct commit_list *get_merge_bases_many(struct commit *one,
					 int n,
					 struct commit **twos)
{
	return get_merge_bases_many_0(one, n, twos, 1);
}

struct commit_list *get_merge_bases_many_dirty(struct commit *one,
					       int n,
					       struct commit **twos)
{
	return get_merge_bases_many_0(one, n, twos, 0);
}

struct commit_list *get_merge_bases(struct commit *one, struct commit *two)
{
	return get_merge_bases_many_0(one, 1, &two, 1);
}

/*
 * Is "commit" a descendant of one of the elements on the "with_commit" list?
 */
int is_descendant_of(struct commit *commit, struct commit_list *with_commit)
{
	if (!with_commit)
		return 1;
	while (with_commit) {
		struct commit *other;

		other = with_commit->item;
		with_commit = with_commit->next;
		if (in_merge_bases(other, commit))
			return 1;
	}
	return 0;
}

/*
 * Is "commit" an ancestor of one of the "references"?
 */
int in_merge_bases_many(struct commit *commit, int nr_reference, struct commit **reference)
{
	struct commit_list *bases;
	int ret = 0, i;

	if (parse_commit(commit))
		return ret;
	for (i = 0; i < nr_reference; i++)
		if (parse_commit(reference[i]))
			return ret;

	bases = paint_down_to_common(commit, nr_reference, reference);
	if (commit->object.flags & PARENT2)
		ret = 1;
	clear_commit_marks(commit, all_flags);
	clear_commit_marks_many(nr_reference, reference, all_flags);
	free_commit_list(bases);
	return ret;
}

/*
 * Is "commit" an ancestor of (i.e. reachable from) the "reference"?
 */
int in_merge_bases(struct commit *commit, struct commit *reference)
{
	return in_merge_bases_many(commit, 1, &reference);
}

struct commit_list *reduce_heads(struct commit_list *heads)
{
	struct commit_list *p;
	struct commit_list *result = NULL, **tail = &result;
	struct commit **array;
	int num_head, i;

	if (!heads)
		return NULL;

	/* Uniquify */
	for (p = heads; p; p = p->next)
		p->item->object.flags &= ~STALE;
	for (p = heads, num_head = 0; p; p = p->next) {
		if (p->item->object.flags & STALE)
			continue;
		p->item->object.flags |= STALE;
		num_head++;
	}
	array = xcalloc(num_head, sizeof(*array));
	for (p = heads, i = 0; p; p = p->next) {
		if (p->item->object.flags & STALE) {
			array[i++] = p->item;
			p->item->object.flags &= ~STALE;
		}
	}
	num_head = remove_redundant(array, num_head);
	for (i = 0; i < num_head; i++)
		tail = &commit_list_insert(array[i], tail)->next;
	return result;
}

static const char gpg_sig_header[] = "gpgsig";
static const int gpg_sig_header_len = sizeof(gpg_sig_header) - 1;

static int do_sign_commit(struct strbuf *buf, const char *keyid)
{
	struct strbuf sig = STRBUF_INIT;
	int inspos, copypos;
	const char *eoh;

	/* find the end of the header */
	eoh = strstr(buf->buf, "\n\n");
	if (!eoh)
		inspos = buf->len;
	else
		inspos = eoh - buf->buf + 1;

	if (!keyid || !*keyid)
		keyid = get_signing_key();
	if (sign_buffer(buf, &sig, keyid)) {
		strbuf_release(&sig);
		return -1;
	}

	for (copypos = 0; sig.buf[copypos]; ) {
		const char *bol = sig.buf + copypos;
		const char *eol = strchrnul(bol, '\n');
		int len = (eol - bol) + !!*eol;

		if (!copypos) {
			strbuf_insert(buf, inspos, gpg_sig_header, gpg_sig_header_len);
			inspos += gpg_sig_header_len;
		}
		strbuf_insert(buf, inspos++, " ", 1);
		strbuf_insert(buf, inspos, bol, len);
		inspos += len;
		copypos += len;
	}
	strbuf_release(&sig);
	return 0;
}

int parse_signed_commit(const struct commit *commit,
			struct strbuf *payload, struct strbuf *signature)
{

	unsigned long size;
	const char *buffer = get_commit_buffer(commit, &size);
	int in_signature, saw_signature = -1;
	const char *line, *tail;

	line = buffer;
	tail = buffer + size;
	in_signature = 0;
	saw_signature = 0;
	while (line < tail) {
		const char *sig = NULL;
		const char *next = memchr(line, '\n', tail - line);

		next = next ? next + 1 : tail;
		if (in_signature && line[0] == ' ')
			sig = line + 1;
		else if (starts_with(line, gpg_sig_header) &&
			 line[gpg_sig_header_len] == ' ')
			sig = line + gpg_sig_header_len + 1;
		if (sig) {
			strbuf_add(signature, sig, next - sig);
			saw_signature = 1;
			in_signature = 1;
		} else {
			if (*line == '\n')
				/* dump the whole remainder of the buffer */
				next = tail;
			strbuf_add(payload, line, next - line);
			in_signature = 0;
		}
		line = next;
	}
	unuse_commit_buffer(commit, buffer);
	return saw_signature;
}

int remove_signature(struct strbuf *buf)
{
	const char *line = buf->buf;
	const char *tail = buf->buf + buf->len;
	int in_signature = 0;
	const char *sig_start = NULL;
	const char *sig_end = NULL;

	while (line < tail) {
		const char *next = memchr(line, '\n', tail - line);
		next = next ? next + 1 : tail;

		if (in_signature && line[0] == ' ')
			sig_end = next;
		else if (starts_with(line, gpg_sig_header) &&
			 line[gpg_sig_header_len] == ' ') {
			sig_start = line;
			sig_end = next;
			in_signature = 1;
		} else {
			if (*line == '\n')
				/* dump the whole remainder of the buffer */
				next = tail;
			in_signature = 0;
		}
		line = next;
	}

	if (sig_start)
		strbuf_remove(buf, sig_start - buf->buf, sig_end - sig_start);

	return sig_start != NULL;
}

static void handle_signed_tag(struct commit *parent, struct commit_extra_header ***tail)
{
	struct merge_remote_desc *desc;
	struct commit_extra_header *mergetag;
	char *buf;
	unsigned long size, len;
	enum object_type type;

	desc = merge_remote_util(parent);
	if (!desc || !desc->obj)
		return;
	buf = read_sha1_file(desc->obj->oid.hash, &type, &size);
	if (!buf || type != OBJ_TAG)
		goto free_return;
	len = parse_signature(buf, size);
	if (size == len)
		goto free_return;
	/*
	 * We could verify this signature and either omit the tag when
	 * it does not validate, but the integrator may not have the
	 * public key of the signer of the tag he is merging, while a
	 * later auditor may have it while auditing, so let's not run
	 * verify-signed-buffer here for now...
	 *
	 * if (verify_signed_buffer(buf, len, buf + len, size - len, ...))
	 *	warn("warning: signed tag unverified.");
	 */
	mergetag = xcalloc(1, sizeof(*mergetag));
	mergetag->key = xstrdup("mergetag");
	mergetag->value = buf;
	mergetag->len = size;

	**tail = mergetag;
	*tail = &mergetag->next;
	return;

free_return:
	free(buf);
}

int check_commit_signature(const struct commit *commit, struct signature_check *sigc)
{
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;
	int ret = 1;

	sigc->result = 'N';

	if (parse_signed_commit(commit, &payload, &signature) <= 0)
		goto out;
	ret = check_signature(payload.buf, payload.len, signature.buf,
		signature.len, sigc);

 out:
	strbuf_release(&payload);
	strbuf_release(&signature);

	return ret;
}



void append_merge_tag_headers(struct commit_list *parents,
			      struct commit_extra_header ***tail)
{
	while (parents) {
		struct commit *parent = parents->item;
		handle_signed_tag(parent, tail);
		parents = parents->next;
	}
}

static void add_extra_header(struct strbuf *buffer,
			     struct commit_extra_header *extra)
{
	strbuf_addstr(buffer, extra->key);
	if (extra->len)
		strbuf_add_lines(buffer, " ", extra->value, extra->len);
	else
		strbuf_addch(buffer, '\n');
}

struct commit_extra_header *read_commit_extra_headers(struct commit *commit,
						      const char **exclude)
{
	struct commit_extra_header *extra = NULL;
	unsigned long size;
	const char *buffer = get_commit_buffer(commit, &size);
	extra = read_commit_extra_header_lines(buffer, size, exclude);
	unuse_commit_buffer(commit, buffer);
	return extra;
}

void for_each_mergetag(each_mergetag_fn fn, struct commit *commit, void *data)
{
	struct commit_extra_header *extra, *to_free;

	to_free = read_commit_extra_headers(commit, NULL);
	for (extra = to_free; extra; extra = extra->next) {
		if (strcmp(extra->key, "mergetag"))
			continue; /* not a merge tag */
		fn(commit, extra, data);
	}
	free_commit_extra_headers(to_free);
}

static inline int standard_header_field(const char *field, size_t len)
{
	return ((len == 4 && !memcmp(field, "tree ", 5)) ||
		(len == 6 && !memcmp(field, "parent ", 7)) ||
		(len == 6 && !memcmp(field, "author ", 7)) ||
		(len == 9 && !memcmp(field, "committer ", 10)) ||
		(len == 8 && !memcmp(field, "encoding ", 9)));
}

static int excluded_header_field(const char *field, size_t len, const char **exclude)
{
	if (!exclude)
		return 0;

	while (*exclude) {
		size_t xlen = strlen(*exclude);
		if (len == xlen &&
		    !memcmp(field, *exclude, xlen) && field[xlen] == ' ')
			return 1;
		exclude++;
	}
	return 0;
}

static struct commit_extra_header *read_commit_extra_header_lines(
	const char *buffer, size_t size,
	const char **exclude)
{
	struct commit_extra_header *extra = NULL, **tail = &extra, *it = NULL;
	const char *line, *next, *eof, *eob;
	struct strbuf buf = STRBUF_INIT;

	for (line = buffer, eob = line + size;
	     line < eob && *line != '\n';
	     line = next) {
		next = memchr(line, '\n', eob - line);
		next = next ? next + 1 : eob;
		if (*line == ' ') {
			/* continuation */
			if (it)
				strbuf_add(&buf, line + 1, next - (line + 1));
			continue;
		}
		if (it)
			it->value = strbuf_detach(&buf, &it->len);
		strbuf_reset(&buf);
		it = NULL;

		eof = strchr(line, ' ');
		if (next <= eof)
			eof = next;

		if (standard_header_field(line, eof - line) ||
		    excluded_header_field(line, eof - line, exclude))
			continue;

		it = xcalloc(1, sizeof(*it));
		it->key = xmemdupz(line, eof-line);
		*tail = it;
		tail = &it->next;
		if (eof + 1 < next)
			strbuf_add(&buf, eof + 1, next - (eof + 1));
	}
	if (it)
		it->value = strbuf_detach(&buf, &it->len);
	return extra;
}

void free_commit_extra_headers(struct commit_extra_header *extra)
{
	while (extra) {
		struct commit_extra_header *next = extra->next;
		free(extra->key);
		free(extra->value);
		free(extra);
		extra = next;
	}
}

int commit_tree(const char *msg, size_t msg_len,
		const unsigned char *tree,
		struct commit_list *parents, unsigned char *ret,
		const char *author, const char *sign_commit)
{
	struct commit_extra_header *extra = NULL, **tail = &extra;
	int result;

	append_merge_tag_headers(parents, &tail);
	result = commit_tree_extended(msg, msg_len, tree, parents, ret,
				      author, sign_commit, extra);
	free_commit_extra_headers(extra);
	return result;
}

static int find_invalid_utf8(const char *buf, int len)
{
	int offset = 0;
	static const unsigned int max_codepoint[] = {
		0x7f, 0x7ff, 0xffff, 0x10ffff
	};

	while (len) {
		unsigned char c = *buf++;
		int bytes, bad_offset;
		unsigned int codepoint;
		unsigned int min_val, max_val;

		len--;
		offset++;

		/* Simple US-ASCII? No worries. */
		if (c < 0x80)
			continue;

		bad_offset = offset-1;

		/*
		 * Count how many more high bits set: that's how
		 * many more bytes this sequence should have.
		 */
		bytes = 0;
		while (c & 0x40) {
			c <<= 1;
			bytes++;
		}

		/*
		 * Must be between 1 and 3 more bytes.  Longer sequences result in
		 * codepoints beyond U+10FFFF, which are guaranteed never to exist.
		 */
		if (bytes < 1 || 3 < bytes)
			return bad_offset;

		/* Do we *have* that many bytes? */
		if (len < bytes)
			return bad_offset;

		/*
		 * Place the encoded bits at the bottom of the value and compute the
		 * valid range.
		 */
		codepoint = (c & 0x7f) >> bytes;
		min_val = max_codepoint[bytes-1] + 1;
		max_val = max_codepoint[bytes];

		offset += bytes;
		len -= bytes;

		/* And verify that they are good continuation bytes */
		do {
			codepoint <<= 6;
			codepoint |= *buf & 0x3f;
			if ((*buf++ & 0xc0) != 0x80)
				return bad_offset;
		} while (--bytes);

		/* Reject codepoints that are out of range for the sequence length. */
		if (codepoint < min_val || codepoint > max_val)
			return bad_offset;
		/* Surrogates are only for UTF-16 and cannot be encoded in UTF-8. */
		if ((codepoint & 0x1ff800) == 0xd800)
			return bad_offset;
		/* U+xxFFFE and U+xxFFFF are guaranteed non-characters. */
		if ((codepoint & 0xfffe) == 0xfffe)
			return bad_offset;
		/* So are anything in the range U+FDD0..U+FDEF. */
		if (codepoint >= 0xfdd0 && codepoint <= 0xfdef)
			return bad_offset;
	}
	return -1;
}

/*
 * This verifies that the buffer is in proper utf8 format.
 *
 * If it isn't, it assumes any non-utf8 characters are Latin1,
 * and does the conversion.
 */
static int verify_utf8(struct strbuf *buf)
{
	int ok = 1;
	long pos = 0;

	for (;;) {
		int bad;
		unsigned char c;
		unsigned char replace[2];

		bad = find_invalid_utf8(buf->buf + pos, buf->len - pos);
		if (bad < 0)
			return ok;
		pos += bad;
		ok = 0;
		c = buf->buf[pos];
		strbuf_remove(buf, pos, 1);

		/* We know 'c' must be in the range 128-255 */
		replace[0] = 0xc0 + (c >> 6);
		replace[1] = 0x80 + (c & 0x3f);
		strbuf_insert(buf, pos, replace, 2);
		pos += 2;
	}
}

static const char commit_utf8_warn[] =
N_("Warning: commit message did not conform to UTF-8.\n"
   "You may want to amend it after fixing the message, or set the config\n"
   "variable i18n.commitencoding to the encoding your project uses.\n");

int commit_tree_extended(const char *msg, size_t msg_len,
			 const unsigned char *tree,
			 struct commit_list *parents, unsigned char *ret,
			 const char *author, const char *sign_commit,
			 struct commit_extra_header *extra)
{
	int result;
	int encoding_is_utf8;
	struct strbuf buffer;

	assert_sha1_type(tree, OBJ_TREE);

	if (memchr(msg, '\0', msg_len))
		return error("a NUL byte in commit log message not allowed.");

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
		struct commit *parent = pop_commit(&parents);
		strbuf_addf(&buffer, "parent %s\n",
			    oid_to_hex(&parent->object.oid));
	}

	/* Person/date information */
	if (!author)
		author = git_author_info(IDENT_STRICT);
	strbuf_addf(&buffer, "author %s\n", author);
	strbuf_addf(&buffer, "committer %s\n", git_committer_info(IDENT_STRICT));
	if (!encoding_is_utf8)
		strbuf_addf(&buffer, "encoding %s\n", git_commit_encoding);

	while (extra) {
		add_extra_header(&buffer, extra);
		extra = extra->next;
	}
	strbuf_addch(&buffer, '\n');

	/* And add the comment */
	strbuf_add(&buffer, msg, msg_len);

	/* And check the encoding */
	if (encoding_is_utf8 && !verify_utf8(&buffer))
		fprintf(stderr, _(commit_utf8_warn));

	if (sign_commit && do_sign_commit(&buffer, sign_commit))
		return -1;

	result = write_sha1_file(buffer.buf, buffer.len, commit_type, ret);
	strbuf_release(&buffer);
	return result;
}

void set_merge_remote_desc(struct commit *commit,
			   const char *name, struct object *obj)
{
	struct merge_remote_desc *desc;
	FLEX_ALLOC_STR(desc, name, name);
	desc->obj = obj;
	commit->util = desc;
}

struct commit *get_merge_parent(const char *name)
{
	struct object *obj;
	struct commit *commit;
	struct object_id oid;
	if (get_sha1(name, oid.hash))
		return NULL;
	obj = parse_object(oid.hash);
	commit = (struct commit *)peel_to_type(name, 0, obj, OBJ_COMMIT);
	if (commit && !commit->util)
		set_merge_remote_desc(commit, name, obj);
	return commit;
}

/*
 * Append a commit to the end of the commit_list.
 *
 * next starts by pointing to the variable that holds the head of an
 * empty commit_list, and is updated to point to the "next" field of
 * the last item on the list as new commits are appended.
 *
 * Usage example:
 *
 *     struct commit_list *list;
 *     struct commit_list **next = &list;
 *
 *     next = commit_list_append(c1, next);
 *     next = commit_list_append(c2, next);
 *     assert(commit_list_count(list) == 2);
 *     return list;
 */
struct commit_list **commit_list_append(struct commit *commit,
					struct commit_list **next)
{
	struct commit_list *new = xmalloc(sizeof(struct commit_list));
	new->item = commit;
	*next = new;
	new->next = NULL;
	return &new->next;
}

const char *find_commit_header(const char *msg, const char *key, size_t *out_len)
{
	int key_len = strlen(key);
	const char *line = msg;

	while (line) {
		const char *eol = strchrnul(line, '\n');

		if (line == eol)
			return NULL;

		if (eol - line > key_len &&
		    !strncmp(line, key, key_len) &&
		    line[key_len] == ' ') {
			*out_len = eol - line - key_len - 1;
			return line + key_len + 1;
		}
		line = *eol ? eol + 1 : NULL;
	}
	return NULL;
}

/*
 * Inspect the given string and determine the true "end" of the log message, in
 * order to find where to put a new Signed-off-by: line.  Ignored are
 * trailing comment lines and blank lines, and also the traditional
 * "Conflicts:" block that is not commented out, so that we can use
 * "git commit -s --amend" on an existing commit that forgot to remove
 * it.
 *
 * Returns the number of bytes from the tail to ignore, to be fed as
 * the second parameter to append_signoff().
 */
int ignore_non_trailer(const char *buf, size_t len)
{
	int boc = 0;
	int bol = 0;
	int in_old_conflicts_block = 0;

	while (bol < len) {
		const char *next_line = memchr(buf + bol, '\n', len - bol);

		if (!next_line)
			next_line = buf + len;
		else
			next_line++;

		if (buf[bol] == comment_line_char || buf[bol] == '\n') {
			/* is this the first of the run of comments? */
			if (!boc)
				boc = bol;
			/* otherwise, it is just continuing */
		} else if (starts_with(buf + bol, "Conflicts:\n")) {
			in_old_conflicts_block = 1;
			if (!boc)
				boc = bol;
		} else if (in_old_conflicts_block && buf[bol] == '\t') {
			; /* a pathname in the conflicts block */
		} else if (boc) {
			/* the previous was not trailing comment */
			boc = 0;
			in_old_conflicts_block = 0;
		}
		bol = next_line - buf;
	}
	return boc ? len - boc : 0;
}
