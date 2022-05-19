#include "cache.h"
#include "tag.h"
#include "cummit.h"
#include "cummit-graph.h"
#include "repository.h"
#include "object-store.h"
#include "pkt-line.h"
#include "utf8.h"
#include "diff.h"
#include "revision.h"
#include "notes.h"
#include "alloc.h"
#include "gpg-interface.h"
#include "mergesort.h"
#include "cummit-slab.h"
#include "prio-queue.h"
#include "hash-lookup.h"
#include "wt-status.h"
#include "advice.h"
#include "refs.h"
#include "cummit-reach.h"
#include "run-command.h"
#include "shallow.h"
#include "hook.h"

static struct cummit_extra_header *read_cummit_extra_header_lines(const char *buf, size_t len, const char **);

int save_cummit_buffer = 1;
int no_graft_file_deprecated_advice;

const char *cummit_type = "cummit";

struct cummit *lookup_cummit_reference_gently(struct repository *r,
		const struct object_id *oid, int quiet)
{
	struct object *obj = deref_tag(r,
				       parse_object(r, oid),
				       NULL, 0);

	if (!obj)
		return NULL;
	return object_as_type(obj, OBJ_CUMMIT, quiet);
}

struct cummit *lookup_cummit_reference(struct repository *r, const struct object_id *oid)
{
	return lookup_cummit_reference_gently(r, oid, 0);
}

struct cummit *lookup_cummit_or_die(const struct object_id *oid, const char *ref_name)
{
	struct cummit *c = lookup_cummit_reference(the_repository, oid);
	if (!c)
		die(_("could not parse %s"), ref_name);
	if (!oideq(oid, &c->object.oid)) {
		warning(_("%s %s is not a cummit!"),
			ref_name, oid_to_hex(oid));
	}
	return c;
}

struct cummit *lookup_cummit(struct repository *r, const struct object_id *oid)
{
	struct object *obj = lookup_object(r, oid);
	if (!obj)
		return create_object(r, oid, alloc_cummit_node(r));
	return object_as_type(obj, OBJ_CUMMIT, 0);
}

struct cummit *lookup_cummit_reference_by_name(const char *name)
{
	struct object_id oid;
	struct cummit *cummit;

	if (get_oid_cummittish(name, &oid))
		return NULL;
	cummit = lookup_cummit_reference(the_repository, &oid);
	if (parse_cummit(cummit))
		return NULL;
	return cummit;
}

static timestamp_t parse_cummit_date(const char *buf, const char *tail)
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
	if (memcmp(buf, "cummitter", 9))
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
	/* dateptr < buf && buf[-1] == '\n', so parsing will stop at buf-1 */
	return parse_timestamp(dateptr, NULL, 10);
}

static const struct object_id *cummit_graft_oid_access(size_t index, const void *table)
{
	const struct cummit_graft * const *cummit_graft_table = table;
	return &cummit_graft_table[index]->oid;
}

int cummit_graft_pos(struct repository *r, const struct object_id *oid)
{
	return oid_pos(oid, r->parsed_objects->grafts,
		       r->parsed_objects->grafts_nr,
		       cummit_graft_oid_access);
}

int register_cummit_graft(struct repository *r, struct cummit_graft *graft,
			  int ignore_dups)
{
	int pos = cummit_graft_pos(r, &graft->oid);

	if (0 <= pos) {
		if (ignore_dups)
			free(graft);
		else {
			free(r->parsed_objects->grafts[pos]);
			r->parsed_objects->grafts[pos] = graft;
		}
		return 1;
	}
	pos = -pos - 1;
	ALLOC_GROW(r->parsed_objects->grafts,
		   r->parsed_objects->grafts_nr + 1,
		   r->parsed_objects->grafts_alloc);
	r->parsed_objects->grafts_nr++;
	if (pos < r->parsed_objects->grafts_nr)
		memmove(r->parsed_objects->grafts + pos + 1,
			r->parsed_objects->grafts + pos,
			(r->parsed_objects->grafts_nr - pos - 1) *
			sizeof(*r->parsed_objects->grafts));
	r->parsed_objects->grafts[pos] = graft;
	return 0;
}

struct cummit_graft *read_graft_line(struct strbuf *line)
{
	/* The format is just "cummit Parent1 Parent2 ...\n" */
	int i, phase;
	const char *tail = NULL;
	struct cummit_graft *graft = NULL;
	struct object_id dummy_oid, *oid;

	strbuf_rtrim(line);
	if (!line->len || line->buf[0] == '#')
		return NULL;
	/*
	 * phase 0 verifies line, counts hashes in line and allocates graft
	 * phase 1 fills graft
	 */
	for (phase = 0; phase < 2; phase++) {
		oid = graft ? &graft->oid : &dummy_oid;
		if (parse_oid_hex(line->buf, oid, &tail))
			goto bad_graft_data;
		for (i = 0; *tail != '\0'; i++) {
			oid = graft ? &graft->parent[i] : &dummy_oid;
			if (!isspace(*tail++) || parse_oid_hex(tail, oid, &tail))
				goto bad_graft_data;
		}
		if (!graft) {
			graft = xmalloc(st_add(sizeof(*graft),
					       st_mult(sizeof(struct object_id), i)));
			graft->nr_parent = i;
		}
	}
	return graft;

bad_graft_data:
	error("bad graft data: %s", line->buf);
	assert(!graft);
	return NULL;
}

static int read_graft_file(struct repository *r, const char *graft_file)
{
	FILE *fp = fopen_or_warn(graft_file, "r");
	struct strbuf buf = STRBUF_INIT;
	if (!fp)
		return -1;
	if (!no_graft_file_deprecated_advice &&
	    advice_enabled(ADVICE_GRAFT_FILE_DEPRECATED))
		advise(_("Support for <BUT_DIR>/info/grafts is deprecated\n"
			 "and will be removed in a future Git version.\n"
			 "\n"
			 "Please use \"but replace --convert-graft-file\"\n"
			 "to convert the grafts into replace refs.\n"
			 "\n"
			 "Turn this message off by running\n"
			 "\"but config advice.graftFileDeprecated false\""));
	while (!strbuf_getwholeline(&buf, fp, '\n')) {
		/* The format is just "cummit Parent1 Parent2 ...\n" */
		struct cummit_graft *graft = read_graft_line(&buf);
		if (!graft)
			continue;
		if (register_cummit_graft(r, graft, 1))
			error("duplicate graft data: %s", buf.buf);
	}
	fclose(fp);
	strbuf_release(&buf);
	return 0;
}

void prepare_cummit_graft(struct repository *r)
{
	char *graft_file;

	if (r->parsed_objects->cummit_graft_prepared)
		return;
	if (!startup_info->have_repository)
		return;

	graft_file = get_graft_file(r);
	read_graft_file(r, graft_file);
	/* make sure shallows are read */
	is_repository_shallow(r);
	r->parsed_objects->cummit_graft_prepared = 1;
}

struct cummit_graft *lookup_cummit_graft(struct repository *r, const struct object_id *oid)
{
	int pos;
	prepare_cummit_graft(r);
	pos = cummit_graft_pos(r, oid);
	if (pos < 0)
		return NULL;
	return r->parsed_objects->grafts[pos];
}

int for_each_cummit_graft(each_cummit_graft_fn fn, void *cb_data)
{
	int i, ret;
	for (i = ret = 0; i < the_repository->parsed_objects->grafts_nr && !ret; i++)
		ret = fn(the_repository->parsed_objects->grafts[i], cb_data);
	return ret;
}

void reset_cummit_grafts(struct repository *r)
{
	int i;

	for (i = 0; i < r->parsed_objects->grafts_nr; i++)
		free(r->parsed_objects->grafts[i]);
	r->parsed_objects->grafts_nr = 0;
	r->parsed_objects->cummit_graft_prepared = 0;
}

struct cummit_buffer {
	void *buffer;
	unsigned long size;
};
define_cummit_slab(buffer_slab, struct cummit_buffer);

struct buffer_slab *allocate_cummit_buffer_slab(void)
{
	struct buffer_slab *bs = xmalloc(sizeof(*bs));
	init_buffer_slab(bs);
	return bs;
}

void free_cummit_buffer_slab(struct buffer_slab *bs)
{
	clear_buffer_slab(bs);
	free(bs);
}

void set_cummit_buffer(struct repository *r, struct cummit *cummit, void *buffer, unsigned long size)
{
	struct cummit_buffer *v = buffer_slab_at(
		r->parsed_objects->buffer_slab, cummit);
	v->buffer = buffer;
	v->size = size;
}

const void *get_cached_cummit_buffer(struct repository *r, const struct cummit *cummit, unsigned long *sizep)
{
	struct cummit_buffer *v = buffer_slab_peek(
		r->parsed_objects->buffer_slab, cummit);
	if (!v) {
		if (sizep)
			*sizep = 0;
		return NULL;
	}
	if (sizep)
		*sizep = v->size;
	return v->buffer;
}

const void *repo_get_cummit_buffer(struct repository *r,
				   const struct cummit *cummit,
				   unsigned long *sizep)
{
	const void *ret = get_cached_cummit_buffer(r, cummit, sizep);
	if (!ret) {
		enum object_type type;
		unsigned long size;
		ret = repo_read_object_file(r, &cummit->object.oid, &type, &size);
		if (!ret)
			die("cannot read cummit object %s",
			    oid_to_hex(&cummit->object.oid));
		if (type != OBJ_CUMMIT)
			die("expected cummit for %s, got %s",
			    oid_to_hex(&cummit->object.oid), type_name(type));
		if (sizep)
			*sizep = size;
	}
	return ret;
}

void repo_unuse_cummit_buffer(struct repository *r,
			      const struct cummit *cummit,
			      const void *buffer)
{
	struct cummit_buffer *v = buffer_slab_peek(
		r->parsed_objects->buffer_slab, cummit);
	if (!(v && v->buffer == buffer))
		free((void *)buffer);
}

void free_cummit_buffer(struct parsed_object_pool *pool, struct cummit *cummit)
{
	struct cummit_buffer *v = buffer_slab_peek(
		pool->buffer_slab, cummit);
	if (v) {
		FREE_AND_NULL(v->buffer);
		v->size = 0;
	}
}

static inline void set_cummit_tree(struct cummit *c, struct tree *t)
{
	c->maybe_tree = t;
}

struct tree *repo_get_cummit_tree(struct repository *r,
				  const struct cummit *cummit)
{
	if (cummit->maybe_tree || !cummit->object.parsed)
		return cummit->maybe_tree;

	if (cummit_graph_position(cummit) != CUMMIT_NOT_FROM_GRAPH)
		return get_cummit_tree_in_graph(r, cummit);

	return NULL;
}

struct object_id *get_cummit_tree_oid(const struct cummit *cummit)
{
	struct tree *tree = get_cummit_tree(cummit);
	return tree ? &tree->object.oid : NULL;
}

void release_cummit_memory(struct parsed_object_pool *pool, struct cummit *c)
{
	set_cummit_tree(c, NULL);
	free_cummit_buffer(pool, c);
	c->index = 0;
	free_cummit_list(c->parents);

	c->object.parsed = 0;
}

const void *detach_cummit_buffer(struct cummit *cummit, unsigned long *sizep)
{
	struct cummit_buffer *v = buffer_slab_peek(
		the_repository->parsed_objects->buffer_slab, cummit);
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

int parse_cummit_buffer(struct repository *r, struct cummit *item, const void *buffer, unsigned long size, int check_graph)
{
	const char *tail = buffer;
	const char *bufptr = buffer;
	struct object_id parent;
	struct cummit_list **pptr;
	struct cummit_graft *graft;
	const int tree_entry_len = the_hash_algo->hexsz + 5;
	const int parent_entry_len = the_hash_algo->hexsz + 7;
	struct tree *tree;

	if (item->object.parsed)
		return 0;

	if (item->parents) {
		/*
		 * Presumably this is leftover from an earlier failed parse;
		 * clear it out in preparation for us re-parsing (we'll hit the
		 * same error, but that's good, since it lets our caller know
		 * the result cannot be trusted.
		 */
		free_cummit_list(item->parents);
		item->parents = NULL;
	}

	tail += size;
	if (tail <= bufptr + tree_entry_len + 1 || memcmp(bufptr, "tree ", 5) ||
			bufptr[tree_entry_len] != '\n')
		return error("bogus cummit object %s", oid_to_hex(&item->object.oid));
	if (get_oid_hex(bufptr + 5, &parent) < 0)
		return error("bad tree pointer in cummit %s",
			     oid_to_hex(&item->object.oid));
	tree = lookup_tree(r, &parent);
	if (!tree)
		return error("bad tree pointer %s in cummit %s",
			     oid_to_hex(&parent),
			     oid_to_hex(&item->object.oid));
	set_cummit_tree(item, tree);
	bufptr += tree_entry_len + 1; /* "tree " + "hex sha1" + "\n" */
	pptr = &item->parents;

	graft = lookup_cummit_graft(r, &item->object.oid);
	if (graft)
		r->parsed_objects->substituted_parent = 1;
	while (bufptr + parent_entry_len < tail && !memcmp(bufptr, "parent ", 7)) {
		struct cummit *new_parent;

		if (tail <= bufptr + parent_entry_len + 1 ||
		    get_oid_hex(bufptr + 7, &parent) ||
		    bufptr[parent_entry_len] != '\n')
			return error("bad parents in cummit %s", oid_to_hex(&item->object.oid));
		bufptr += parent_entry_len + 1;
		/*
		 * The clone is shallow if nr_parent < 0, and we must
		 * not traverse its real parents even when we unhide them.
		 */
		if (graft && (graft->nr_parent < 0 || grafts_replace_parents))
			continue;
		new_parent = lookup_cummit(r, &parent);
		if (!new_parent)
			return error("bad parent %s in cummit %s",
				     oid_to_hex(&parent),
				     oid_to_hex(&item->object.oid));
		pptr = &cummit_list_insert(new_parent, pptr)->next;
	}
	if (graft) {
		int i;
		struct cummit *new_parent;
		for (i = 0; i < graft->nr_parent; i++) {
			new_parent = lookup_cummit(r,
						   &graft->parent[i]);
			if (!new_parent)
				return error("bad graft parent %s in cummit %s",
					     oid_to_hex(&graft->parent[i]),
					     oid_to_hex(&item->object.oid));
			pptr = &cummit_list_insert(new_parent, pptr)->next;
		}
	}
	item->date = parse_cummit_date(bufptr, tail);

	if (check_graph)
		load_cummit_graph_info(r, item);

	item->object.parsed = 1;
	return 0;
}

int repo_parse_cummit_internal(struct repository *r,
			       struct cummit *item,
			       int quiet_on_missing,
			       int use_cummit_graph)
{
	enum object_type type;
	void *buffer;
	unsigned long size;
	int ret;

	if (!item)
		return -1;
	if (item->object.parsed)
		return 0;
	if (use_cummit_graph && parse_cummit_in_graph(r, item))
		return 0;
	buffer = repo_read_object_file(r, &item->object.oid, &type, &size);
	if (!buffer)
		return quiet_on_missing ? -1 :
			error("Could not read %s",
			     oid_to_hex(&item->object.oid));
	if (type != OBJ_CUMMIT) {
		free(buffer);
		return error("Object %s not a cummit",
			     oid_to_hex(&item->object.oid));
	}

	ret = parse_cummit_buffer(r, item, buffer, size, 0);
	if (save_cummit_buffer && !ret) {
		set_cummit_buffer(r, item, buffer, size);
		return 0;
	}
	free(buffer);
	return ret;
}

int repo_parse_cummit_gently(struct repository *r,
			     struct cummit *item, int quiet_on_missing)
{
	return repo_parse_cummit_internal(r, item, quiet_on_missing, 1);
}

void parse_cummit_or_die(struct cummit *item)
{
	if (parse_cummit(item))
		die("unable to parse cummit %s",
		    item ? oid_to_hex(&item->object.oid) : "(null)");
}

int find_cummit_subject(const char *cummit_buffer, const char **subject)
{
	const char *eol;
	const char *p = cummit_buffer;

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

size_t cummit_subject_length(const char *body)
{
	const char *p = body;
	while (*p) {
		const char *next = skip_blank_lines(p);
		if (next != p)
			break;
		p = strchrnul(p, '\n');
		if (*p)
			p++;
	}
	return p - body;
}

struct cummit_list *cummit_list_insert(struct cummit *item, struct cummit_list **list_p)
{
	struct cummit_list *new_list = xmalloc(sizeof(struct cummit_list));
	new_list->item = item;
	new_list->next = *list_p;
	*list_p = new_list;
	return new_list;
}

int cummit_list_contains(struct cummit *item, struct cummit_list *list)
{
	while (list) {
		if (list->item == item)
			return 1;
		list = list->next;
	}

	return 0;
}

unsigned cummit_list_count(const struct cummit_list *l)
{
	unsigned c = 0;
	for (; l; l = l->next )
		c++;
	return c;
}

struct cummit_list *copy_cummit_list(struct cummit_list *list)
{
	struct cummit_list *head = NULL;
	struct cummit_list **pp = &head;
	while (list) {
		pp = cummit_list_append(list->item, pp);
		list = list->next;
	}
	return head;
}

struct cummit_list *reverse_cummit_list(struct cummit_list *list)
{
	struct cummit_list *next = NULL, *current, *backup;
	for (current = list; current; current = backup) {
		backup = current->next;
		current->next = next;
		next = current;
	}
	return next;
}

void free_cummit_list(struct cummit_list *list)
{
	while (list)
		pop_cummit(&list);
}

struct cummit_list * cummit_list_insert_by_date(struct cummit *item, struct cummit_list **list)
{
	struct cummit_list **pp = list;
	struct cummit_list *p;
	while ((p = *pp) != NULL) {
		if (p->item->date < item->date) {
			break;
		}
		pp = &p->next;
	}
	return cummit_list_insert(item, pp);
}

static int cummit_list_compare_by_date(const void *a, const void *b)
{
	timestamp_t a_date = ((const struct cummit_list *)a)->item->date;
	timestamp_t b_date = ((const struct cummit_list *)b)->item->date;
	if (a_date < b_date)
		return 1;
	if (a_date > b_date)
		return -1;
	return 0;
}

static void *cummit_list_get_next(const void *a)
{
	return ((const struct cummit_list *)a)->next;
}

static void cummit_list_set_next(void *a, void *next)
{
	((struct cummit_list *)a)->next = next;
}

void cummit_list_sort_by_date(struct cummit_list **list)
{
	*list = llist_mergesort(*list, cummit_list_get_next, cummit_list_set_next,
				cummit_list_compare_by_date);
}

struct cummit *pop_most_recent_cummit(struct cummit_list **list,
				      unsigned int mark)
{
	struct cummit *ret = pop_cummit(list);
	struct cummit_list *parents = ret->parents;

	while (parents) {
		struct cummit *cummit = parents->item;
		if (!parse_cummit(cummit) && !(cummit->object.flags & mark)) {
			cummit->object.flags |= mark;
			cummit_list_insert_by_date(cummit, list);
		}
		parents = parents->next;
	}
	return ret;
}

static void clear_cummit_marks_1(struct cummit_list **plist,
				 struct cummit *cummit, unsigned int mark)
{
	while (cummit) {
		struct cummit_list *parents;

		if (!(mark & cummit->object.flags))
			return;

		cummit->object.flags &= ~mark;

		parents = cummit->parents;
		if (!parents)
			return;

		while ((parents = parents->next))
			cummit_list_insert(parents->item, plist);

		cummit = cummit->parents->item;
	}
}

void clear_cummit_marks_many(int nr, struct cummit **cummit, unsigned int mark)
{
	struct cummit_list *list = NULL;

	while (nr--) {
		clear_cummit_marks_1(&list, *cummit, mark);
		cummit++;
	}
	while (list)
		clear_cummit_marks_1(&list, pop_cummit(&list), mark);
}

void clear_cummit_marks(struct cummit *cummit, unsigned int mark)
{
	clear_cummit_marks_many(1, &cummit, mark);
}

struct cummit *pop_cummit(struct cummit_list **stack)
{
	struct cummit_list *top = *stack;
	struct cummit *item = top ? top->item : NULL;

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
define_cummit_slab(indegree_slab, int);

define_cummit_slab(author_date_slab, timestamp_t);

void record_author_date(struct author_date_slab *author_date,
			struct cummit *cummit)
{
	const char *buffer = get_cummit_buffer(cummit, NULL);
	struct ident_split ident;
	const char *ident_line;
	size_t ident_len;
	char *date_end;
	timestamp_t date;

	ident_line = find_commit_header(buffer, "author", &ident_len);
	if (!ident_line)
		goto fail_exit; /* no author line */
	if (split_ident_line(&ident, ident_line, ident_len) ||
	    !ident.date_begin || !ident.date_end)
		goto fail_exit; /* malformed "author" line */

	date = parse_timestamp(ident.date_begin, &date_end, 10);
	if (date_end != ident.date_end)
		goto fail_exit; /* malformed date */
	*(author_date_slab_at(author_date, cummit)) = date;

fail_exit:
	unuse_cummit_buffer(cummit, buffer);
}

int compare_cummits_by_author_date(const void *a_, const void *b_,
				   void *cb_data)
{
	const struct cummit *a = a_, *b = b_;
	struct author_date_slab *author_date = cb_data;
	timestamp_t a_date = *(author_date_slab_at(author_date, a));
	timestamp_t b_date = *(author_date_slab_at(author_date, b));

	/* newer cummits with larger date first */
	if (a_date < b_date)
		return 1;
	else if (a_date > b_date)
		return -1;
	return 0;
}

int compare_cummits_by_gen_then_cummit_date(const void *a_, const void *b_, void *unused)
{
	const struct cummit *a = a_, *b = b_;
	const timestamp_t generation_a = cummit_graph_generation(a),
			  generation_b = cummit_graph_generation(b);

	/* newer cummits first */
	if (generation_a < generation_b)
		return 1;
	else if (generation_a > generation_b)
		return -1;

	/* use date as a heuristic when generations are equal */
	if (a->date < b->date)
		return 1;
	else if (a->date > b->date)
		return -1;
	return 0;
}

int compare_cummits_by_cummit_date(const void *a_, const void *b_, void *unused)
{
	const struct cummit *a = a_, *b = b_;
	/* newer cummits with larger date first */
	if (a->date < b->date)
		return 1;
	else if (a->date > b->date)
		return -1;
	return 0;
}

/*
 * Performs an in-place topological sort on the list supplied.
 */
void sort_in_topological_order(struct cummit_list **list, enum rev_sort_order sort_order)
{
	struct cummit_list *next, *orig = *list;
	struct cummit_list **pptr;
	struct indegree_slab indegree;
	struct prio_queue queue;
	struct cummit *cummit;
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
	case REV_SORT_BY_CUMMIT_DATE:
		queue.compare = compare_cummits_by_cummit_date;
		break;
	case REV_SORT_BY_AUTHOR_DATE:
		init_author_date_slab(&author_date);
		queue.compare = compare_cummits_by_author_date;
		queue.cb_data = &author_date;
		break;
	}

	/* Mark them and clear the indegree */
	for (next = orig; next; next = next->next) {
		struct cummit *cummit = next->item;
		*(indegree_slab_at(&indegree, cummit)) = 1;
		/* also record the author dates, if needed */
		if (sort_order == REV_SORT_BY_AUTHOR_DATE)
			record_author_date(&author_date, cummit);
	}

	/* update the indegree */
	for (next = orig; next; next = next->next) {
		struct cummit_list *parents = next->item->parents;
		while (parents) {
			struct cummit *parent = parents->item;
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
		struct cummit *cummit = next->item;

		if (*(indegree_slab_at(&indegree, cummit)) == 1)
			prio_queue_put(&queue, cummit);
	}

	/*
	 * This is unfortunate; the initial tips need to be shown
	 * in the order given from the revision traversal machinery.
	 */
	if (sort_order == REV_SORT_IN_GRAPH_ORDER)
		prio_queue_reverse(&queue);

	/* We no longer need the cummit list */
	free_cummit_list(orig);

	pptr = list;
	*list = NULL;
	while ((cummit = prio_queue_get(&queue)) != NULL) {
		struct cummit_list *parents;

		for (parents = cummit->parents; parents ; parents = parents->next) {
			struct cummit *parent = parents->item;
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
		*(indegree_slab_at(&indegree, cummit)) = 0;

		pptr = &cummit_list_insert(cummit, pptr)->next;
	}

	clear_indegree_slab(&indegree);
	clear_prio_queue(&queue);
	if (sort_order == REV_SORT_BY_AUTHOR_DATE)
		clear_author_date_slab(&author_date);
}

struct rev_collect {
	struct cummit **cummit;
	int nr;
	int alloc;
	unsigned int initial : 1;
};

static void add_one_cummit(struct object_id *oid, struct rev_collect *revs)
{
	struct cummit *cummit;

	if (is_null_oid(oid))
		return;

	cummit = lookup_cummit(the_repository, oid);
	if (!cummit ||
	    (cummit->object.flags & TMP_MARK) ||
	    parse_cummit(cummit))
		return;

	ALLOC_GROW(revs->cummit, revs->nr + 1, revs->alloc);
	revs->cummit[revs->nr++] = cummit;
	cummit->object.flags |= TMP_MARK;
}

static int collect_one_reflog_ent(struct object_id *ooid, struct object_id *noid,
				  const char *ident, timestamp_t timestamp,
				  int tz, const char *message, void *cbdata)
{
	struct rev_collect *revs = cbdata;

	if (revs->initial) {
		revs->initial = 0;
		add_one_cummit(ooid, revs);
	}
	add_one_cummit(noid, revs);
	return 0;
}

struct cummit *get_fork_point(const char *refname, struct cummit *cummit)
{
	struct object_id oid;
	struct rev_collect revs;
	struct cummit_list *bases;
	int i;
	struct cummit *ret = NULL;
	char *full_refname;

	switch (dwim_ref(refname, strlen(refname), &oid, &full_refname, 0)) {
	case 0:
		die("No such ref: '%s'", refname);
	case 1:
		break; /* good */
	default:
		die("Ambiguous refname: '%s'", refname);
	}

	memset(&revs, 0, sizeof(revs));
	revs.initial = 1;
	for_each_reflog_ent(full_refname, collect_one_reflog_ent, &revs);

	if (!revs.nr)
		add_one_cummit(&oid, &revs);

	for (i = 0; i < revs.nr; i++)
		revs.cummit[i]->object.flags &= ~TMP_MARK;

	bases = get_merge_bases_many(cummit, revs.nr, revs.cummit);

	/*
	 * There should be one and only one merge base, when we found
	 * a common ancestor among reflog entries.
	 */
	if (!bases || bases->next)
		goto cleanup_return;

	/* And the found one must be one of the reflog entries */
	for (i = 0; i < revs.nr; i++)
		if (&bases->item->object == &revs.cummit[i]->object)
			break; /* found */
	if (revs.nr <= i)
		goto cleanup_return;

	ret = bases->item;

cleanup_return:
	free_cummit_list(bases);
	free(full_refname);
	return ret;
}

/*
 * Indexed by hash algorithm identifier.
 */
static const char *gpg_sig_headers[] = {
	NULL,
	"gpgsig",
	"gpgsig-sha256",
};

int sign_with_header(struct strbuf *buf, const char *keyid)
{
	struct strbuf sig = STRBUF_INIT;
	int inspos, copypos;
	const char *eoh;
	const char *gpg_sig_header = gpg_sig_headers[hash_algo_by_ptr(the_hash_algo)];
	int gpg_sig_header_len = strlen(gpg_sig_header);

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
		strbuf_insertstr(buf, inspos++, " ");
		strbuf_insert(buf, inspos, bol, len);
		inspos += len;
		copypos += len;
	}
	strbuf_release(&sig);
	return 0;
}



int parse_signed_cummit(const struct cummit *cummit,
			struct strbuf *payload, struct strbuf *signature,
			const struct but_hash_algo *algop)
{
	unsigned long size;
	const char *buffer = get_cummit_buffer(cummit, &size);
	int ret = parse_buffer_signed_by_header(buffer, size, payload, signature, algop);

	unuse_cummit_buffer(cummit, buffer);
	return ret;
}

int parse_buffer_signed_by_header(const char *buffer,
				  unsigned long size,
				  struct strbuf *payload,
				  struct strbuf *signature,
				  const struct but_hash_algo *algop)
{
	int in_signature = 0, saw_signature = 0, other_signature = 0;
	const char *line, *tail, *p;
	const char *gpg_sig_header = gpg_sig_headers[hash_algo_by_ptr(algop)];

	line = buffer;
	tail = buffer + size;
	while (line < tail) {
		const char *sig = NULL;
		const char *next = memchr(line, '\n', tail - line);

		next = next ? next + 1 : tail;
		if (in_signature && line[0] == ' ')
			sig = line + 1;
		else if (skip_prefix(line, gpg_sig_header, &p) &&
			 *p == ' ') {
			sig = line + strlen(gpg_sig_header) + 1;
			other_signature = 0;
		}
		else if (starts_with(line, "gpgsig"))
			other_signature = 1;
		else if (other_signature && line[0] != ' ')
			other_signature = 0;
		if (sig) {
			strbuf_add(signature, sig, next - sig);
			saw_signature = 1;
			in_signature = 1;
		} else {
			if (*line == '\n')
				/* dump the whole remainder of the buffer */
				next = tail;
			if (!other_signature)
				strbuf_add(payload, line, next - line);
			in_signature = 0;
		}
		line = next;
	}
	return saw_signature;
}

int remove_signature(struct strbuf *buf)
{
	const char *line = buf->buf;
	const char *tail = buf->buf + buf->len;
	int in_signature = 0;
	struct sigbuf {
		const char *start;
		const char *end;
	} sigs[2], *sigp = &sigs[0];
	int i;
	const char *orig_buf = buf->buf;

	memset(sigs, 0, sizeof(sigs));

	while (line < tail) {
		const char *next = memchr(line, '\n', tail - line);
		next = next ? next + 1 : tail;

		if (in_signature && line[0] == ' ')
			sigp->end = next;
		else if (starts_with(line, "gpgsig")) {
			int i;
			for (i = 1; i < BUT_HASH_NALGOS; i++) {
				const char *p;
				if (skip_prefix(line, gpg_sig_headers[i], &p) &&
				    *p == ' ') {
					sigp->start = line;
					sigp->end = next;
					in_signature = 1;
				}
			}
		} else {
			if (*line == '\n')
				/* dump the whole remainder of the buffer */
				next = tail;
			if (in_signature && sigp - sigs != ARRAY_SIZE(sigs))
				sigp++;
			in_signature = 0;
		}
		line = next;
	}

	for (i = ARRAY_SIZE(sigs) - 1; i >= 0; i--)
		if (sigs[i].start)
			strbuf_remove(buf, sigs[i].start - orig_buf, sigs[i].end - sigs[i].start);

	return sigs[0].start != NULL;
}

static void handle_signed_tag(struct cummit *parent, struct cummit_extra_header ***tail)
{
	struct merge_remote_desc *desc;
	struct cummit_extra_header *mergetag;
	char *buf;
	unsigned long size;
	enum object_type type;
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;

	desc = merge_remote_util(parent);
	if (!desc || !desc->obj)
		return;
	buf = read_object_file(&desc->obj->oid, &type, &size);
	if (!buf || type != OBJ_TAG)
		goto free_return;
	if (!parse_signature(buf, size, &payload, &signature))
		goto free_return;
	/*
	 * We could verify this signature and either omit the tag when
	 * it does not validate, but the integrator may not have the
	 * public key of the signer of the tag being merged, while a
	 * later auditor may have it while auditing, so let's not run
	 * verify-signed-buffer here for now...
	 *
	 * if (verify_signed_buffer(buf, len, buf + len, size - len, ...))
	 *	warn("warning: signed tag unverified.");
	 */
	CALLOC_ARRAY(mergetag, 1);
	mergetag->key = xstrdup("mergetag");
	mergetag->value = buf;
	mergetag->len = size;

	**tail = mergetag;
	*tail = &mergetag->next;
	strbuf_release(&payload);
	strbuf_release(&signature);
	return;

free_return:
	free(buf);
}

int check_cummit_signature(const struct cummit *cummit, struct signature_check *sigc)
{
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;
	int ret = 1;

	sigc->result = 'N';

	if (parse_signed_cummit(cummit, &payload, &signature, the_hash_algo) <= 0)
		goto out;

	sigc->payload_type = SIGNATURE_PAYLOAD_CUMMIT;
	sigc->payload = strbuf_detach(&payload, &sigc->payload_len);
	ret = check_signature(sigc, signature.buf, signature.len);

 out:
	strbuf_release(&payload);
	strbuf_release(&signature);

	return ret;
}

void verify_merge_signature(struct cummit *cummit, int verbosity,
			    int check_trust)
{
	char hex[BUT_MAX_HEXSZ + 1];
	struct signature_check signature_check;
	int ret;
	memset(&signature_check, 0, sizeof(signature_check));

	ret = check_cummit_signature(cummit, &signature_check);

	find_unique_abbrev_r(hex, &cummit->object.oid, DEFAULT_ABBREV);
	switch (signature_check.result) {
	case 'G':
		if (ret || (check_trust && signature_check.trust_level < TRUST_MARGINAL))
			die(_("cummit %s has an untrusted GPG signature, "
			      "allegedly by %s."), hex, signature_check.signer);
		break;
	case 'B':
		die(_("cummit %s has a bad GPG signature "
		      "allegedly by %s."), hex, signature_check.signer);
	default: /* 'N' */
		die(_("cummit %s does not have a GPG signature."), hex);
	}
	if (verbosity >= 0 && signature_check.result == 'G')
		printf(_("cummit %s has a good GPG signature by %s\n"),
		       hex, signature_check.signer);

	signature_check_clear(&signature_check);
}

void append_merge_tag_headers(struct cummit_list *parents,
			      struct cummit_extra_header ***tail)
{
	while (parents) {
		struct cummit *parent = parents->item;
		handle_signed_tag(parent, tail);
		parents = parents->next;
	}
}

static void add_extra_header(struct strbuf *buffer,
			     struct cummit_extra_header *extra)
{
	strbuf_addstr(buffer, extra->key);
	if (extra->len)
		strbuf_add_lines(buffer, " ", extra->value, extra->len);
	else
		strbuf_addch(buffer, '\n');
}

struct cummit_extra_header *read_cummit_extra_headers(struct cummit *cummit,
						      const char **exclude)
{
	struct cummit_extra_header *extra = NULL;
	unsigned long size;
	const char *buffer = get_cummit_buffer(cummit, &size);
	extra = read_cummit_extra_header_lines(buffer, size, exclude);
	unuse_cummit_buffer(cummit, buffer);
	return extra;
}

int for_each_mergetag(each_mergetag_fn fn, struct cummit *cummit, void *data)
{
	struct cummit_extra_header *extra, *to_free;
	int res = 0;

	to_free = read_cummit_extra_headers(cummit, NULL);
	for (extra = to_free; !res && extra; extra = extra->next) {
		if (strcmp(extra->key, "mergetag"))
			continue; /* not a merge tag */
		res = fn(cummit, extra, data);
	}
	free_cummit_extra_headers(to_free);
	return res;
}

static inline int standard_header_field(const char *field, size_t len)
{
	return ((len == 4 && !memcmp(field, "tree", 4)) ||
		(len == 6 && !memcmp(field, "parent", 6)) ||
		(len == 6 && !memcmp(field, "author", 6)) ||
		(len == 9 && !memcmp(field, "cummitter", 9)) ||
		(len == 8 && !memcmp(field, "encoding", 8)));
}

static int excluded_header_field(const char *field, size_t len, const char **exclude)
{
	if (!exclude)
		return 0;

	while (*exclude) {
		size_t xlen = strlen(*exclude);
		if (len == xlen && !memcmp(field, *exclude, xlen))
			return 1;
		exclude++;
	}
	return 0;
}

static struct cummit_extra_header *read_cummit_extra_header_lines(
	const char *buffer, size_t size,
	const char **exclude)
{
	struct cummit_extra_header *extra = NULL, **tail = &extra, *it = NULL;
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

		eof = memchr(line, ' ', next - line);
		if (!eof)
			eof = next;
		else if (standard_header_field(line, eof - line) ||
			 excluded_header_field(line, eof - line, exclude))
			continue;

		CALLOC_ARRAY(it, 1);
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

void free_cummit_extra_headers(struct cummit_extra_header *extra)
{
	while (extra) {
		struct cummit_extra_header *next = extra->next;
		free(extra->key);
		free(extra->value);
		free(extra);
		extra = next;
	}
}

int cummit_tree(const char *msg, size_t msg_len, const struct object_id *tree,
		struct cummit_list *parents, struct object_id *ret,
		const char *author, const char *sign_cummit)
{
	struct cummit_extra_header *extra = NULL, **tail = &extra;
	int result;

	append_merge_tag_headers(parents, &tail);
	result = cummit_tree_extended(msg, msg_len, tree, parents, ret, author,
				      NULL, sign_cummit, extra);
	free_cummit_extra_headers(extra);
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

static const char cummit_utf8_warn[] =
N_("Warning: cummit message did not conform to UTF-8.\n"
   "You may want to amend it after fixing the message, or set the config\n"
   "variable i18n.cummitencoding to the encoding your project uses.\n");

int cummit_tree_extended(const char *msg, size_t msg_len,
			 const struct object_id *tree,
			 struct cummit_list *parents, struct object_id *ret,
			 const char *author, const char *cummitter,
			 const char *sign_cummit,
			 struct cummit_extra_header *extra)
{
	int result;
	int encoding_is_utf8;
	struct strbuf buffer;

	assert_oid_type(tree, OBJ_TREE);

	if (memchr(msg, '\0', msg_len))
		return error("a NUL byte in cummit log message not allowed.");

	/* Not having i18n.cummitencoding is the same as having utf-8 */
	encoding_is_utf8 = is_encoding_utf8(but_cummit_encoding);

	strbuf_init(&buffer, 8192); /* should avoid reallocs for the headers */
	strbuf_addf(&buffer, "tree %s\n", oid_to_hex(tree));

	/*
	 * NOTE! This ordering means that the same exact tree merged with a
	 * different order of parents will be a _different_ changeset even
	 * if everything else stays the same.
	 */
	while (parents) {
		struct cummit *parent = pop_cummit(&parents);
		strbuf_addf(&buffer, "parent %s\n",
			    oid_to_hex(&parent->object.oid));
	}

	/* Person/date information */
	if (!author)
		author = but_author_info(IDENT_STRICT);
	strbuf_addf(&buffer, "author %s\n", author);
	if (!cummitter)
		cummitter = but_cummitter_info(IDENT_STRICT);
	strbuf_addf(&buffer, "cummitter %s\n", cummitter);
	if (!encoding_is_utf8)
		strbuf_addf(&buffer, "encoding %s\n", but_cummit_encoding);

	while (extra) {
		add_extra_header(&buffer, extra);
		extra = extra->next;
	}
	strbuf_addch(&buffer, '\n');

	/* And add the comment */
	strbuf_add(&buffer, msg, msg_len);

	/* And check the encoding */
	if (encoding_is_utf8 && !verify_utf8(&buffer))
		fprintf(stderr, _(cummit_utf8_warn));

	if (sign_cummit && sign_with_header(&buffer, sign_cummit)) {
		result = -1;
		goto out;
	}

	result = write_object_file(buffer.buf, buffer.len, OBJ_CUMMIT, ret);
out:
	strbuf_release(&buffer);
	return result;
}

define_cummit_slab(merge_desc_slab, struct merge_remote_desc *);
static struct merge_desc_slab merge_desc_slab = CUMMIT_SLAB_INIT(1, merge_desc_slab);

struct merge_remote_desc *merge_remote_util(struct cummit *cummit)
{
	return *merge_desc_slab_at(&merge_desc_slab, cummit);
}

void set_merge_remote_desc(struct cummit *cummit,
			   const char *name, struct object *obj)
{
	struct merge_remote_desc *desc;
	FLEX_ALLOC_STR(desc, name, name);
	desc->obj = obj;
	*merge_desc_slab_at(&merge_desc_slab, cummit) = desc;
}

struct cummit *get_merge_parent(const char *name)
{
	struct object *obj;
	struct cummit *cummit;
	struct object_id oid;
	if (get_oid(name, &oid))
		return NULL;
	obj = parse_object(the_repository, &oid);
	cummit = (struct cummit *)peel_to_type(name, 0, obj, OBJ_CUMMIT);
	if (cummit && !merge_remote_util(cummit))
		set_merge_remote_desc(cummit, name, obj);
	return cummit;
}

/*
 * Append a cummit to the end of the cummit_list.
 *
 * next starts by pointing to the variable that holds the head of an
 * empty cummit_list, and is updated to point to the "next" field of
 * the last item on the list as new cummits are appended.
 *
 * Usage example:
 *
 *     struct cummit_list *list;
 *     struct cummit_list **next = &list;
 *
 *     next = cummit_list_append(c1, next);
 *     next = cummit_list_append(c2, next);
 *     assert(cummit_list_count(list) == 2);
 *     return list;
 */
struct cummit_list **cummit_list_append(struct cummit *cummit,
					struct cummit_list **next)
{
	struct cummit_list *new_cummit = xmalloc(sizeof(struct cummit_list));
	new_cummit->item = cummit;
	*next = new_cummit;
	new_cummit->next = NULL;
	return &new_cummit->next;
}

const char *find_header_mem(const char *msg, size_t len,
			const char *key, size_t *out_len)
{
	int key_len = strlen(key);
	const char *line = msg;

	/*
	 * NEEDSWORK: It's possible for strchrnul() to scan beyond the range
	 * given by len. However, current callers are safe because they compute
	 * len by scanning a NUL-terminated block of memory starting at msg.
	 * Nonetheless, it would be better to ensure the function does not look
	 * at msg beyond the len provided by the caller.
	 */
	while (line && line < msg + len) {
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

const char *find_commit_header(const char *msg, const char *key, size_t *out_len)
{
	return find_header_mem(msg, strlen(msg), key, out_len);
}
/*
 * Inspect the given string and determine the true "end" of the log message, in
 * order to find where to put a new Signed-off-by trailer.  Ignored are
 * trailing comment lines and blank lines.  To support "but cummit -s
 * --amend" on an existing cummit, we also ignore "Conflicts:".  To
 * support "but cummit -v", we truncate at cut lines.
 *
 * Returns the number of bytes from the tail to ignore, to be fed as
 * the second parameter to append_signoff().
 */
size_t ignore_non_trailer(const char *buf, size_t len)
{
	size_t boc = 0;
	size_t bol = 0;
	int in_old_conflicts_block = 0;
	size_t cutoff = wt_status_locate_end(buf, len);

	while (bol < cutoff) {
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
	return boc ? len - boc : len - cutoff;
}

int run_commit_hook(int editor_is_used, const char *index_file,
		    int *invoked_hook, const char *name, ...)
{
	struct run_hooks_opt opt = RUN_HOOKS_OPT_INIT;
	va_list args;
	const char *arg;

	strvec_pushf(&opt.env, "BUT_INDEX_FILE=%s", index_file);

	/*
	 * Let the hook know that no editor will be launched.
	 */
	if (!editor_is_used)
		strvec_push(&opt.env, "BUT_EDITOR=:");

	va_start(args, name);
	while ((arg = va_arg(args, const char *)))
		strvec_push(&opt.args, arg);
	va_end(args);

	opt.invoked_hook = invoked_hook;
	return run_hooks_opt(name, &opt);
}
