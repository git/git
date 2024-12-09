#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "tag.h"
#include "commit.h"
#include "commit-graph.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "repository.h"
#include "object-name.h"
#include "object-store-ll.h"
#include "utf8.h"
#include "diff.h"
#include "revision.h"
#include "notes.h"
#include "alloc.h"
#include "gpg-interface.h"
#include "mergesort.h"
#include "commit-slab.h"
#include "prio-queue.h"
#include "hash-lookup.h"
#include "wt-status.h"
#include "advice.h"
#include "refs.h"
#include "commit-reach.h"
#include "setup.h"
#include "shallow.h"
#include "tree.h"
#include "hook.h"
#include "parse.h"
#include "object-file-convert.h"

static struct commit_extra_header *read_commit_extra_header_lines(const char *buf, size_t len, const char **);

int save_commit_buffer = 1;
int no_graft_file_deprecated_advice;

const char *commit_type = "commit";

struct commit *lookup_commit_reference_gently(struct repository *r,
		const struct object_id *oid, int quiet)
{
	struct object *obj = deref_tag(r,
				       parse_object(r, oid),
				       NULL, 0);

	if (!obj)
		return NULL;
	return object_as_type(obj, OBJ_COMMIT, quiet);
}

struct commit *lookup_commit_reference(struct repository *r, const struct object_id *oid)
{
	return lookup_commit_reference_gently(r, oid, 0);
}

struct commit *lookup_commit_or_die(const struct object_id *oid, const char *ref_name)
{
	struct commit *c = lookup_commit_reference(the_repository, oid);
	if (!c)
		die(_("could not parse %s"), ref_name);
	if (!oideq(oid, &c->object.oid)) {
		warning(_("%s %s is not a commit!"),
			ref_name, oid_to_hex(oid));
	}
	return c;
}

struct commit *lookup_commit_object(struct repository *r,
				    const struct object_id *oid)
{
	struct object *obj = parse_object(r, oid);
	return obj ? object_as_type(obj, OBJ_COMMIT, 0) : NULL;

}

struct commit *lookup_commit(struct repository *r, const struct object_id *oid)
{
	struct object *obj = lookup_object(r, oid);
	if (!obj)
		return create_object(r, oid, alloc_commit_node(r));
	return object_as_type(obj, OBJ_COMMIT, 0);
}

struct commit *lookup_commit_reference_by_name(const char *name)
{
	return lookup_commit_reference_by_name_gently(name, 0);
}

struct commit *lookup_commit_reference_by_name_gently(const char *name,
						      int quiet)
{
	struct object_id oid;
	struct commit *commit;

	if (repo_get_oid_committish(the_repository, name, &oid))
		return NULL;
	commit = lookup_commit_reference_gently(the_repository, &oid, quiet);
	if (repo_parse_commit(the_repository, commit))
		return NULL;
	return commit;
}

static timestamp_t parse_commit_date(const char *buf, const char *tail)
{
	const char *dateptr;
	const char *eol;

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

	/*
	 * Jump to end-of-line so that we can walk backwards to find the
	 * end-of-email ">". This is more forgiving of malformed cases
	 * because unexpected characters tend to be in the name and email
	 * fields.
	 */
	eol = memchr(buf, '\n', tail - buf);
	if (!eol)
		return 0;
	dateptr = eol;
	while (dateptr > buf && dateptr[-1] != '>')
		dateptr--;
	if (dateptr == buf)
		return 0;

	/*
	 * Trim leading whitespace, but make sure we have at least one
	 * non-whitespace character, as parse_timestamp() will otherwise walk
	 * right past the newline we found in "eol" when skipping whitespace
	 * itself.
	 *
	 * In theory it would be sufficient to allow any character not matched
	 * by isspace(), but there's a catch: our isspace() does not
	 * necessarily match the behavior of parse_timestamp(), as the latter
	 * is implemented by system routines which match more exotic control
	 * codes, or even locale-dependent sequences.
	 *
	 * Since we expect the timestamp to be a number, we can check for that.
	 * Anything else (e.g., a non-numeric token like "foo") would just
	 * cause parse_timestamp() to return 0 anyway.
	 */
	while (dateptr < eol && isspace(*dateptr))
		dateptr++;
	if (!isdigit(*dateptr) && *dateptr != '-')
		return 0;

	/*
	 * We know there is at least one digit (or dash), so we'll begin
	 * parsing there and stop at worst case at eol.
	 *
	 * Note that we may feed parse_timestamp() extra characters here if the
	 * commit is malformed, and it will parse as far as it can. For
	 * example, "123foo456" would return "123". That might be questionable
	 * (versus returning "0"), but it would help in a hypothetical case
	 * like "123456+0100", where the whitespace from the timezone is
	 * missing. Since such syntactic errors may be baked into history and
	 * hard to correct now, let's err on trying to make our best guess
	 * here, rather than insist on perfect syntax.
	 */
	return parse_timestamp(dateptr, NULL, 10);
}

static const struct object_id *commit_graft_oid_access(size_t index, const void *table)
{
	const struct commit_graft * const *commit_graft_table = table;
	return &commit_graft_table[index]->oid;
}

int commit_graft_pos(struct repository *r, const struct object_id *oid)
{
	return oid_pos(oid, r->parsed_objects->grafts,
		       r->parsed_objects->grafts_nr,
		       commit_graft_oid_access);
}

void unparse_commit(struct repository *r, const struct object_id *oid)
{
	struct commit *c = lookup_commit(r, oid);

	if (!c->object.parsed)
		return;
	free_commit_list(c->parents);
	c->parents = NULL;
	c->object.parsed = 0;
}

int register_commit_graft(struct repository *r, struct commit_graft *graft,
			  int ignore_dups)
{
	int pos = commit_graft_pos(r, &graft->oid);

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
	unparse_commit(r, &graft->oid);
	return 0;
}

struct commit_graft *read_graft_line(struct strbuf *line)
{
	/* The format is just "Commit Parent1 Parent2 ...\n" */
	int i, phase;
	const char *tail = NULL;
	struct commit_graft *graft = NULL;
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
		advise(_("Support for <GIT_DIR>/info/grafts is deprecated\n"
			 "and will be removed in a future Git version.\n"
			 "\n"
			 "Please use \"git replace --convert-graft-file\"\n"
			 "to convert the grafts into replace refs.\n"
			 "\n"
			 "Turn this message off by running\n"
			 "\"git config set advice.graftFileDeprecated false\""));
	while (!strbuf_getwholeline(&buf, fp, '\n')) {
		/* The format is just "Commit Parent1 Parent2 ...\n" */
		struct commit_graft *graft = read_graft_line(&buf);
		if (!graft)
			continue;
		if (register_commit_graft(r, graft, 1))
			error("duplicate graft data: %s", buf.buf);
	}
	fclose(fp);
	strbuf_release(&buf);
	return 0;
}

void prepare_commit_graft(struct repository *r)
{
	const char *graft_file;

	if (r->parsed_objects->commit_graft_prepared)
		return;
	if (!startup_info->have_repository)
		return;

	graft_file = repo_get_graft_file(r);
	read_graft_file(r, graft_file);
	/* make sure shallows are read */
	is_repository_shallow(r);
	r->parsed_objects->commit_graft_prepared = 1;
}

struct commit_graft *lookup_commit_graft(struct repository *r, const struct object_id *oid)
{
	int pos;
	prepare_commit_graft(r);
	pos = commit_graft_pos(r, oid);
	if (pos < 0)
		return NULL;
	return r->parsed_objects->grafts[pos];
}

int for_each_commit_graft(each_commit_graft_fn fn, void *cb_data)
{
	int i, ret;
	for (i = ret = 0; i < the_repository->parsed_objects->grafts_nr && !ret; i++)
		ret = fn(the_repository->parsed_objects->grafts[i], cb_data);
	return ret;
}

struct commit_buffer {
	void *buffer;
	unsigned long size;
};
define_commit_slab(buffer_slab, struct commit_buffer);

struct buffer_slab *allocate_commit_buffer_slab(void)
{
	struct buffer_slab *bs = xmalloc(sizeof(*bs));
	init_buffer_slab(bs);
	return bs;
}

void free_commit_buffer_slab(struct buffer_slab *bs)
{
	clear_buffer_slab(bs);
	free(bs);
}

void set_commit_buffer(struct repository *r, struct commit *commit, void *buffer, unsigned long size)
{
	struct commit_buffer *v = buffer_slab_at(
		r->parsed_objects->buffer_slab, commit);
	v->buffer = buffer;
	v->size = size;
}

const void *get_cached_commit_buffer(struct repository *r, const struct commit *commit, unsigned long *sizep)
{
	struct commit_buffer *v = buffer_slab_peek(
		r->parsed_objects->buffer_slab, commit);
	if (!v) {
		if (sizep)
			*sizep = 0;
		return NULL;
	}
	if (sizep)
		*sizep = v->size;
	return v->buffer;
}

const void *repo_get_commit_buffer(struct repository *r,
				   const struct commit *commit,
				   unsigned long *sizep)
{
	const void *ret = get_cached_commit_buffer(r, commit, sizep);
	if (!ret) {
		enum object_type type;
		unsigned long size;
		ret = repo_read_object_file(r, &commit->object.oid, &type, &size);
		if (!ret)
			die("cannot read commit object %s",
			    oid_to_hex(&commit->object.oid));
		if (type != OBJ_COMMIT)
			die("expected commit for %s, got %s",
			    oid_to_hex(&commit->object.oid), type_name(type));
		if (sizep)
			*sizep = size;
	}
	return ret;
}

void repo_unuse_commit_buffer(struct repository *r,
			      const struct commit *commit,
			      const void *buffer)
{
	struct commit_buffer *v = buffer_slab_peek(
		r->parsed_objects->buffer_slab, commit);
	if (!(v && v->buffer == buffer))
		free((void *)buffer);
}

void free_commit_buffer(struct parsed_object_pool *pool, struct commit *commit)
{
	struct commit_buffer *v = buffer_slab_peek(
		pool->buffer_slab, commit);
	if (v) {
		FREE_AND_NULL(v->buffer);
		v->size = 0;
	}
}

static inline void set_commit_tree(struct commit *c, struct tree *t)
{
	c->maybe_tree = t;
}

struct tree *repo_get_commit_tree(struct repository *r,
				  const struct commit *commit)
{
	if (commit->maybe_tree || !commit->object.parsed)
		return commit->maybe_tree;

	if (commit_graph_position(commit) != COMMIT_NOT_FROM_GRAPH)
		return get_commit_tree_in_graph(r, commit);

	return NULL;
}

struct object_id *get_commit_tree_oid(const struct commit *commit)
{
	struct tree *tree = repo_get_commit_tree(the_repository, commit);
	return tree ? &tree->object.oid : NULL;
}

void release_commit_memory(struct parsed_object_pool *pool, struct commit *c)
{
	set_commit_tree(c, NULL);
	free_commit_buffer(pool, c);
	c->index = 0;
	free_commit_list(c->parents);

	c->object.parsed = 0;
}

const void *detach_commit_buffer(struct commit *commit, unsigned long *sizep)
{
	struct commit_buffer *v = buffer_slab_peek(
		the_repository->parsed_objects->buffer_slab, commit);
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

int parse_commit_buffer(struct repository *r, struct commit *item, const void *buffer, unsigned long size, int check_graph)
{
	const char *tail = buffer;
	const char *bufptr = buffer;
	struct object_id parent;
	struct commit_list **pptr;
	struct commit_graft *graft;
	const int tree_entry_len = the_hash_algo->hexsz + 5;
	const int parent_entry_len = the_hash_algo->hexsz + 7;
	struct tree *tree;

	if (item->object.parsed)
		return 0;
	/*
	 * Presumably this is leftover from an earlier failed parse;
	 * clear it out in preparation for us re-parsing (we'll hit the
	 * same error, but that's good, since it lets our caller know
	 * the result cannot be trusted.
	 */
	free_commit_list(item->parents);
	item->parents = NULL;

	tail += size;
	if (tail <= bufptr + tree_entry_len + 1 || memcmp(bufptr, "tree ", 5) ||
			bufptr[tree_entry_len] != '\n')
		return error("bogus commit object %s", oid_to_hex(&item->object.oid));
	if (get_oid_hex(bufptr + 5, &parent) < 0)
		return error("bad tree pointer in commit %s",
			     oid_to_hex(&item->object.oid));
	tree = lookup_tree(r, &parent);
	if (!tree)
		return error("bad tree pointer %s in commit %s",
			     oid_to_hex(&parent),
			     oid_to_hex(&item->object.oid));
	set_commit_tree(item, tree);
	bufptr += tree_entry_len + 1; /* "tree " + "hex sha1" + "\n" */
	pptr = &item->parents;

	graft = lookup_commit_graft(r, &item->object.oid);
	if (graft)
		r->parsed_objects->substituted_parent = 1;
	while (bufptr + parent_entry_len < tail && !memcmp(bufptr, "parent ", 7)) {
		struct commit *new_parent;

		if (tail <= bufptr + parent_entry_len + 1 ||
		    get_oid_hex(bufptr + 7, &parent) ||
		    bufptr[parent_entry_len] != '\n')
			return error("bad parents in commit %s", oid_to_hex(&item->object.oid));
		bufptr += parent_entry_len + 1;
		/*
		 * The clone is shallow if nr_parent < 0, and we must
		 * not traverse its real parents even when we unhide them.
		 */
		if (graft && (graft->nr_parent < 0 || !grafts_keep_true_parents))
			continue;
		new_parent = lookup_commit(r, &parent);
		if (!new_parent)
			return error("bad parent %s in commit %s",
				     oid_to_hex(&parent),
				     oid_to_hex(&item->object.oid));
		pptr = &commit_list_insert(new_parent, pptr)->next;
	}
	if (graft) {
		int i;
		struct commit *new_parent;
		for (i = 0; i < graft->nr_parent; i++) {
			new_parent = lookup_commit(r,
						   &graft->parent[i]);
			if (!new_parent)
				return error("bad graft parent %s in commit %s",
					     oid_to_hex(&graft->parent[i]),
					     oid_to_hex(&item->object.oid));
			pptr = &commit_list_insert(new_parent, pptr)->next;
		}
	}
	item->date = parse_commit_date(bufptr, tail);

	if (check_graph)
		load_commit_graph_info(r, item);

	item->object.parsed = 1;
	return 0;
}

int repo_parse_commit_internal(struct repository *r,
			       struct commit *item,
			       int quiet_on_missing,
			       int use_commit_graph)
{
	enum object_type type;
	void *buffer;
	unsigned long size;
	struct object_info oi = {
		.typep = &type,
		.sizep = &size,
		.contentp = &buffer,
	};
	/*
	 * Git does not support partial clones that exclude commits, so set
	 * OBJECT_INFO_SKIP_FETCH_OBJECT to fail fast when an object is missing.
	 */
	int flags = OBJECT_INFO_LOOKUP_REPLACE | OBJECT_INFO_SKIP_FETCH_OBJECT |
		OBJECT_INFO_DIE_IF_CORRUPT;
	int ret;

	if (!item)
		return -1;
	if (item->object.parsed)
		return 0;
	if (use_commit_graph && parse_commit_in_graph(r, item)) {
		static int commit_graph_paranoia = -1;

		if (commit_graph_paranoia == -1)
			commit_graph_paranoia = git_env_bool(GIT_COMMIT_GRAPH_PARANOIA, 0);

		if (commit_graph_paranoia && !has_object(r, &item->object.oid, 0)) {
			unparse_commit(r, &item->object.oid);
			return quiet_on_missing ? -1 :
				error(_("commit %s exists in commit-graph but not in the object database"),
				      oid_to_hex(&item->object.oid));
		}

		return 0;
	}

	if (oid_object_info_extended(r, &item->object.oid, &oi, flags) < 0)
		return quiet_on_missing ? -1 :
			error("Could not read %s",
			     oid_to_hex(&item->object.oid));
	if (type != OBJ_COMMIT) {
		free(buffer);
		return error("Object %s not a commit",
			     oid_to_hex(&item->object.oid));
	}

	ret = parse_commit_buffer(r, item, buffer, size, 0);
	if (save_commit_buffer && !ret &&
	    !get_cached_commit_buffer(r, item, NULL)) {
		set_commit_buffer(r, item, buffer, size);
		return 0;
	}
	free(buffer);
	return ret;
}

int repo_parse_commit_gently(struct repository *r,
			     struct commit *item, int quiet_on_missing)
{
	return repo_parse_commit_internal(r, item, quiet_on_missing, 1);
}

void parse_commit_or_die(struct commit *item)
{
	if (repo_parse_commit(the_repository, item))
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

size_t commit_subject_length(const char *body)
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

struct commit_list *commit_list_insert(struct commit *item, struct commit_list **list_p)
{
	struct commit_list *new_list = xmalloc(sizeof(struct commit_list));
	new_list->item = item;
	new_list->next = *list_p;
	*list_p = new_list;
	return new_list;
}

int commit_list_contains(struct commit *item, struct commit_list *list)
{
	while (list) {
		if (list->item == item)
			return 1;
		list = list->next;
	}

	return 0;
}

unsigned commit_list_count(const struct commit_list *l)
{
	unsigned c = 0;
	for (; l; l = l->next )
		c++;
	return c;
}

struct commit_list *copy_commit_list(const struct commit_list *list)
{
	struct commit_list *head = NULL;
	struct commit_list **pp = &head;
	while (list) {
		pp = commit_list_append(list->item, pp);
		list = list->next;
	}
	return head;
}

struct commit_list *reverse_commit_list(struct commit_list *list)
{
	struct commit_list *next = NULL, *current, *backup;
	for (current = list; current; current = backup) {
		backup = current->next;
		current->next = next;
		next = current;
	}
	return next;
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

static int commit_list_compare_by_date(const struct commit_list *a,
				       const struct commit_list *b)
{
	timestamp_t a_date = a->item->date;
	timestamp_t b_date = b->item->date;
	if (a_date < b_date)
		return 1;
	if (a_date > b_date)
		return -1;
	return 0;
}

DEFINE_LIST_SORT(static, commit_list_sort, struct commit_list, next);

void commit_list_sort_by_date(struct commit_list **list)
{
	commit_list_sort(list, commit_list_compare_by_date);
}

struct commit *pop_most_recent_commit(struct commit_list **list,
				      unsigned int mark)
{
	struct commit *ret = pop_commit(list);
	struct commit_list *parents = ret->parents;

	while (parents) {
		struct commit *commit = parents->item;
		if (!repo_parse_commit(the_repository, commit) && !(commit->object.flags & mark)) {
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

		while ((parents = parents->next)) {
			if (parents->item->object.flags & mark)
				commit_list_insert(parents->item, plist);
		}

		commit = commit->parents->item;
	}
}

void clear_commit_marks_many(int nr, struct commit **commit, unsigned int mark)
{
	struct commit_list *list = NULL;

	while (nr--) {
		clear_commit_marks_1(&list, *commit, mark);
		commit++;
	}
	while (list)
		clear_commit_marks_1(&list, pop_commit(&list), mark);
}

void clear_commit_marks(struct commit *commit, unsigned int mark)
{
	clear_commit_marks_many(1, &commit, mark);
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

define_commit_slab(author_date_slab, timestamp_t);

void record_author_date(struct author_date_slab *author_date,
			struct commit *commit)
{
	const char *buffer = repo_get_commit_buffer(the_repository, commit,
						    NULL);
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
	*(author_date_slab_at(author_date, commit)) = date;

fail_exit:
	repo_unuse_commit_buffer(the_repository, commit, buffer);
}

int compare_commits_by_author_date(const void *a_, const void *b_,
				   void *cb_data)
{
	const struct commit *a = a_, *b = b_;
	struct author_date_slab *author_date = cb_data;
	timestamp_t a_date = *(author_date_slab_at(author_date, a));
	timestamp_t b_date = *(author_date_slab_at(author_date, b));

	/* newer commits with larger date first */
	if (a_date < b_date)
		return 1;
	else if (a_date > b_date)
		return -1;
	return 0;
}

int compare_commits_by_gen_then_commit_date(const void *a_, const void *b_,
					    void *unused UNUSED)
{
	const struct commit *a = a_, *b = b_;
	const timestamp_t generation_a = commit_graph_generation(a),
			  generation_b = commit_graph_generation(b);

	/* newer commits first */
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

int compare_commits_by_commit_date(const void *a_, const void *b_,
				   void *unused UNUSED)
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

struct rev_collect {
	struct commit **commit;
	int nr;
	int alloc;
	unsigned int initial : 1;
};

static void add_one_commit(struct object_id *oid, struct rev_collect *revs)
{
	struct commit *commit;

	if (is_null_oid(oid))
		return;

	commit = lookup_commit(the_repository, oid);
	if (!commit ||
	    (commit->object.flags & TMP_MARK) ||
	    repo_parse_commit(the_repository, commit))
		return;

	ALLOC_GROW(revs->commit, revs->nr + 1, revs->alloc);
	revs->commit[revs->nr++] = commit;
	commit->object.flags |= TMP_MARK;
}

static int collect_one_reflog_ent(struct object_id *ooid, struct object_id *noid,
				  const char *ident UNUSED,
				  timestamp_t timestamp UNUSED, int tz UNUSED,
				  const char *message UNUSED, void *cbdata)
{
	struct rev_collect *revs = cbdata;

	if (revs->initial) {
		revs->initial = 0;
		add_one_commit(ooid, revs);
	}
	add_one_commit(noid, revs);
	return 0;
}

struct commit *get_fork_point(const char *refname, struct commit *commit)
{
	struct object_id oid;
	struct rev_collect revs;
	struct commit_list *bases = NULL;
	int i;
	struct commit *ret = NULL;
	char *full_refname;

	switch (repo_dwim_ref(the_repository, refname, strlen(refname), &oid,
			      &full_refname, 0)) {
	case 0:
		die("No such ref: '%s'", refname);
	case 1:
		break; /* good */
	default:
		die("Ambiguous refname: '%s'", refname);
	}

	memset(&revs, 0, sizeof(revs));
	revs.initial = 1;
	refs_for_each_reflog_ent(get_main_ref_store(the_repository),
				 full_refname, collect_one_reflog_ent, &revs);

	if (!revs.nr)
		add_one_commit(&oid, &revs);

	for (i = 0; i < revs.nr; i++)
		revs.commit[i]->object.flags &= ~TMP_MARK;

	if (repo_get_merge_bases_many(the_repository, commit, revs.nr,
				      revs.commit, &bases) < 0)
		exit(128);

	/*
	 * There should be one and only one merge base, when we found
	 * a common ancestor among reflog entries.
	 */
	if (!bases || bases->next)
		goto cleanup_return;

	/* And the found one must be one of the reflog entries */
	for (i = 0; i < revs.nr; i++)
		if (&bases->item->object == &revs.commit[i]->object)
			break; /* found */
	if (revs.nr <= i)
		goto cleanup_return;

	ret = bases->item;

cleanup_return:
	free(revs.commit);
	free_commit_list(bases);
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

int add_header_signature(struct strbuf *buf, struct strbuf *sig, const struct git_hash_algo *algo)
{
	int inspos, copypos;
	const char *eoh;
	const char *gpg_sig_header = gpg_sig_headers[hash_algo_by_ptr(algo)];
	int gpg_sig_header_len = strlen(gpg_sig_header);

	/* find the end of the header */
	eoh = strstr(buf->buf, "\n\n");
	if (!eoh)
		inspos = buf->len;
	else
		inspos = eoh - buf->buf + 1;

	for (copypos = 0; sig->buf[copypos]; ) {
		const char *bol = sig->buf + copypos;
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
	return 0;
}

static int sign_commit_to_strbuf(struct strbuf *sig, struct strbuf *buf, const char *keyid)
{
	char *keyid_to_free = NULL;
	int ret = 0;
	if (!keyid || !*keyid)
		keyid = keyid_to_free = get_signing_key();
	if (sign_buffer(buf, sig, keyid))
		ret = -1;
	free(keyid_to_free);
	return ret;
}

int parse_signed_commit(const struct commit *commit,
			struct strbuf *payload, struct strbuf *signature,
			const struct git_hash_algo *algop)
{
	unsigned long size;
	const char *buffer = repo_get_commit_buffer(the_repository, commit,
						    &size);
	int ret = parse_buffer_signed_by_header(buffer, size, payload, signature, algop);

	repo_unuse_commit_buffer(the_repository, commit, buffer);
	return ret;
}

int parse_buffer_signed_by_header(const char *buffer,
				  unsigned long size,
				  struct strbuf *payload,
				  struct strbuf *signature,
				  const struct git_hash_algo *algop)
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
			for (i = 1; i < GIT_HASH_NALGOS; i++) {
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

static void handle_signed_tag(const struct commit *parent, struct commit_extra_header ***tail)
{
	struct merge_remote_desc *desc;
	struct commit_extra_header *mergetag;
	char *buf;
	unsigned long size;
	enum object_type type;
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;

	desc = merge_remote_util(parent);
	if (!desc || !desc->obj)
		return;
	buf = repo_read_object_file(the_repository, &desc->obj->oid, &type,
				    &size);
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

int check_commit_signature(const struct commit *commit, struct signature_check *sigc)
{
	struct strbuf payload = STRBUF_INIT;
	struct strbuf signature = STRBUF_INIT;
	int ret = 1;

	sigc->result = 'N';

	if (parse_signed_commit(commit, &payload, &signature, the_hash_algo) <= 0)
		goto out;

	sigc->payload_type = SIGNATURE_PAYLOAD_COMMIT;
	sigc->payload = strbuf_detach(&payload, &sigc->payload_len);
	ret = check_signature(sigc, signature.buf, signature.len);

 out:
	strbuf_release(&payload);
	strbuf_release(&signature);

	return ret;
}

void verify_merge_signature(struct commit *commit, int verbosity,
			    int check_trust)
{
	char hex[GIT_MAX_HEXSZ + 1];
	struct signature_check signature_check;
	int ret;
	memset(&signature_check, 0, sizeof(signature_check));

	ret = check_commit_signature(commit, &signature_check);

	repo_find_unique_abbrev_r(the_repository, hex, &commit->object.oid,
				  DEFAULT_ABBREV);
	switch (signature_check.result) {
	case 'G':
		if (ret || (check_trust && signature_check.trust_level < TRUST_MARGINAL))
			die(_("Commit %s has an untrusted GPG signature, "
			      "allegedly by %s."), hex, signature_check.signer);
		break;
	case 'B':
		die(_("Commit %s has a bad GPG signature "
		      "allegedly by %s."), hex, signature_check.signer);
	default: /* 'N' */
		die(_("Commit %s does not have a GPG signature."), hex);
	}
	if (verbosity >= 0 && signature_check.result == 'G')
		printf(_("Commit %s has a good GPG signature by %s\n"),
		       hex, signature_check.signer);

	signature_check_clear(&signature_check);
}

void append_merge_tag_headers(const struct commit_list *parents,
			      struct commit_extra_header ***tail)
{
	while (parents) {
		const struct commit *parent = parents->item;
		handle_signed_tag(parent, tail);
		parents = parents->next;
	}
}

static int convert_commit_extra_headers(const struct commit_extra_header *orig,
					struct commit_extra_header **result)
{
	const struct git_hash_algo *compat = the_repository->compat_hash_algo;
	const struct git_hash_algo *algo = the_repository->hash_algo;
	struct commit_extra_header *extra = NULL, **tail = &extra;
	struct strbuf out = STRBUF_INIT;
	while (orig) {
		struct commit_extra_header *new;
		CALLOC_ARRAY(new, 1);
		if (!strcmp(orig->key, "mergetag")) {
			if (convert_object_file(&out, algo, compat,
						orig->value, orig->len,
						OBJ_TAG, 1)) {
				free(new);
				free_commit_extra_headers(extra);
				return -1;
			}
			new->key = xstrdup("mergetag");
			new->value = strbuf_detach(&out, &new->len);
		} else {
			new->key = xstrdup(orig->key);
			new->len = orig->len;
			new->value = xmemdupz(orig->value, orig->len);
		}
		*tail = new;
		tail = &new->next;
		orig = orig->next;
	}
	*result = extra;
	return 0;
}

static void add_extra_header(struct strbuf *buffer,
			     const struct commit_extra_header *extra)
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
	const char *buffer = repo_get_commit_buffer(the_repository, commit,
						    &size);
	extra = read_commit_extra_header_lines(buffer, size, exclude);
	repo_unuse_commit_buffer(the_repository, commit, buffer);
	return extra;
}

int for_each_mergetag(each_mergetag_fn fn, struct commit *commit, void *data)
{
	struct commit_extra_header *extra, *to_free;
	int res = 0;

	to_free = read_commit_extra_headers(commit, NULL);
	for (extra = to_free; !res && extra; extra = extra->next) {
		if (strcmp(extra->key, "mergetag"))
			continue; /* not a merge tag */
		res = fn(commit, extra, data);
	}
	free_commit_extra_headers(to_free);
	return res;
}

static inline int standard_header_field(const char *field, size_t len)
{
	return ((len == 4 && !memcmp(field, "tree", 4)) ||
		(len == 6 && !memcmp(field, "parent", 6)) ||
		(len == 6 && !memcmp(field, "author", 6)) ||
		(len == 9 && !memcmp(field, "committer", 9)) ||
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

int commit_tree(const char *msg, size_t msg_len, const struct object_id *tree,
		const struct commit_list *parents, struct object_id *ret,
		const char *author, const char *sign_commit)
{
	struct commit_extra_header *extra = NULL, **tail = &extra;
	int result;

	append_merge_tag_headers(parents, &tail);
	result = commit_tree_extended(msg, msg_len, tree, parents, ret, author,
				      NULL, sign_commit, extra);
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
   "variable i18n.commitEncoding to the encoding your project uses.\n");

static void write_commit_tree(struct strbuf *buffer, const char *msg, size_t msg_len,
			      const struct object_id *tree,
			      const struct object_id *parents, size_t parents_len,
			      const char *author, const char *committer,
			      const struct commit_extra_header *extra)
{
	int encoding_is_utf8;
	size_t i;

	/* Not having i18n.commitencoding is the same as having utf-8 */
	encoding_is_utf8 = is_encoding_utf8(git_commit_encoding);

	strbuf_grow(buffer, 8192); /* should avoid reallocs for the headers */
	strbuf_addf(buffer, "tree %s\n", oid_to_hex(tree));

	/*
	 * NOTE! This ordering means that the same exact tree merged with a
	 * different order of parents will be a _different_ changeset even
	 * if everything else stays the same.
	 */
	for (i = 0; i < parents_len; i++)
		strbuf_addf(buffer, "parent %s\n", oid_to_hex(&parents[i]));

	/* Person/date information */
	if (!author)
		author = git_author_info(IDENT_STRICT);
	strbuf_addf(buffer, "author %s\n", author);
	if (!committer)
		committer = git_committer_info(IDENT_STRICT);
	strbuf_addf(buffer, "committer %s\n", committer);
	if (!encoding_is_utf8)
		strbuf_addf(buffer, "encoding %s\n", git_commit_encoding);

	while (extra) {
		add_extra_header(buffer, extra);
		extra = extra->next;
	}
	strbuf_addch(buffer, '\n');

	/* And add the comment */
	strbuf_add(buffer, msg, msg_len);
}

int commit_tree_extended(const char *msg, size_t msg_len,
			 const struct object_id *tree,
			 const struct commit_list *parents, struct object_id *ret,
			 const char *author, const char *committer,
			 const char *sign_commit,
			 const struct commit_extra_header *extra)
{
	struct repository *r = the_repository;
	int result = 0;
	int encoding_is_utf8;
	struct strbuf buffer = STRBUF_INIT, compat_buffer = STRBUF_INIT;
	struct strbuf sig = STRBUF_INIT, compat_sig = STRBUF_INIT;
	struct object_id *parent_buf = NULL, *compat_oid = NULL;
	struct object_id compat_oid_buf;
	size_t i, nparents;

	/* Not having i18n.commitencoding is the same as having utf-8 */
	encoding_is_utf8 = is_encoding_utf8(git_commit_encoding);

	assert_oid_type(tree, OBJ_TREE);

	if (memchr(msg, '\0', msg_len))
		return error("a NUL byte in commit log message not allowed.");

	nparents = commit_list_count(parents);
	CALLOC_ARRAY(parent_buf, nparents);
	i = 0;
	for (const struct commit_list *p = parents; p; p = p->next)
		oidcpy(&parent_buf[i++], &p->item->object.oid);

	write_commit_tree(&buffer, msg, msg_len, tree, parent_buf, nparents, author, committer, extra);
	if (sign_commit && sign_commit_to_strbuf(&sig, &buffer, sign_commit)) {
		result = -1;
		goto out;
	}
	if (r->compat_hash_algo) {
		struct commit_extra_header *compat_extra = NULL;
		struct object_id mapped_tree;
		struct object_id *mapped_parents;

		CALLOC_ARRAY(mapped_parents, nparents);

		if (repo_oid_to_algop(r, tree, r->compat_hash_algo, &mapped_tree)) {
			result = -1;
			free(mapped_parents);
			goto out;
		}
		for (i = 0; i < nparents; i++)
			if (repo_oid_to_algop(r, &parent_buf[i], r->compat_hash_algo, &mapped_parents[i])) {
				result = -1;
				free(mapped_parents);
				goto out;
			}
		if (convert_commit_extra_headers(extra, &compat_extra)) {
			result = -1;
			free(mapped_parents);
			goto out;
		}
		write_commit_tree(&compat_buffer, msg, msg_len, &mapped_tree,
				  mapped_parents, nparents, author, committer, compat_extra);
		free_commit_extra_headers(compat_extra);
		free(mapped_parents);

		if (sign_commit && sign_commit_to_strbuf(&compat_sig, &compat_buffer, sign_commit)) {
			result = -1;
			goto out;
		}
	}

	if (sign_commit) {
		struct sig_pairs {
			struct strbuf *sig;
			const struct git_hash_algo *algo;
		} bufs [2] = {
			{ &compat_sig, r->compat_hash_algo },
			{ &sig, r->hash_algo },
		};

		/*
		 * We write algorithms in the order they were implemented in
		 * Git to produce a stable hash when multiple algorithms are
		 * used.
		 */
		if (r->compat_hash_algo && hash_algo_by_ptr(bufs[0].algo) > hash_algo_by_ptr(bufs[1].algo))
			SWAP(bufs[0], bufs[1]);

		/*
		 * We traverse each algorithm in order, and apply the signature
		 * to each buffer.
		 */
		for (size_t i = 0; i < ARRAY_SIZE(bufs); i++) {
			if (!bufs[i].algo)
				continue;
			add_header_signature(&buffer, bufs[i].sig, bufs[i].algo);
			if (r->compat_hash_algo)
				add_header_signature(&compat_buffer, bufs[i].sig, bufs[i].algo);
		}
	}

	/* And check the encoding. */
	if (encoding_is_utf8 && (!verify_utf8(&buffer) || !verify_utf8(&compat_buffer)))
		fprintf(stderr, _(commit_utf8_warn));

	if (r->compat_hash_algo) {
		hash_object_file(r->compat_hash_algo, compat_buffer.buf, compat_buffer.len,
			OBJ_COMMIT, &compat_oid_buf);
		compat_oid = &compat_oid_buf;
	}

	result = write_object_file_flags(buffer.buf, buffer.len, OBJ_COMMIT,
					 ret, compat_oid, 0);
out:
	free(parent_buf);
	strbuf_release(&buffer);
	strbuf_release(&compat_buffer);
	strbuf_release(&sig);
	strbuf_release(&compat_sig);
	return result;
}

define_commit_slab(merge_desc_slab, struct merge_remote_desc *);
static struct merge_desc_slab merge_desc_slab = COMMIT_SLAB_INIT(1, merge_desc_slab);

struct merge_remote_desc *merge_remote_util(const struct commit *commit)
{
	return *merge_desc_slab_at(&merge_desc_slab, commit);
}

void set_merge_remote_desc(struct commit *commit,
			   const char *name, struct object *obj)
{
	struct merge_remote_desc *desc;
	FLEX_ALLOC_STR(desc, name, name);
	desc->obj = obj;
	*merge_desc_slab_at(&merge_desc_slab, commit) = desc;
}

struct commit *get_merge_parent(const char *name)
{
	struct object *obj;
	struct commit *commit;
	struct object_id oid;
	if (repo_get_oid(the_repository, name, &oid))
		return NULL;
	obj = parse_object(the_repository, &oid);
	commit = (struct commit *)repo_peel_to_type(the_repository, name, 0,
						    obj, OBJ_COMMIT);
	if (commit && !merge_remote_util(commit))
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
	struct commit_list *new_commit = xmalloc(sizeof(struct commit_list));
	new_commit->item = commit;
	*next = new_commit;
	new_commit->next = NULL;
	return &new_commit->next;
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
 * order to find where to put a new Signed-off-by trailer.  Ignored are
 * trailing comment lines and blank lines.  To support "git commit -s
 * --amend" on an existing commit, we also ignore "Conflicts:".  To
 * support "git commit -v", we truncate at cut lines.
 *
 * Returns the number of bytes from the tail to ignore, to be fed as
 * the second parameter to append_signoff().
 */
size_t ignored_log_message_bytes(const char *buf, size_t len)
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

		if (starts_with_mem(buf + bol, cutoff - bol, comment_line_str) ||
		    buf[bol] == '\n') {
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

	strvec_pushf(&opt.env, "GIT_INDEX_FILE=%s", index_file);

	/*
	 * Let the hook know that no editor will be launched.
	 */
	if (!editor_is_used)
		strvec_push(&opt.env, "GIT_EDITOR=:");

	va_start(args, name);
	while ((arg = va_arg(args, const char *)))
		strvec_push(&opt.args, arg);
	va_end(args);

	opt.invoked_hook = invoked_hook;
	return run_hooks_opt(the_repository, name, &opt);
}
