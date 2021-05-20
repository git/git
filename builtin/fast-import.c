#include "builtin.h"
#include "cache.h"
#include "repository.h"
#include "config.h"
#include "lockfile.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "delta.h"
#include "pack.h"
#include "refs.h"
#include "csum-file.h"
#include "quote.h"
#include "dir.h"
#include "run-command.h"
#include "packfile.h"
#include "object-store.h"
#include "mem-pool.h"
#include "commit-reach.h"
#include "khash.h"

#define PACK_ID_BITS 16
#define MAX_PACK_ID ((1<<PACK_ID_BITS)-1)
#define DEPTH_BITS 13
#define MAX_DEPTH ((1<<DEPTH_BITS)-1)

/*
 * We abuse the setuid bit on directories to mean "do not delta".
 */
#define NO_DELTA S_ISUID

/*
 * The amount of additional space required in order to write an object into the
 * current pack. This is the hash lengths at the end of the pack, plus the
 * length of one object ID.
 */
#define PACK_SIZE_THRESHOLD (the_hash_algo->rawsz * 3)

struct object_entry {
	struct pack_idx_entry idx;
	struct hashmap_entry ent;
	uint32_t type : TYPE_BITS,
		pack_id : PACK_ID_BITS,
		depth : DEPTH_BITS;
};

static int object_entry_hashcmp(const void *map_data,
				const struct hashmap_entry *eptr,
				const struct hashmap_entry *entry_or_key,
				const void *keydata)
{
	const struct object_id *oid = keydata;
	const struct object_entry *e1, *e2;

	e1 = container_of(eptr, const struct object_entry, ent);
	if (oid)
		return oidcmp(&e1->idx.oid, oid);

	e2 = container_of(entry_or_key, const struct object_entry, ent);
	return oidcmp(&e1->idx.oid, &e2->idx.oid);
}

struct object_entry_pool {
	struct object_entry_pool *next_pool;
	struct object_entry *next_free;
	struct object_entry *end;
	struct object_entry entries[FLEX_ARRAY]; /* more */
};

struct mark_set {
	union {
		struct object_id *oids[1024];
		struct object_entry *marked[1024];
		struct mark_set *sets[1024];
	} data;
	unsigned int shift;
};

struct last_object {
	struct strbuf data;
	off_t offset;
	unsigned int depth;
	unsigned no_swap : 1;
};

struct atom_str {
	struct atom_str *next_atom;
	unsigned short str_len;
	char str_dat[FLEX_ARRAY]; /* more */
};

struct tree_content;
struct tree_entry {
	struct tree_content *tree;
	struct atom_str *name;
	struct tree_entry_ms {
		uint16_t mode;
		struct object_id oid;
	} versions[2];
};

struct tree_content {
	unsigned int entry_capacity; /* must match avail_tree_content */
	unsigned int entry_count;
	unsigned int delta_depth;
	struct tree_entry *entries[FLEX_ARRAY]; /* more */
};

struct avail_tree_content {
	unsigned int entry_capacity; /* must match tree_content */
	struct avail_tree_content *next_avail;
};

struct branch {
	struct branch *table_next_branch;
	struct branch *active_next_branch;
	const char *name;
	struct tree_entry branch_tree;
	uintmax_t last_commit;
	uintmax_t num_notes;
	unsigned active : 1;
	unsigned delete : 1;
	unsigned pack_id : PACK_ID_BITS;
	struct object_id oid;
};

struct tag {
	struct tag *next_tag;
	const char *name;
	unsigned int pack_id;
	struct object_id oid;
};

struct hash_list {
	struct hash_list *next;
	struct object_id oid;
};

typedef enum {
	WHENSPEC_RAW = 1,
	WHENSPEC_RAW_PERMISSIVE,
	WHENSPEC_RFC2822,
	WHENSPEC_NOW
} whenspec_type;

struct recent_command {
	struct recent_command *prev;
	struct recent_command *next;
	char *buf;
};

typedef void (*mark_set_inserter_t)(struct mark_set **s, struct object_id *oid, uintmax_t mark);
typedef void (*each_mark_fn_t)(uintmax_t mark, void *obj, void *cbp);

/* Configured limits on output */
static unsigned long max_depth = 50;
static off_t max_packsize;
static int unpack_limit = 100;
static int force_update;

/* Stats and misc. counters */
static uintmax_t alloc_count;
static uintmax_t marks_set_count;
static uintmax_t object_count_by_type[1 << TYPE_BITS];
static uintmax_t duplicate_count_by_type[1 << TYPE_BITS];
static uintmax_t delta_count_by_type[1 << TYPE_BITS];
static uintmax_t delta_count_attempts_by_type[1 << TYPE_BITS];
static unsigned long object_count;
static unsigned long branch_count;
static unsigned long branch_load_count;
static int failure;
static FILE *pack_edges;
static unsigned int show_stats = 1;
static int global_argc;
static const char **global_argv;

/* Memory pools */
static struct mem_pool fi_mem_pool =  {NULL, 2*1024*1024 -
				       sizeof(struct mp_block), 0 };

/* Atom management */
static unsigned int atom_table_sz = 4451;
static unsigned int atom_cnt;
static struct atom_str **atom_table;

/* The .pack file being generated */
static struct pack_idx_option pack_idx_opts;
static unsigned int pack_id;
static struct hashfile *pack_file;
static struct packed_git *pack_data;
static struct packed_git **all_packs;
static off_t pack_size;

/* Table of objects we've written. */
static unsigned int object_entry_alloc = 5000;
static struct object_entry_pool *blocks;
static struct hashmap object_table;
static struct mark_set *marks;
static const char *export_marks_file;
static const char *import_marks_file;
static int import_marks_file_from_stream;
static int import_marks_file_ignore_missing;
static int import_marks_file_done;
static int relative_marks_paths;

/* Our last blob */
static struct last_object last_blob = { STRBUF_INIT, 0, 0, 0 };

/* Tree management */
static unsigned int tree_entry_alloc = 1000;
static void *avail_tree_entry;
static unsigned int avail_tree_table_sz = 100;
static struct avail_tree_content **avail_tree_table;
static size_t tree_entry_allocd;
static struct strbuf old_tree = STRBUF_INIT;
static struct strbuf new_tree = STRBUF_INIT;

/* Branch data */
static unsigned long max_active_branches = 5;
static unsigned long cur_active_branches;
static unsigned long branch_table_sz = 1039;
static struct branch **branch_table;
static struct branch *active_branches;

/* Tag data */
static struct tag *first_tag;
static struct tag *last_tag;

/* Input stream parsing */
static whenspec_type whenspec = WHENSPEC_RAW;
static struct strbuf command_buf = STRBUF_INIT;
static int unread_command_buf;
static struct recent_command cmd_hist = {&cmd_hist, &cmd_hist, NULL};
static struct recent_command *cmd_tail = &cmd_hist;
static struct recent_command *rc_free;
static unsigned int cmd_save = 100;
static uintmax_t next_mark;
static struct strbuf new_data = STRBUF_INIT;
static int seen_data_command;
static int require_explicit_termination;
static int allow_unsafe_features;

/* Signal handling */
static volatile sig_atomic_t checkpoint_requested;

/* Submodule marks */
static struct string_list sub_marks_from = STRING_LIST_INIT_DUP;
static struct string_list sub_marks_to = STRING_LIST_INIT_DUP;
static kh_oid_map_t *sub_oid_map;

/* Where to write output of cat-blob commands */
static int cat_blob_fd = STDOUT_FILENO;

static void parse_argv(void);
static void parse_get_mark(const char *p);
static void parse_cat_blob(const char *p);
static void parse_ls(const char *p, struct branch *b);

static void for_each_mark(struct mark_set *m, uintmax_t base, each_mark_fn_t callback, void *p)
{
	uintmax_t k;
	if (m->shift) {
		for (k = 0; k < 1024; k++) {
			if (m->data.sets[k])
				for_each_mark(m->data.sets[k], base + (k << m->shift), callback, p);
		}
	} else {
		for (k = 0; k < 1024; k++) {
			if (m->data.marked[k])
				callback(base + k, m->data.marked[k], p);
		}
	}
}

static void dump_marks_fn(uintmax_t mark, void *object, void *cbp) {
	struct object_entry *e = object;
	FILE *f = cbp;

	fprintf(f, ":%" PRIuMAX " %s\n", mark, oid_to_hex(&e->idx.oid));
}

static void write_branch_report(FILE *rpt, struct branch *b)
{
	fprintf(rpt, "%s:\n", b->name);

	fprintf(rpt, "  status      :");
	if (b->active)
		fputs(" active", rpt);
	if (b->branch_tree.tree)
		fputs(" loaded", rpt);
	if (is_null_oid(&b->branch_tree.versions[1].oid))
		fputs(" dirty", rpt);
	fputc('\n', rpt);

	fprintf(rpt, "  tip commit  : %s\n", oid_to_hex(&b->oid));
	fprintf(rpt, "  old tree    : %s\n",
		oid_to_hex(&b->branch_tree.versions[0].oid));
	fprintf(rpt, "  cur tree    : %s\n",
		oid_to_hex(&b->branch_tree.versions[1].oid));
	fprintf(rpt, "  commit clock: %" PRIuMAX "\n", b->last_commit);

	fputs("  last pack   : ", rpt);
	if (b->pack_id < MAX_PACK_ID)
		fprintf(rpt, "%u", b->pack_id);
	fputc('\n', rpt);

	fputc('\n', rpt);
}

static void write_crash_report(const char *err)
{
	char *loc = git_pathdup("fast_import_crash_%"PRIuMAX, (uintmax_t) getpid());
	FILE *rpt = fopen(loc, "w");
	struct branch *b;
	unsigned long lu;
	struct recent_command *rc;

	if (!rpt) {
		error_errno("can't write crash report %s", loc);
		free(loc);
		return;
	}

	fprintf(stderr, "fast-import: dumping crash report to %s\n", loc);

	fprintf(rpt, "fast-import crash report:\n");
	fprintf(rpt, "    fast-import process: %"PRIuMAX"\n", (uintmax_t) getpid());
	fprintf(rpt, "    parent process     : %"PRIuMAX"\n", (uintmax_t) getppid());
	fprintf(rpt, "    at %s\n", show_date(time(NULL), 0, DATE_MODE(ISO8601)));
	fputc('\n', rpt);

	fputs("fatal: ", rpt);
	fputs(err, rpt);
	fputc('\n', rpt);

	fputc('\n', rpt);
	fputs("Most Recent Commands Before Crash\n", rpt);
	fputs("---------------------------------\n", rpt);
	for (rc = cmd_hist.next; rc != &cmd_hist; rc = rc->next) {
		if (rc->next == &cmd_hist)
			fputs("* ", rpt);
		else
			fputs("  ", rpt);
		fputs(rc->buf, rpt);
		fputc('\n', rpt);
	}

	fputc('\n', rpt);
	fputs("Active Branch LRU\n", rpt);
	fputs("-----------------\n", rpt);
	fprintf(rpt, "    active_branches = %lu cur, %lu max\n",
		cur_active_branches,
		max_active_branches);
	fputc('\n', rpt);
	fputs("  pos  clock name\n", rpt);
	fputs("  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n", rpt);
	for (b = active_branches, lu = 0; b; b = b->active_next_branch)
		fprintf(rpt, "  %2lu) %6" PRIuMAX" %s\n",
			++lu, b->last_commit, b->name);

	fputc('\n', rpt);
	fputs("Inactive Branches\n", rpt);
	fputs("-----------------\n", rpt);
	for (lu = 0; lu < branch_table_sz; lu++) {
		for (b = branch_table[lu]; b; b = b->table_next_branch)
			write_branch_report(rpt, b);
	}

	if (first_tag) {
		struct tag *tg;
		fputc('\n', rpt);
		fputs("Annotated Tags\n", rpt);
		fputs("--------------\n", rpt);
		for (tg = first_tag; tg; tg = tg->next_tag) {
			fputs(oid_to_hex(&tg->oid), rpt);
			fputc(' ', rpt);
			fputs(tg->name, rpt);
			fputc('\n', rpt);
		}
	}

	fputc('\n', rpt);
	fputs("Marks\n", rpt);
	fputs("-----\n", rpt);
	if (export_marks_file)
		fprintf(rpt, "  exported to %s\n", export_marks_file);
	else
		for_each_mark(marks, 0, dump_marks_fn, rpt);

	fputc('\n', rpt);
	fputs("-------------------\n", rpt);
	fputs("END OF CRASH REPORT\n", rpt);
	fclose(rpt);
	free(loc);
}

static void end_packfile(void);
static void unkeep_all_packs(void);
static void dump_marks(void);

static NORETURN void die_nicely(const char *err, va_list params)
{
	static int zombie;
	char message[2 * PATH_MAX];

	vsnprintf(message, sizeof(message), err, params);
	fputs("fatal: ", stderr);
	fputs(message, stderr);
	fputc('\n', stderr);

	if (!zombie) {
		zombie = 1;
		write_crash_report(message);
		end_packfile();
		unkeep_all_packs();
		dump_marks();
	}
	exit(128);
}

#ifndef SIGUSR1	/* Windows, for example */

static void set_checkpoint_signal(void)
{
}

#else

static void checkpoint_signal(int signo)
{
	checkpoint_requested = 1;
}

static void set_checkpoint_signal(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = checkpoint_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGUSR1, &sa, NULL);
}

#endif

static void alloc_objects(unsigned int cnt)
{
	struct object_entry_pool *b;

	b = xmalloc(sizeof(struct object_entry_pool)
		+ cnt * sizeof(struct object_entry));
	b->next_pool = blocks;
	b->next_free = b->entries;
	b->end = b->entries + cnt;
	blocks = b;
	alloc_count += cnt;
}

static struct object_entry *new_object(struct object_id *oid)
{
	struct object_entry *e;

	if (blocks->next_free == blocks->end)
		alloc_objects(object_entry_alloc);

	e = blocks->next_free++;
	oidcpy(&e->idx.oid, oid);
	return e;
}

static struct object_entry *find_object(struct object_id *oid)
{
	return hashmap_get_entry_from_hash(&object_table, oidhash(oid), oid,
					   struct object_entry, ent);
}

static struct object_entry *insert_object(struct object_id *oid)
{
	struct object_entry *e;
	unsigned int hash = oidhash(oid);

	e = hashmap_get_entry_from_hash(&object_table, hash, oid,
					struct object_entry, ent);
	if (!e) {
		e = new_object(oid);
		e->idx.offset = 0;
		hashmap_entry_init(&e->ent, hash);
		hashmap_add(&object_table, &e->ent);
	}

	return e;
}

static void invalidate_pack_id(unsigned int id)
{
	unsigned long lu;
	struct tag *t;
	struct hashmap_iter iter;
	struct object_entry *e;

	hashmap_for_each_entry(&object_table, &iter, e, ent) {
		if (e->pack_id == id)
			e->pack_id = MAX_PACK_ID;
	}

	for (lu = 0; lu < branch_table_sz; lu++) {
		struct branch *b;

		for (b = branch_table[lu]; b; b = b->table_next_branch)
			if (b->pack_id == id)
				b->pack_id = MAX_PACK_ID;
	}

	for (t = first_tag; t; t = t->next_tag)
		if (t->pack_id == id)
			t->pack_id = MAX_PACK_ID;
}

static unsigned int hc_str(const char *s, size_t len)
{
	unsigned int r = 0;
	while (len-- > 0)
		r = r * 31 + *s++;
	return r;
}

static void insert_mark(struct mark_set **top, uintmax_t idnum, struct object_entry *oe)
{
	struct mark_set *s = *top;

	while ((idnum >> s->shift) >= 1024) {
		s = mem_pool_calloc(&fi_mem_pool, 1, sizeof(struct mark_set));
		s->shift = (*top)->shift + 10;
		s->data.sets[0] = *top;
		*top = s;
	}
	while (s->shift) {
		uintmax_t i = idnum >> s->shift;
		idnum -= i << s->shift;
		if (!s->data.sets[i]) {
			s->data.sets[i] = mem_pool_calloc(&fi_mem_pool, 1, sizeof(struct mark_set));
			s->data.sets[i]->shift = s->shift - 10;
		}
		s = s->data.sets[i];
	}
	if (!s->data.marked[idnum])
		marks_set_count++;
	s->data.marked[idnum] = oe;
}

static void *find_mark(struct mark_set *s, uintmax_t idnum)
{
	uintmax_t orig_idnum = idnum;
	struct object_entry *oe = NULL;
	if ((idnum >> s->shift) < 1024) {
		while (s && s->shift) {
			uintmax_t i = idnum >> s->shift;
			idnum -= i << s->shift;
			s = s->data.sets[i];
		}
		if (s)
			oe = s->data.marked[idnum];
	}
	if (!oe)
		die("mark :%" PRIuMAX " not declared", orig_idnum);
	return oe;
}

static struct atom_str *to_atom(const char *s, unsigned short len)
{
	unsigned int hc = hc_str(s, len) % atom_table_sz;
	struct atom_str *c;

	for (c = atom_table[hc]; c; c = c->next_atom)
		if (c->str_len == len && !strncmp(s, c->str_dat, len))
			return c;

	c = mem_pool_alloc(&fi_mem_pool, sizeof(struct atom_str) + len + 1);
	c->str_len = len;
	memcpy(c->str_dat, s, len);
	c->str_dat[len] = 0;
	c->next_atom = atom_table[hc];
	atom_table[hc] = c;
	atom_cnt++;
	return c;
}

static struct branch *lookup_branch(const char *name)
{
	unsigned int hc = hc_str(name, strlen(name)) % branch_table_sz;
	struct branch *b;

	for (b = branch_table[hc]; b; b = b->table_next_branch)
		if (!strcmp(name, b->name))
			return b;
	return NULL;
}

static struct branch *new_branch(const char *name)
{
	unsigned int hc = hc_str(name, strlen(name)) % branch_table_sz;
	struct branch *b = lookup_branch(name);

	if (b)
		die("Invalid attempt to create duplicate branch: %s", name);
	if (check_refname_format(name, REFNAME_ALLOW_ONELEVEL))
		die("Branch name doesn't conform to GIT standards: %s", name);

	b = mem_pool_calloc(&fi_mem_pool, 1, sizeof(struct branch));
	b->name = mem_pool_strdup(&fi_mem_pool, name);
	b->table_next_branch = branch_table[hc];
	b->branch_tree.versions[0].mode = S_IFDIR;
	b->branch_tree.versions[1].mode = S_IFDIR;
	b->num_notes = 0;
	b->active = 0;
	b->pack_id = MAX_PACK_ID;
	branch_table[hc] = b;
	branch_count++;
	return b;
}

static unsigned int hc_entries(unsigned int cnt)
{
	cnt = cnt & 7 ? (cnt / 8) + 1 : cnt / 8;
	return cnt < avail_tree_table_sz ? cnt : avail_tree_table_sz - 1;
}

static struct tree_content *new_tree_content(unsigned int cnt)
{
	struct avail_tree_content *f, *l = NULL;
	struct tree_content *t;
	unsigned int hc = hc_entries(cnt);

	for (f = avail_tree_table[hc]; f; l = f, f = f->next_avail)
		if (f->entry_capacity >= cnt)
			break;

	if (f) {
		if (l)
			l->next_avail = f->next_avail;
		else
			avail_tree_table[hc] = f->next_avail;
	} else {
		cnt = cnt & 7 ? ((cnt / 8) + 1) * 8 : cnt;
		f = mem_pool_alloc(&fi_mem_pool, sizeof(*t) + sizeof(t->entries[0]) * cnt);
		f->entry_capacity = cnt;
	}

	t = (struct tree_content*)f;
	t->entry_count = 0;
	t->delta_depth = 0;
	return t;
}

static void release_tree_entry(struct tree_entry *e);
static void release_tree_content(struct tree_content *t)
{
	struct avail_tree_content *f = (struct avail_tree_content*)t;
	unsigned int hc = hc_entries(f->entry_capacity);
	f->next_avail = avail_tree_table[hc];
	avail_tree_table[hc] = f;
}

static void release_tree_content_recursive(struct tree_content *t)
{
	unsigned int i;
	for (i = 0; i < t->entry_count; i++)
		release_tree_entry(t->entries[i]);
	release_tree_content(t);
}

static struct tree_content *grow_tree_content(
	struct tree_content *t,
	int amt)
{
	struct tree_content *r = new_tree_content(t->entry_count + amt);
	r->entry_count = t->entry_count;
	r->delta_depth = t->delta_depth;
	COPY_ARRAY(r->entries, t->entries, t->entry_count);
	release_tree_content(t);
	return r;
}

static struct tree_entry *new_tree_entry(void)
{
	struct tree_entry *e;

	if (!avail_tree_entry) {
		unsigned int n = tree_entry_alloc;
		tree_entry_allocd += n * sizeof(struct tree_entry);
		ALLOC_ARRAY(e, n);
		avail_tree_entry = e;
		while (n-- > 1) {
			*((void**)e) = e + 1;
			e++;
		}
		*((void**)e) = NULL;
	}

	e = avail_tree_entry;
	avail_tree_entry = *((void**)e);
	return e;
}

static void release_tree_entry(struct tree_entry *e)
{
	if (e->tree)
		release_tree_content_recursive(e->tree);
	*((void**)e) = avail_tree_entry;
	avail_tree_entry = e;
}

static struct tree_content *dup_tree_content(struct tree_content *s)
{
	struct tree_content *d;
	struct tree_entry *a, *b;
	unsigned int i;

	if (!s)
		return NULL;
	d = new_tree_content(s->entry_count);
	for (i = 0; i < s->entry_count; i++) {
		a = s->entries[i];
		b = new_tree_entry();
		memcpy(b, a, sizeof(*a));
		if (a->tree && is_null_oid(&b->versions[1].oid))
			b->tree = dup_tree_content(a->tree);
		else
			b->tree = NULL;
		d->entries[i] = b;
	}
	d->entry_count = s->entry_count;
	d->delta_depth = s->delta_depth;

	return d;
}

static void start_packfile(void)
{
	struct strbuf tmp_file = STRBUF_INIT;
	struct packed_git *p;
	int pack_fd;

	pack_fd = odb_mkstemp(&tmp_file, "pack/tmp_pack_XXXXXX");
	FLEX_ALLOC_STR(p, pack_name, tmp_file.buf);
	strbuf_release(&tmp_file);

	p->pack_fd = pack_fd;
	p->do_not_close = 1;
	pack_file = hashfd(pack_fd, p->pack_name);

	pack_data = p;
	pack_size = write_pack_header(pack_file, 0);
	object_count = 0;

	REALLOC_ARRAY(all_packs, pack_id + 1);
	all_packs[pack_id] = p;
}

static const char *create_index(void)
{
	const char *tmpfile;
	struct pack_idx_entry **idx, **c, **last;
	struct object_entry *e;
	struct object_entry_pool *o;

	/* Build the table of object IDs. */
	ALLOC_ARRAY(idx, object_count);
	c = idx;
	for (o = blocks; o; o = o->next_pool)
		for (e = o->next_free; e-- != o->entries;)
			if (pack_id == e->pack_id)
				*c++ = &e->idx;
	last = idx + object_count;
	if (c != last)
		die("internal consistency error creating the index");

	tmpfile = write_idx_file(NULL, idx, object_count, &pack_idx_opts,
				 pack_data->hash);
	free(idx);
	return tmpfile;
}

static char *keep_pack(const char *curr_index_name)
{
	static const char *keep_msg = "fast-import";
	struct strbuf name = STRBUF_INIT;
	int keep_fd;

	odb_pack_name(&name, pack_data->hash, "keep");
	keep_fd = odb_pack_keep(name.buf);
	if (keep_fd < 0)
		die_errno("cannot create keep file");
	write_or_die(keep_fd, keep_msg, strlen(keep_msg));
	if (close(keep_fd))
		die_errno("failed to write keep file");

	odb_pack_name(&name, pack_data->hash, "pack");
	if (finalize_object_file(pack_data->pack_name, name.buf))
		die("cannot store pack file");

	odb_pack_name(&name, pack_data->hash, "idx");
	if (finalize_object_file(curr_index_name, name.buf))
		die("cannot store index file");
	free((void *)curr_index_name);
	return strbuf_detach(&name, NULL);
}

static void unkeep_all_packs(void)
{
	struct strbuf name = STRBUF_INIT;
	int k;

	for (k = 0; k < pack_id; k++) {
		struct packed_git *p = all_packs[k];
		odb_pack_name(&name, p->hash, "keep");
		unlink_or_warn(name.buf);
	}
	strbuf_release(&name);
}

static int loosen_small_pack(const struct packed_git *p)
{
	struct child_process unpack = CHILD_PROCESS_INIT;

	if (lseek(p->pack_fd, 0, SEEK_SET) < 0)
		die_errno("Failed seeking to start of '%s'", p->pack_name);

	unpack.in = p->pack_fd;
	unpack.git_cmd = 1;
	unpack.stdout_to_stderr = 1;
	strvec_push(&unpack.args, "unpack-objects");
	if (!show_stats)
		strvec_push(&unpack.args, "-q");

	return run_command(&unpack);
}

static void end_packfile(void)
{
	static int running;

	if (running || !pack_data)
		return;

	running = 1;
	clear_delta_base_cache();
	if (object_count) {
		struct packed_git *new_p;
		struct object_id cur_pack_oid;
		char *idx_name;
		int i;
		struct branch *b;
		struct tag *t;

		close_pack_windows(pack_data);
		finalize_hashfile(pack_file, cur_pack_oid.hash, 0);
		fixup_pack_header_footer(pack_data->pack_fd, pack_data->hash,
					 pack_data->pack_name, object_count,
					 cur_pack_oid.hash, pack_size);

		if (object_count <= unpack_limit) {
			if (!loosen_small_pack(pack_data)) {
				invalidate_pack_id(pack_id);
				goto discard_pack;
			}
		}

		close(pack_data->pack_fd);
		idx_name = keep_pack(create_index());

		/* Register the packfile with core git's machinery. */
		new_p = add_packed_git(idx_name, strlen(idx_name), 1);
		if (!new_p)
			die("core git rejected index %s", idx_name);
		all_packs[pack_id] = new_p;
		install_packed_git(the_repository, new_p);
		free(idx_name);

		/* Print the boundary */
		if (pack_edges) {
			fprintf(pack_edges, "%s:", new_p->pack_name);
			for (i = 0; i < branch_table_sz; i++) {
				for (b = branch_table[i]; b; b = b->table_next_branch) {
					if (b->pack_id == pack_id)
						fprintf(pack_edges, " %s",
							oid_to_hex(&b->oid));
				}
			}
			for (t = first_tag; t; t = t->next_tag) {
				if (t->pack_id == pack_id)
					fprintf(pack_edges, " %s",
						oid_to_hex(&t->oid));
			}
			fputc('\n', pack_edges);
			fflush(pack_edges);
		}

		pack_id++;
	}
	else {
discard_pack:
		close(pack_data->pack_fd);
		unlink_or_warn(pack_data->pack_name);
	}
	FREE_AND_NULL(pack_data);
	running = 0;

	/* We can't carry a delta across packfiles. */
	strbuf_release(&last_blob.data);
	last_blob.offset = 0;
	last_blob.depth = 0;
}

static void cycle_packfile(void)
{
	end_packfile();
	start_packfile();
}

static int store_object(
	enum object_type type,
	struct strbuf *dat,
	struct last_object *last,
	struct object_id *oidout,
	uintmax_t mark)
{
	void *out, *delta;
	struct object_entry *e;
	unsigned char hdr[96];
	struct object_id oid;
	unsigned long hdrlen, deltalen;
	git_hash_ctx c;
	git_zstream s;

	hdrlen = xsnprintf((char *)hdr, sizeof(hdr), "%s %lu",
			   type_name(type), (unsigned long)dat->len) + 1;
	the_hash_algo->init_fn(&c);
	the_hash_algo->update_fn(&c, hdr, hdrlen);
	the_hash_algo->update_fn(&c, dat->buf, dat->len);
	the_hash_algo->final_oid_fn(&oid, &c);
	if (oidout)
		oidcpy(oidout, &oid);

	e = insert_object(&oid);
	if (mark)
		insert_mark(&marks, mark, e);
	if (e->idx.offset) {
		duplicate_count_by_type[type]++;
		return 1;
	} else if (find_sha1_pack(oid.hash,
				  get_all_packs(the_repository))) {
		e->type = type;
		e->pack_id = MAX_PACK_ID;
		e->idx.offset = 1; /* just not zero! */
		duplicate_count_by_type[type]++;
		return 1;
	}

	if (last && last->data.len && last->data.buf && last->depth < max_depth
		&& dat->len > the_hash_algo->rawsz) {

		delta_count_attempts_by_type[type]++;
		delta = diff_delta(last->data.buf, last->data.len,
			dat->buf, dat->len,
			&deltalen, dat->len - the_hash_algo->rawsz);
	} else
		delta = NULL;

	git_deflate_init(&s, pack_compression_level);
	if (delta) {
		s.next_in = delta;
		s.avail_in = deltalen;
	} else {
		s.next_in = (void *)dat->buf;
		s.avail_in = dat->len;
	}
	s.avail_out = git_deflate_bound(&s, s.avail_in);
	s.next_out = out = xmalloc(s.avail_out);
	while (git_deflate(&s, Z_FINISH) == Z_OK)
		; /* nothing */
	git_deflate_end(&s);

	/* Determine if we should auto-checkpoint. */
	if ((max_packsize
		&& (pack_size + PACK_SIZE_THRESHOLD + s.total_out) > max_packsize)
		|| (pack_size + PACK_SIZE_THRESHOLD + s.total_out) < pack_size) {

		/* This new object needs to *not* have the current pack_id. */
		e->pack_id = pack_id + 1;
		cycle_packfile();

		/* We cannot carry a delta into the new pack. */
		if (delta) {
			FREE_AND_NULL(delta);

			git_deflate_init(&s, pack_compression_level);
			s.next_in = (void *)dat->buf;
			s.avail_in = dat->len;
			s.avail_out = git_deflate_bound(&s, s.avail_in);
			s.next_out = out = xrealloc(out, s.avail_out);
			while (git_deflate(&s, Z_FINISH) == Z_OK)
				; /* nothing */
			git_deflate_end(&s);
		}
	}

	e->type = type;
	e->pack_id = pack_id;
	e->idx.offset = pack_size;
	object_count++;
	object_count_by_type[type]++;

	crc32_begin(pack_file);

	if (delta) {
		off_t ofs = e->idx.offset - last->offset;
		unsigned pos = sizeof(hdr) - 1;

		delta_count_by_type[type]++;
		e->depth = last->depth + 1;

		hdrlen = encode_in_pack_object_header(hdr, sizeof(hdr),
						      OBJ_OFS_DELTA, deltalen);
		hashwrite(pack_file, hdr, hdrlen);
		pack_size += hdrlen;

		hdr[pos] = ofs & 127;
		while (ofs >>= 7)
			hdr[--pos] = 128 | (--ofs & 127);
		hashwrite(pack_file, hdr + pos, sizeof(hdr) - pos);
		pack_size += sizeof(hdr) - pos;
	} else {
		e->depth = 0;
		hdrlen = encode_in_pack_object_header(hdr, sizeof(hdr),
						      type, dat->len);
		hashwrite(pack_file, hdr, hdrlen);
		pack_size += hdrlen;
	}

	hashwrite(pack_file, out, s.total_out);
	pack_size += s.total_out;

	e->idx.crc32 = crc32_end(pack_file);

	free(out);
	free(delta);
	if (last) {
		if (last->no_swap) {
			last->data = *dat;
		} else {
			strbuf_swap(&last->data, dat);
		}
		last->offset = e->idx.offset;
		last->depth = e->depth;
	}
	return 0;
}

static void truncate_pack(struct hashfile_checkpoint *checkpoint)
{
	if (hashfile_truncate(pack_file, checkpoint))
		die_errno("cannot truncate pack to skip duplicate");
	pack_size = checkpoint->offset;
}

static void stream_blob(uintmax_t len, struct object_id *oidout, uintmax_t mark)
{
	size_t in_sz = 64 * 1024, out_sz = 64 * 1024;
	unsigned char *in_buf = xmalloc(in_sz);
	unsigned char *out_buf = xmalloc(out_sz);
	struct object_entry *e;
	struct object_id oid;
	unsigned long hdrlen;
	off_t offset;
	git_hash_ctx c;
	git_zstream s;
	struct hashfile_checkpoint checkpoint;
	int status = Z_OK;

	/* Determine if we should auto-checkpoint. */
	if ((max_packsize
		&& (pack_size + PACK_SIZE_THRESHOLD + len) > max_packsize)
		|| (pack_size + PACK_SIZE_THRESHOLD + len) < pack_size)
		cycle_packfile();

	hashfile_checkpoint(pack_file, &checkpoint);
	offset = checkpoint.offset;

	hdrlen = xsnprintf((char *)out_buf, out_sz, "blob %" PRIuMAX, len) + 1;

	the_hash_algo->init_fn(&c);
	the_hash_algo->update_fn(&c, out_buf, hdrlen);

	crc32_begin(pack_file);

	git_deflate_init(&s, pack_compression_level);

	hdrlen = encode_in_pack_object_header(out_buf, out_sz, OBJ_BLOB, len);

	s.next_out = out_buf + hdrlen;
	s.avail_out = out_sz - hdrlen;

	while (status != Z_STREAM_END) {
		if (0 < len && !s.avail_in) {
			size_t cnt = in_sz < len ? in_sz : (size_t)len;
			size_t n = fread(in_buf, 1, cnt, stdin);
			if (!n && feof(stdin))
				die("EOF in data (%" PRIuMAX " bytes remaining)", len);

			the_hash_algo->update_fn(&c, in_buf, n);
			s.next_in = in_buf;
			s.avail_in = n;
			len -= n;
		}

		status = git_deflate(&s, len ? 0 : Z_FINISH);

		if (!s.avail_out || status == Z_STREAM_END) {
			size_t n = s.next_out - out_buf;
			hashwrite(pack_file, out_buf, n);
			pack_size += n;
			s.next_out = out_buf;
			s.avail_out = out_sz;
		}

		switch (status) {
		case Z_OK:
		case Z_BUF_ERROR:
		case Z_STREAM_END:
			continue;
		default:
			die("unexpected deflate failure: %d", status);
		}
	}
	git_deflate_end(&s);
	the_hash_algo->final_oid_fn(&oid, &c);

	if (oidout)
		oidcpy(oidout, &oid);

	e = insert_object(&oid);

	if (mark)
		insert_mark(&marks, mark, e);

	if (e->idx.offset) {
		duplicate_count_by_type[OBJ_BLOB]++;
		truncate_pack(&checkpoint);

	} else if (find_sha1_pack(oid.hash,
				  get_all_packs(the_repository))) {
		e->type = OBJ_BLOB;
		e->pack_id = MAX_PACK_ID;
		e->idx.offset = 1; /* just not zero! */
		duplicate_count_by_type[OBJ_BLOB]++;
		truncate_pack(&checkpoint);

	} else {
		e->depth = 0;
		e->type = OBJ_BLOB;
		e->pack_id = pack_id;
		e->idx.offset = offset;
		e->idx.crc32 = crc32_end(pack_file);
		object_count++;
		object_count_by_type[OBJ_BLOB]++;
	}

	free(in_buf);
	free(out_buf);
}

/* All calls must be guarded by find_object() or find_mark() to
 * ensure the 'struct object_entry' passed was written by this
 * process instance.  We unpack the entry by the offset, avoiding
 * the need for the corresponding .idx file.  This unpacking rule
 * works because we only use OBJ_REF_DELTA within the packfiles
 * created by fast-import.
 *
 * oe must not be NULL.  Such an oe usually comes from giving
 * an unknown SHA-1 to find_object() or an undefined mark to
 * find_mark().  Callers must test for this condition and use
 * the standard read_sha1_file() when it happens.
 *
 * oe->pack_id must not be MAX_PACK_ID.  Such an oe is usually from
 * find_mark(), where the mark was reloaded from an existing marks
 * file and is referencing an object that this fast-import process
 * instance did not write out to a packfile.  Callers must test for
 * this condition and use read_sha1_file() instead.
 */
static void *gfi_unpack_entry(
	struct object_entry *oe,
	unsigned long *sizep)
{
	enum object_type type;
	struct packed_git *p = all_packs[oe->pack_id];
	if (p == pack_data && p->pack_size < (pack_size + the_hash_algo->rawsz)) {
		/* The object is stored in the packfile we are writing to
		 * and we have modified it since the last time we scanned
		 * back to read a previously written object.  If an old
		 * window covered [p->pack_size, p->pack_size + rawsz) its
		 * data is stale and is not valid.  Closing all windows
		 * and updating the packfile length ensures we can read
		 * the newly written data.
		 */
		close_pack_windows(p);
		hashflush(pack_file);

		/* We have to offer rawsz bytes additional on the end of
		 * the packfile as the core unpacker code assumes the
		 * footer is present at the file end and must promise
		 * at least rawsz bytes within any window it maps.  But
		 * we don't actually create the footer here.
		 */
		p->pack_size = pack_size + the_hash_algo->rawsz;
	}
	return unpack_entry(the_repository, p, oe->idx.offset, &type, sizep);
}

static const char *get_mode(const char *str, uint16_t *modep)
{
	unsigned char c;
	uint16_t mode = 0;

	while ((c = *str++) != ' ') {
		if (c < '0' || c > '7')
			return NULL;
		mode = (mode << 3) + (c - '0');
	}
	*modep = mode;
	return str;
}

static void load_tree(struct tree_entry *root)
{
	struct object_id *oid = &root->versions[1].oid;
	struct object_entry *myoe;
	struct tree_content *t;
	unsigned long size;
	char *buf;
	const char *c;

	root->tree = t = new_tree_content(8);
	if (is_null_oid(oid))
		return;

	myoe = find_object(oid);
	if (myoe && myoe->pack_id != MAX_PACK_ID) {
		if (myoe->type != OBJ_TREE)
			die("Not a tree: %s", oid_to_hex(oid));
		t->delta_depth = myoe->depth;
		buf = gfi_unpack_entry(myoe, &size);
		if (!buf)
			die("Can't load tree %s", oid_to_hex(oid));
	} else {
		enum object_type type;
		buf = read_object_file(oid, &type, &size);
		if (!buf || type != OBJ_TREE)
			die("Can't load tree %s", oid_to_hex(oid));
	}

	c = buf;
	while (c != (buf + size)) {
		struct tree_entry *e = new_tree_entry();

		if (t->entry_count == t->entry_capacity)
			root->tree = t = grow_tree_content(t, t->entry_count);
		t->entries[t->entry_count++] = e;

		e->tree = NULL;
		c = get_mode(c, &e->versions[1].mode);
		if (!c)
			die("Corrupt mode in %s", oid_to_hex(oid));
		e->versions[0].mode = e->versions[1].mode;
		e->name = to_atom(c, strlen(c));
		c += e->name->str_len + 1;
		oidread(&e->versions[0].oid, (unsigned char *)c);
		oidread(&e->versions[1].oid, (unsigned char *)c);
		c += the_hash_algo->rawsz;
	}
	free(buf);
}

static int tecmp0 (const void *_a, const void *_b)
{
	struct tree_entry *a = *((struct tree_entry**)_a);
	struct tree_entry *b = *((struct tree_entry**)_b);
	return base_name_compare(
		a->name->str_dat, a->name->str_len, a->versions[0].mode,
		b->name->str_dat, b->name->str_len, b->versions[0].mode);
}

static int tecmp1 (const void *_a, const void *_b)
{
	struct tree_entry *a = *((struct tree_entry**)_a);
	struct tree_entry *b = *((struct tree_entry**)_b);
	return base_name_compare(
		a->name->str_dat, a->name->str_len, a->versions[1].mode,
		b->name->str_dat, b->name->str_len, b->versions[1].mode);
}

static void mktree(struct tree_content *t, int v, struct strbuf *b)
{
	size_t maxlen = 0;
	unsigned int i;

	if (!v)
		QSORT(t->entries, t->entry_count, tecmp0);
	else
		QSORT(t->entries, t->entry_count, tecmp1);

	for (i = 0; i < t->entry_count; i++) {
		if (t->entries[i]->versions[v].mode)
			maxlen += t->entries[i]->name->str_len + 34;
	}

	strbuf_reset(b);
	strbuf_grow(b, maxlen);
	for (i = 0; i < t->entry_count; i++) {
		struct tree_entry *e = t->entries[i];
		if (!e->versions[v].mode)
			continue;
		strbuf_addf(b, "%o %s%c",
			(unsigned int)(e->versions[v].mode & ~NO_DELTA),
			e->name->str_dat, '\0');
		strbuf_add(b, e->versions[v].oid.hash, the_hash_algo->rawsz);
	}
}

static void store_tree(struct tree_entry *root)
{
	struct tree_content *t;
	unsigned int i, j, del;
	struct last_object lo = { STRBUF_INIT, 0, 0, /* no_swap */ 1 };
	struct object_entry *le = NULL;

	if (!is_null_oid(&root->versions[1].oid))
		return;

	if (!root->tree)
		load_tree(root);
	t = root->tree;

	for (i = 0; i < t->entry_count; i++) {
		if (t->entries[i]->tree)
			store_tree(t->entries[i]);
	}

	if (!(root->versions[0].mode & NO_DELTA))
		le = find_object(&root->versions[0].oid);
	if (S_ISDIR(root->versions[0].mode) && le && le->pack_id == pack_id) {
		mktree(t, 0, &old_tree);
		lo.data = old_tree;
		lo.offset = le->idx.offset;
		lo.depth = t->delta_depth;
	}

	mktree(t, 1, &new_tree);
	store_object(OBJ_TREE, &new_tree, &lo, &root->versions[1].oid, 0);

	t->delta_depth = lo.depth;
	for (i = 0, j = 0, del = 0; i < t->entry_count; i++) {
		struct tree_entry *e = t->entries[i];
		if (e->versions[1].mode) {
			e->versions[0].mode = e->versions[1].mode;
			oidcpy(&e->versions[0].oid, &e->versions[1].oid);
			t->entries[j++] = e;
		} else {
			release_tree_entry(e);
			del++;
		}
	}
	t->entry_count -= del;
}

static void tree_content_replace(
	struct tree_entry *root,
	const struct object_id *oid,
	const uint16_t mode,
	struct tree_content *newtree)
{
	if (!S_ISDIR(mode))
		die("Root cannot be a non-directory");
	oidclr(&root->versions[0].oid);
	oidcpy(&root->versions[1].oid, oid);
	if (root->tree)
		release_tree_content_recursive(root->tree);
	root->tree = newtree;
}

static int tree_content_set(
	struct tree_entry *root,
	const char *p,
	const struct object_id *oid,
	const uint16_t mode,
	struct tree_content *subtree)
{
	struct tree_content *t;
	const char *slash1;
	unsigned int i, n;
	struct tree_entry *e;

	slash1 = strchrnul(p, '/');
	n = slash1 - p;
	if (!n)
		die("Empty path component found in input");
	if (!*slash1 && !S_ISDIR(mode) && subtree)
		die("Non-directories cannot have subtrees");

	if (!root->tree)
		load_tree(root);
	t = root->tree;
	for (i = 0; i < t->entry_count; i++) {
		e = t->entries[i];
		if (e->name->str_len == n && !fspathncmp(p, e->name->str_dat, n)) {
			if (!*slash1) {
				if (!S_ISDIR(mode)
						&& e->versions[1].mode == mode
						&& oideq(&e->versions[1].oid, oid))
					return 0;
				e->versions[1].mode = mode;
				oidcpy(&e->versions[1].oid, oid);
				if (e->tree)
					release_tree_content_recursive(e->tree);
				e->tree = subtree;

				/*
				 * We need to leave e->versions[0].sha1 alone
				 * to avoid modifying the preimage tree used
				 * when writing out the parent directory.
				 * But after replacing the subdir with a
				 * completely different one, it's not a good
				 * delta base any more, and besides, we've
				 * thrown away the tree entries needed to
				 * make a delta against it.
				 *
				 * So let's just explicitly disable deltas
				 * for the subtree.
				 */
				if (S_ISDIR(e->versions[0].mode))
					e->versions[0].mode |= NO_DELTA;

				oidclr(&root->versions[1].oid);
				return 1;
			}
			if (!S_ISDIR(e->versions[1].mode)) {
				e->tree = new_tree_content(8);
				e->versions[1].mode = S_IFDIR;
			}
			if (!e->tree)
				load_tree(e);
			if (tree_content_set(e, slash1 + 1, oid, mode, subtree)) {
				oidclr(&root->versions[1].oid);
				return 1;
			}
			return 0;
		}
	}

	if (t->entry_count == t->entry_capacity)
		root->tree = t = grow_tree_content(t, t->entry_count);
	e = new_tree_entry();
	e->name = to_atom(p, n);
	e->versions[0].mode = 0;
	oidclr(&e->versions[0].oid);
	t->entries[t->entry_count++] = e;
	if (*slash1) {
		e->tree = new_tree_content(8);
		e->versions[1].mode = S_IFDIR;
		tree_content_set(e, slash1 + 1, oid, mode, subtree);
	} else {
		e->tree = subtree;
		e->versions[1].mode = mode;
		oidcpy(&e->versions[1].oid, oid);
	}
	oidclr(&root->versions[1].oid);
	return 1;
}

static int tree_content_remove(
	struct tree_entry *root,
	const char *p,
	struct tree_entry *backup_leaf,
	int allow_root)
{
	struct tree_content *t;
	const char *slash1;
	unsigned int i, n;
	struct tree_entry *e;

	slash1 = strchrnul(p, '/');
	n = slash1 - p;

	if (!root->tree)
		load_tree(root);

	if (!*p && allow_root) {
		e = root;
		goto del_entry;
	}

	t = root->tree;
	for (i = 0; i < t->entry_count; i++) {
		e = t->entries[i];
		if (e->name->str_len == n && !fspathncmp(p, e->name->str_dat, n)) {
			if (*slash1 && !S_ISDIR(e->versions[1].mode))
				/*
				 * If p names a file in some subdirectory, and a
				 * file or symlink matching the name of the
				 * parent directory of p exists, then p cannot
				 * exist and need not be deleted.
				 */
				return 1;
			if (!*slash1 || !S_ISDIR(e->versions[1].mode))
				goto del_entry;
			if (!e->tree)
				load_tree(e);
			if (tree_content_remove(e, slash1 + 1, backup_leaf, 0)) {
				for (n = 0; n < e->tree->entry_count; n++) {
					if (e->tree->entries[n]->versions[1].mode) {
						oidclr(&root->versions[1].oid);
						return 1;
					}
				}
				backup_leaf = NULL;
				goto del_entry;
			}
			return 0;
		}
	}
	return 0;

del_entry:
	if (backup_leaf)
		memcpy(backup_leaf, e, sizeof(*backup_leaf));
	else if (e->tree)
		release_tree_content_recursive(e->tree);
	e->tree = NULL;
	e->versions[1].mode = 0;
	oidclr(&e->versions[1].oid);
	oidclr(&root->versions[1].oid);
	return 1;
}

static int tree_content_get(
	struct tree_entry *root,
	const char *p,
	struct tree_entry *leaf,
	int allow_root)
{
	struct tree_content *t;
	const char *slash1;
	unsigned int i, n;
	struct tree_entry *e;

	slash1 = strchrnul(p, '/');
	n = slash1 - p;
	if (!n && !allow_root)
		die("Empty path component found in input");

	if (!root->tree)
		load_tree(root);

	if (!n) {
		e = root;
		goto found_entry;
	}

	t = root->tree;
	for (i = 0; i < t->entry_count; i++) {
		e = t->entries[i];
		if (e->name->str_len == n && !fspathncmp(p, e->name->str_dat, n)) {
			if (!*slash1)
				goto found_entry;
			if (!S_ISDIR(e->versions[1].mode))
				return 0;
			if (!e->tree)
				load_tree(e);
			return tree_content_get(e, slash1 + 1, leaf, 0);
		}
	}
	return 0;

found_entry:
	memcpy(leaf, e, sizeof(*leaf));
	if (e->tree && is_null_oid(&e->versions[1].oid))
		leaf->tree = dup_tree_content(e->tree);
	else
		leaf->tree = NULL;
	return 1;
}

static int update_branch(struct branch *b)
{
	static const char *msg = "fast-import";
	struct ref_transaction *transaction;
	struct object_id old_oid;
	struct strbuf err = STRBUF_INIT;

	if (is_null_oid(&b->oid)) {
		if (b->delete)
			delete_ref(NULL, b->name, NULL, 0);
		return 0;
	}
	if (read_ref(b->name, &old_oid))
		oidclr(&old_oid);
	if (!force_update && !is_null_oid(&old_oid)) {
		struct commit *old_cmit, *new_cmit;

		old_cmit = lookup_commit_reference_gently(the_repository,
							  &old_oid, 0);
		new_cmit = lookup_commit_reference_gently(the_repository,
							  &b->oid, 0);
		if (!old_cmit || !new_cmit)
			return error("Branch %s is missing commits.", b->name);

		if (!in_merge_bases(old_cmit, new_cmit)) {
			warning("Not updating %s"
				" (new tip %s does not contain %s)",
				b->name, oid_to_hex(&b->oid),
				oid_to_hex(&old_oid));
			return -1;
		}
	}
	transaction = ref_transaction_begin(&err);
	if (!transaction ||
	    ref_transaction_update(transaction, b->name, &b->oid, &old_oid,
				   0, msg, &err) ||
	    ref_transaction_commit(transaction, &err)) {
		ref_transaction_free(transaction);
		error("%s", err.buf);
		strbuf_release(&err);
		return -1;
	}
	ref_transaction_free(transaction);
	strbuf_release(&err);
	return 0;
}

static void dump_branches(void)
{
	unsigned int i;
	struct branch *b;

	for (i = 0; i < branch_table_sz; i++) {
		for (b = branch_table[i]; b; b = b->table_next_branch)
			failure |= update_branch(b);
	}
}

static void dump_tags(void)
{
	static const char *msg = "fast-import";
	struct tag *t;
	struct strbuf ref_name = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;
	struct ref_transaction *transaction;

	transaction = ref_transaction_begin(&err);
	if (!transaction) {
		failure |= error("%s", err.buf);
		goto cleanup;
	}
	for (t = first_tag; t; t = t->next_tag) {
		strbuf_reset(&ref_name);
		strbuf_addf(&ref_name, "refs/tags/%s", t->name);

		if (ref_transaction_update(transaction, ref_name.buf,
					   &t->oid, NULL, 0, msg, &err)) {
			failure |= error("%s", err.buf);
			goto cleanup;
		}
	}
	if (ref_transaction_commit(transaction, &err))
		failure |= error("%s", err.buf);

 cleanup:
	ref_transaction_free(transaction);
	strbuf_release(&ref_name);
	strbuf_release(&err);
}

static void dump_marks(void)
{
	struct lock_file mark_lock = LOCK_INIT;
	FILE *f;

	if (!export_marks_file || (import_marks_file && !import_marks_file_done))
		return;

	if (safe_create_leading_directories_const(export_marks_file)) {
		failure |= error_errno("unable to create leading directories of %s",
				       export_marks_file);
		return;
	}

	if (hold_lock_file_for_update(&mark_lock, export_marks_file, 0) < 0) {
		failure |= error_errno("Unable to write marks file %s",
				       export_marks_file);
		return;
	}

	f = fdopen_lock_file(&mark_lock, "w");
	if (!f) {
		int saved_errno = errno;
		rollback_lock_file(&mark_lock);
		failure |= error("Unable to write marks file %s: %s",
			export_marks_file, strerror(saved_errno));
		return;
	}

	for_each_mark(marks, 0, dump_marks_fn, f);
	if (commit_lock_file(&mark_lock)) {
		failure |= error_errno("Unable to write file %s",
				       export_marks_file);
		return;
	}
}

static void insert_object_entry(struct mark_set **s, struct object_id *oid, uintmax_t mark)
{
	struct object_entry *e;
	e = find_object(oid);
	if (!e) {
		enum object_type type = oid_object_info(the_repository,
							oid, NULL);
		if (type < 0)
			die("object not found: %s", oid_to_hex(oid));
		e = insert_object(oid);
		e->type = type;
		e->pack_id = MAX_PACK_ID;
		e->idx.offset = 1; /* just not zero! */
	}
	insert_mark(s, mark, e);
}

static void insert_oid_entry(struct mark_set **s, struct object_id *oid, uintmax_t mark)
{
	insert_mark(s, mark, xmemdupz(oid, sizeof(*oid)));
}

static void read_mark_file(struct mark_set **s, FILE *f, mark_set_inserter_t inserter)
{
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		uintmax_t mark;
		char *end;
		struct object_id oid;

		/* Ensure SHA-1 objects are padded with zeros. */
		memset(oid.hash, 0, sizeof(oid.hash));

		end = strchr(line, '\n');
		if (line[0] != ':' || !end)
			die("corrupt mark line: %s", line);
		*end = 0;
		mark = strtoumax(line + 1, &end, 10);
		if (!mark || end == line + 1
			|| *end != ' '
			|| get_oid_hex_any(end + 1, &oid) == GIT_HASH_UNKNOWN)
			die("corrupt mark line: %s", line);
		inserter(s, &oid, mark);
	}
}

static void read_marks(void)
{
	FILE *f = fopen(import_marks_file, "r");
	if (f)
		;
	else if (import_marks_file_ignore_missing && errno == ENOENT)
		goto done; /* Marks file does not exist */
	else
		die_errno("cannot read '%s'", import_marks_file);
	read_mark_file(&marks, f, insert_object_entry);
	fclose(f);
done:
	import_marks_file_done = 1;
}


static int read_next_command(void)
{
	static int stdin_eof = 0;

	if (stdin_eof) {
		unread_command_buf = 0;
		return EOF;
	}

	for (;;) {
		if (unread_command_buf) {
			unread_command_buf = 0;
		} else {
			struct recent_command *rc;

			stdin_eof = strbuf_getline_lf(&command_buf, stdin);
			if (stdin_eof)
				return EOF;

			if (!seen_data_command
				&& !starts_with(command_buf.buf, "feature ")
				&& !starts_with(command_buf.buf, "option ")) {
				parse_argv();
			}

			rc = rc_free;
			if (rc)
				rc_free = rc->next;
			else {
				rc = cmd_hist.next;
				cmd_hist.next = rc->next;
				cmd_hist.next->prev = &cmd_hist;
				free(rc->buf);
			}

			rc->buf = xstrdup(command_buf.buf);
			rc->prev = cmd_tail;
			rc->next = cmd_hist.prev;
			rc->prev->next = rc;
			cmd_tail = rc;
		}
		if (command_buf.buf[0] == '#')
			continue;
		return 0;
	}
}

static void skip_optional_lf(void)
{
	int term_char = fgetc(stdin);
	if (term_char != '\n' && term_char != EOF)
		ungetc(term_char, stdin);
}

static void parse_mark(void)
{
	const char *v;
	if (skip_prefix(command_buf.buf, "mark :", &v)) {
		next_mark = strtoumax(v, NULL, 10);
		read_next_command();
	}
	else
		next_mark = 0;
}

static void parse_original_identifier(void)
{
	const char *v;
	if (skip_prefix(command_buf.buf, "original-oid ", &v))
		read_next_command();
}

static int parse_data(struct strbuf *sb, uintmax_t limit, uintmax_t *len_res)
{
	const char *data;
	strbuf_reset(sb);

	if (!skip_prefix(command_buf.buf, "data ", &data))
		die("Expected 'data n' command, found: %s", command_buf.buf);

	if (skip_prefix(data, "<<", &data)) {
		char *term = xstrdup(data);
		size_t term_len = command_buf.len - (data - command_buf.buf);

		for (;;) {
			if (strbuf_getline_lf(&command_buf, stdin) == EOF)
				die("EOF in data (terminator '%s' not found)", term);
			if (term_len == command_buf.len
				&& !strcmp(term, command_buf.buf))
				break;
			strbuf_addbuf(sb, &command_buf);
			strbuf_addch(sb, '\n');
		}
		free(term);
	}
	else {
		uintmax_t len = strtoumax(data, NULL, 10);
		size_t n = 0, length = (size_t)len;

		if (limit && limit < len) {
			*len_res = len;
			return 0;
		}
		if (length < len)
			die("data is too large to use in this context");

		while (n < length) {
			size_t s = strbuf_fread(sb, length - n, stdin);
			if (!s && feof(stdin))
				die("EOF in data (%lu bytes remaining)",
					(unsigned long)(length - n));
			n += s;
		}
	}

	skip_optional_lf();
	return 1;
}

static int validate_raw_date(const char *src, struct strbuf *result, int strict)
{
	const char *orig_src = src;
	char *endp;
	unsigned long num;

	errno = 0;

	num = strtoul(src, &endp, 10);
	/*
	 * NEEDSWORK: perhaps check for reasonable values? For example, we
	 *            could error on values representing times more than a
	 *            day in the future.
	 */
	if (errno || endp == src || *endp != ' ')
		return -1;

	src = endp + 1;
	if (*src != '-' && *src != '+')
		return -1;

	num = strtoul(src + 1, &endp, 10);
	/*
	 * NEEDSWORK: check for brokenness other than num > 1400, such as
	 *            (num % 100) >= 60, or ((num % 100) % 15) != 0 ?
	 */
	if (errno || endp == src + 1 || *endp || /* did not parse */
	    (strict && (1400 < num))             /* parsed a broken timezone */
	   )
		return -1;

	strbuf_addstr(result, orig_src);
	return 0;
}

static char *parse_ident(const char *buf)
{
	const char *ltgt;
	size_t name_len;
	struct strbuf ident = STRBUF_INIT;

	/* ensure there is a space delimiter even if there is no name */
	if (*buf == '<')
		--buf;

	ltgt = buf + strcspn(buf, "<>");
	if (*ltgt != '<')
		die("Missing < in ident string: %s", buf);
	if (ltgt != buf && ltgt[-1] != ' ')
		die("Missing space before < in ident string: %s", buf);
	ltgt = ltgt + 1 + strcspn(ltgt + 1, "<>");
	if (*ltgt != '>')
		die("Missing > in ident string: %s", buf);
	ltgt++;
	if (*ltgt != ' ')
		die("Missing space after > in ident string: %s", buf);
	ltgt++;
	name_len = ltgt - buf;
	strbuf_add(&ident, buf, name_len);

	switch (whenspec) {
	case WHENSPEC_RAW:
		if (validate_raw_date(ltgt, &ident, 1) < 0)
			die("Invalid raw date \"%s\" in ident: %s", ltgt, buf);
		break;
	case WHENSPEC_RAW_PERMISSIVE:
		if (validate_raw_date(ltgt, &ident, 0) < 0)
			die("Invalid raw date \"%s\" in ident: %s", ltgt, buf);
		break;
	case WHENSPEC_RFC2822:
		if (parse_date(ltgt, &ident) < 0)
			die("Invalid rfc2822 date \"%s\" in ident: %s", ltgt, buf);
		break;
	case WHENSPEC_NOW:
		if (strcmp("now", ltgt))
			die("Date in ident must be 'now': %s", buf);
		datestamp(&ident);
		break;
	}

	return strbuf_detach(&ident, NULL);
}

static void parse_and_store_blob(
	struct last_object *last,
	struct object_id *oidout,
	uintmax_t mark)
{
	static struct strbuf buf = STRBUF_INIT;
	uintmax_t len;

	if (parse_data(&buf, big_file_threshold, &len))
		store_object(OBJ_BLOB, &buf, last, oidout, mark);
	else {
		if (last) {
			strbuf_release(&last->data);
			last->offset = 0;
			last->depth = 0;
		}
		stream_blob(len, oidout, mark);
		skip_optional_lf();
	}
}

static void parse_new_blob(void)
{
	read_next_command();
	parse_mark();
	parse_original_identifier();
	parse_and_store_blob(&last_blob, NULL, next_mark);
}

static void unload_one_branch(void)
{
	while (cur_active_branches
		&& cur_active_branches >= max_active_branches) {
		uintmax_t min_commit = ULONG_MAX;
		struct branch *e, *l = NULL, *p = NULL;

		for (e = active_branches; e; e = e->active_next_branch) {
			if (e->last_commit < min_commit) {
				p = l;
				min_commit = e->last_commit;
			}
			l = e;
		}

		if (p) {
			e = p->active_next_branch;
			p->active_next_branch = e->active_next_branch;
		} else {
			e = active_branches;
			active_branches = e->active_next_branch;
		}
		e->active = 0;
		e->active_next_branch = NULL;
		if (e->branch_tree.tree) {
			release_tree_content_recursive(e->branch_tree.tree);
			e->branch_tree.tree = NULL;
		}
		cur_active_branches--;
	}
}

static void load_branch(struct branch *b)
{
	load_tree(&b->branch_tree);
	if (!b->active) {
		b->active = 1;
		b->active_next_branch = active_branches;
		active_branches = b;
		cur_active_branches++;
		branch_load_count++;
	}
}

static unsigned char convert_num_notes_to_fanout(uintmax_t num_notes)
{
	unsigned char fanout = 0;
	while ((num_notes >>= 8))
		fanout++;
	return fanout;
}

static void construct_path_with_fanout(const char *hex_sha1,
		unsigned char fanout, char *path)
{
	unsigned int i = 0, j = 0;
	if (fanout >= the_hash_algo->rawsz)
		die("Too large fanout (%u)", fanout);
	while (fanout) {
		path[i++] = hex_sha1[j++];
		path[i++] = hex_sha1[j++];
		path[i++] = '/';
		fanout--;
	}
	memcpy(path + i, hex_sha1 + j, the_hash_algo->hexsz - j);
	path[i + the_hash_algo->hexsz - j] = '\0';
}

static uintmax_t do_change_note_fanout(
		struct tree_entry *orig_root, struct tree_entry *root,
		char *hex_oid, unsigned int hex_oid_len,
		char *fullpath, unsigned int fullpath_len,
		unsigned char fanout)
{
	struct tree_content *t;
	struct tree_entry *e, leaf;
	unsigned int i, tmp_hex_oid_len, tmp_fullpath_len;
	uintmax_t num_notes = 0;
	struct object_id oid;
	/* hex oid + '/' between each pair of hex digits + NUL */
	char realpath[GIT_MAX_HEXSZ + ((GIT_MAX_HEXSZ / 2) - 1) + 1];
	const unsigned hexsz = the_hash_algo->hexsz;

	if (!root->tree)
		load_tree(root);
	t = root->tree;

	for (i = 0; t && i < t->entry_count; i++) {
		e = t->entries[i];
		tmp_hex_oid_len = hex_oid_len + e->name->str_len;
		tmp_fullpath_len = fullpath_len;

		/*
		 * We're interested in EITHER existing note entries (entries
		 * with exactly 40 hex chars in path, not including directory
		 * separators), OR directory entries that may contain note
		 * entries (with < 40 hex chars in path).
		 * Also, each path component in a note entry must be a multiple
		 * of 2 chars.
		 */
		if (!e->versions[1].mode ||
		    tmp_hex_oid_len > hexsz ||
		    e->name->str_len % 2)
			continue;

		/* This _may_ be a note entry, or a subdir containing notes */
		memcpy(hex_oid + hex_oid_len, e->name->str_dat,
		       e->name->str_len);
		if (tmp_fullpath_len)
			fullpath[tmp_fullpath_len++] = '/';
		memcpy(fullpath + tmp_fullpath_len, e->name->str_dat,
		       e->name->str_len);
		tmp_fullpath_len += e->name->str_len;
		fullpath[tmp_fullpath_len] = '\0';

		if (tmp_hex_oid_len == hexsz && !get_oid_hex(hex_oid, &oid)) {
			/* This is a note entry */
			if (fanout == 0xff) {
				/* Counting mode, no rename */
				num_notes++;
				continue;
			}
			construct_path_with_fanout(hex_oid, fanout, realpath);
			if (!strcmp(fullpath, realpath)) {
				/* Note entry is in correct location */
				num_notes++;
				continue;
			}

			/* Rename fullpath to realpath */
			if (!tree_content_remove(orig_root, fullpath, &leaf, 0))
				die("Failed to remove path %s", fullpath);
			tree_content_set(orig_root, realpath,
				&leaf.versions[1].oid,
				leaf.versions[1].mode,
				leaf.tree);
		} else if (S_ISDIR(e->versions[1].mode)) {
			/* This is a subdir that may contain note entries */
			num_notes += do_change_note_fanout(orig_root, e,
				hex_oid, tmp_hex_oid_len,
				fullpath, tmp_fullpath_len, fanout);
		}

		/* The above may have reallocated the current tree_content */
		t = root->tree;
	}
	return num_notes;
}

static uintmax_t change_note_fanout(struct tree_entry *root,
		unsigned char fanout)
{
	/*
	 * The size of path is due to one slash between every two hex digits,
	 * plus the terminating NUL.  Note that there is no slash at the end, so
	 * the number of slashes is one less than half the number of hex
	 * characters.
	 */
	char hex_oid[GIT_MAX_HEXSZ], path[GIT_MAX_HEXSZ + (GIT_MAX_HEXSZ / 2) - 1 + 1];
	return do_change_note_fanout(root, root, hex_oid, 0, path, 0, fanout);
}

static int parse_mapped_oid_hex(const char *hex, struct object_id *oid, const char **end)
{
	int algo;
	khiter_t it;

	/* Make SHA-1 object IDs have all-zero padding. */
	memset(oid->hash, 0, sizeof(oid->hash));

	algo = parse_oid_hex_any(hex, oid, end);
	if (algo == GIT_HASH_UNKNOWN)
		return -1;

	it = kh_get_oid_map(sub_oid_map, *oid);
	/* No such object? */
	if (it == kh_end(sub_oid_map)) {
		/* If we're using the same algorithm, pass it through. */
		if (hash_algos[algo].format_id == the_hash_algo->format_id)
			return 0;
		return -1;
	}
	oidcpy(oid, kh_value(sub_oid_map, it));
	return 0;
}

/*
 * Given a pointer into a string, parse a mark reference:
 *
 *   idnum ::= ':' bigint;
 *
 * Return the first character after the value in *endptr.
 *
 * Complain if the following character is not what is expected,
 * either a space or end of the string.
 */
static uintmax_t parse_mark_ref(const char *p, char **endptr)
{
	uintmax_t mark;

	assert(*p == ':');
	p++;
	mark = strtoumax(p, endptr, 10);
	if (*endptr == p)
		die("No value after ':' in mark: %s", command_buf.buf);
	return mark;
}

/*
 * Parse the mark reference, and complain if this is not the end of
 * the string.
 */
static uintmax_t parse_mark_ref_eol(const char *p)
{
	char *end;
	uintmax_t mark;

	mark = parse_mark_ref(p, &end);
	if (*end != '\0')
		die("Garbage after mark: %s", command_buf.buf);
	return mark;
}

/*
 * Parse the mark reference, demanding a trailing space.  Return a
 * pointer to the space.
 */
static uintmax_t parse_mark_ref_space(const char **p)
{
	uintmax_t mark;
	char *end;

	mark = parse_mark_ref(*p, &end);
	if (*end++ != ' ')
		die("Missing space after mark: %s", command_buf.buf);
	*p = end;
	return mark;
}

static void file_change_m(const char *p, struct branch *b)
{
	static struct strbuf uq = STRBUF_INIT;
	const char *endp;
	struct object_entry *oe;
	struct object_id oid;
	uint16_t mode, inline_data = 0;

	p = get_mode(p, &mode);
	if (!p)
		die("Corrupt mode: %s", command_buf.buf);
	switch (mode) {
	case 0644:
	case 0755:
		mode |= S_IFREG;
	case S_IFREG | 0644:
	case S_IFREG | 0755:
	case S_IFLNK:
	case S_IFDIR:
	case S_IFGITLINK:
		/* ok */
		break;
	default:
		die("Corrupt mode: %s", command_buf.buf);
	}

	if (*p == ':') {
		oe = find_mark(marks, parse_mark_ref_space(&p));
		oidcpy(&oid, &oe->idx.oid);
	} else if (skip_prefix(p, "inline ", &p)) {
		inline_data = 1;
		oe = NULL; /* not used with inline_data, but makes gcc happy */
	} else {
		if (parse_mapped_oid_hex(p, &oid, &p))
			die("Invalid dataref: %s", command_buf.buf);
		oe = find_object(&oid);
		if (*p++ != ' ')
			die("Missing space after SHA1: %s", command_buf.buf);
	}

	strbuf_reset(&uq);
	if (!unquote_c_style(&uq, p, &endp)) {
		if (*endp)
			die("Garbage after path in: %s", command_buf.buf);
		p = uq.buf;
	}

	/* Git does not track empty, non-toplevel directories. */
	if (S_ISDIR(mode) && is_empty_tree_oid(&oid) && *p) {
		tree_content_remove(&b->branch_tree, p, NULL, 0);
		return;
	}

	if (S_ISGITLINK(mode)) {
		if (inline_data)
			die("Git links cannot be specified 'inline': %s",
				command_buf.buf);
		else if (oe) {
			if (oe->type != OBJ_COMMIT)
				die("Not a commit (actually a %s): %s",
					type_name(oe->type), command_buf.buf);
		}
		/*
		 * Accept the sha1 without checking; it expected to be in
		 * another repository.
		 */
	} else if (inline_data) {
		if (S_ISDIR(mode))
			die("Directories cannot be specified 'inline': %s",
				command_buf.buf);
		if (p != uq.buf) {
			strbuf_addstr(&uq, p);
			p = uq.buf;
		}
		while (read_next_command() != EOF) {
			const char *v;
			if (skip_prefix(command_buf.buf, "cat-blob ", &v))
				parse_cat_blob(v);
			else {
				parse_and_store_blob(&last_blob, &oid, 0);
				break;
			}
		}
	} else {
		enum object_type expected = S_ISDIR(mode) ?
						OBJ_TREE: OBJ_BLOB;
		enum object_type type = oe ? oe->type :
					oid_object_info(the_repository, &oid,
							NULL);
		if (type < 0)
			die("%s not found: %s",
					S_ISDIR(mode) ?  "Tree" : "Blob",
					command_buf.buf);
		if (type != expected)
			die("Not a %s (actually a %s): %s",
				type_name(expected), type_name(type),
				command_buf.buf);
	}

	if (!*p) {
		tree_content_replace(&b->branch_tree, &oid, mode, NULL);
		return;
	}
	tree_content_set(&b->branch_tree, p, &oid, mode, NULL);
}

static void file_change_d(const char *p, struct branch *b)
{
	static struct strbuf uq = STRBUF_INIT;
	const char *endp;

	strbuf_reset(&uq);
	if (!unquote_c_style(&uq, p, &endp)) {
		if (*endp)
			die("Garbage after path in: %s", command_buf.buf);
		p = uq.buf;
	}
	tree_content_remove(&b->branch_tree, p, NULL, 1);
}

static void file_change_cr(const char *s, struct branch *b, int rename)
{
	const char *d;
	static struct strbuf s_uq = STRBUF_INIT;
	static struct strbuf d_uq = STRBUF_INIT;
	const char *endp;
	struct tree_entry leaf;

	strbuf_reset(&s_uq);
	if (!unquote_c_style(&s_uq, s, &endp)) {
		if (*endp != ' ')
			die("Missing space after source: %s", command_buf.buf);
	} else {
		endp = strchr(s, ' ');
		if (!endp)
			die("Missing space after source: %s", command_buf.buf);
		strbuf_add(&s_uq, s, endp - s);
	}
	s = s_uq.buf;

	endp++;
	if (!*endp)
		die("Missing dest: %s", command_buf.buf);

	d = endp;
	strbuf_reset(&d_uq);
	if (!unquote_c_style(&d_uq, d, &endp)) {
		if (*endp)
			die("Garbage after dest in: %s", command_buf.buf);
		d = d_uq.buf;
	}

	memset(&leaf, 0, sizeof(leaf));
	if (rename)
		tree_content_remove(&b->branch_tree, s, &leaf, 1);
	else
		tree_content_get(&b->branch_tree, s, &leaf, 1);
	if (!leaf.versions[1].mode)
		die("Path %s not in branch", s);
	if (!*d) {	/* C "path/to/subdir" "" */
		tree_content_replace(&b->branch_tree,
			&leaf.versions[1].oid,
			leaf.versions[1].mode,
			leaf.tree);
		return;
	}
	tree_content_set(&b->branch_tree, d,
		&leaf.versions[1].oid,
		leaf.versions[1].mode,
		leaf.tree);
}

static void note_change_n(const char *p, struct branch *b, unsigned char *old_fanout)
{
	static struct strbuf uq = STRBUF_INIT;
	struct object_entry *oe;
	struct branch *s;
	struct object_id oid, commit_oid;
	char path[GIT_MAX_RAWSZ * 3];
	uint16_t inline_data = 0;
	unsigned char new_fanout;

	/*
	 * When loading a branch, we don't traverse its tree to count the real
	 * number of notes (too expensive to do this for all non-note refs).
	 * This means that recently loaded notes refs might incorrectly have
	 * b->num_notes == 0, and consequently, old_fanout might be wrong.
	 *
	 * Fix this by traversing the tree and counting the number of notes
	 * when b->num_notes == 0. If the notes tree is truly empty, the
	 * calculation should not take long.
	 */
	if (b->num_notes == 0 && *old_fanout == 0) {
		/* Invoke change_note_fanout() in "counting mode". */
		b->num_notes = change_note_fanout(&b->branch_tree, 0xff);
		*old_fanout = convert_num_notes_to_fanout(b->num_notes);
	}

	/* Now parse the notemodify command. */
	/* <dataref> or 'inline' */
	if (*p == ':') {
		oe = find_mark(marks, parse_mark_ref_space(&p));
		oidcpy(&oid, &oe->idx.oid);
	} else if (skip_prefix(p, "inline ", &p)) {
		inline_data = 1;
		oe = NULL; /* not used with inline_data, but makes gcc happy */
	} else {
		if (parse_mapped_oid_hex(p, &oid, &p))
			die("Invalid dataref: %s", command_buf.buf);
		oe = find_object(&oid);
		if (*p++ != ' ')
			die("Missing space after SHA1: %s", command_buf.buf);
	}

	/* <commit-ish> */
	s = lookup_branch(p);
	if (s) {
		if (is_null_oid(&s->oid))
			die("Can't add a note on empty branch.");
		oidcpy(&commit_oid, &s->oid);
	} else if (*p == ':') {
		uintmax_t commit_mark = parse_mark_ref_eol(p);
		struct object_entry *commit_oe = find_mark(marks, commit_mark);
		if (commit_oe->type != OBJ_COMMIT)
			die("Mark :%" PRIuMAX " not a commit", commit_mark);
		oidcpy(&commit_oid, &commit_oe->idx.oid);
	} else if (!get_oid(p, &commit_oid)) {
		unsigned long size;
		char *buf = read_object_with_reference(the_repository,
						       &commit_oid,
						       commit_type, &size,
						       &commit_oid);
		if (!buf || size < the_hash_algo->hexsz + 6)
			die("Not a valid commit: %s", p);
		free(buf);
	} else
		die("Invalid ref name or SHA1 expression: %s", p);

	if (inline_data) {
		if (p != uq.buf) {
			strbuf_addstr(&uq, p);
			p = uq.buf;
		}
		read_next_command();
		parse_and_store_blob(&last_blob, &oid, 0);
	} else if (oe) {
		if (oe->type != OBJ_BLOB)
			die("Not a blob (actually a %s): %s",
				type_name(oe->type), command_buf.buf);
	} else if (!is_null_oid(&oid)) {
		enum object_type type = oid_object_info(the_repository, &oid,
							NULL);
		if (type < 0)
			die("Blob not found: %s", command_buf.buf);
		if (type != OBJ_BLOB)
			die("Not a blob (actually a %s): %s",
			    type_name(type), command_buf.buf);
	}

	construct_path_with_fanout(oid_to_hex(&commit_oid), *old_fanout, path);
	if (tree_content_remove(&b->branch_tree, path, NULL, 0))
		b->num_notes--;

	if (is_null_oid(&oid))
		return; /* nothing to insert */

	b->num_notes++;
	new_fanout = convert_num_notes_to_fanout(b->num_notes);
	construct_path_with_fanout(oid_to_hex(&commit_oid), new_fanout, path);
	tree_content_set(&b->branch_tree, path, &oid, S_IFREG | 0644, NULL);
}

static void file_change_deleteall(struct branch *b)
{
	release_tree_content_recursive(b->branch_tree.tree);
	oidclr(&b->branch_tree.versions[0].oid);
	oidclr(&b->branch_tree.versions[1].oid);
	load_tree(&b->branch_tree);
	b->num_notes = 0;
}

static void parse_from_commit(struct branch *b, char *buf, unsigned long size)
{
	if (!buf || size < the_hash_algo->hexsz + 6)
		die("Not a valid commit: %s", oid_to_hex(&b->oid));
	if (memcmp("tree ", buf, 5)
		|| get_oid_hex(buf + 5, &b->branch_tree.versions[1].oid))
		die("The commit %s is corrupt", oid_to_hex(&b->oid));
	oidcpy(&b->branch_tree.versions[0].oid,
	       &b->branch_tree.versions[1].oid);
}

static void parse_from_existing(struct branch *b)
{
	if (is_null_oid(&b->oid)) {
		oidclr(&b->branch_tree.versions[0].oid);
		oidclr(&b->branch_tree.versions[1].oid);
	} else {
		unsigned long size;
		char *buf;

		buf = read_object_with_reference(the_repository,
						 &b->oid, commit_type, &size,
						 &b->oid);
		parse_from_commit(b, buf, size);
		free(buf);
	}
}

static int parse_objectish(struct branch *b, const char *objectish)
{
	struct branch *s;
	struct object_id oid;

	oidcpy(&oid, &b->branch_tree.versions[1].oid);

	s = lookup_branch(objectish);
	if (b == s)
		die("Can't create a branch from itself: %s", b->name);
	else if (s) {
		struct object_id *t = &s->branch_tree.versions[1].oid;
		oidcpy(&b->oid, &s->oid);
		oidcpy(&b->branch_tree.versions[0].oid, t);
		oidcpy(&b->branch_tree.versions[1].oid, t);
	} else if (*objectish == ':') {
		uintmax_t idnum = parse_mark_ref_eol(objectish);
		struct object_entry *oe = find_mark(marks, idnum);
		if (oe->type != OBJ_COMMIT)
			die("Mark :%" PRIuMAX " not a commit", idnum);
		if (!oideq(&b->oid, &oe->idx.oid)) {
			oidcpy(&b->oid, &oe->idx.oid);
			if (oe->pack_id != MAX_PACK_ID) {
				unsigned long size;
				char *buf = gfi_unpack_entry(oe, &size);
				parse_from_commit(b, buf, size);
				free(buf);
			} else
				parse_from_existing(b);
		}
	} else if (!get_oid(objectish, &b->oid)) {
		parse_from_existing(b);
		if (is_null_oid(&b->oid))
			b->delete = 1;
	}
	else
		die("Invalid ref name or SHA1 expression: %s", objectish);

	if (b->branch_tree.tree && !oideq(&oid, &b->branch_tree.versions[1].oid)) {
		release_tree_content_recursive(b->branch_tree.tree);
		b->branch_tree.tree = NULL;
	}

	read_next_command();
	return 1;
}

static int parse_from(struct branch *b)
{
	const char *from;

	if (!skip_prefix(command_buf.buf, "from ", &from))
		return 0;

	return parse_objectish(b, from);
}

static int parse_objectish_with_prefix(struct branch *b, const char *prefix)
{
	const char *base;

	if (!skip_prefix(command_buf.buf, prefix, &base))
		return 0;

	return parse_objectish(b, base);
}

static struct hash_list *parse_merge(unsigned int *count)
{
	struct hash_list *list = NULL, **tail = &list, *n;
	const char *from;
	struct branch *s;

	*count = 0;
	while (skip_prefix(command_buf.buf, "merge ", &from)) {
		n = xmalloc(sizeof(*n));
		s = lookup_branch(from);
		if (s)
			oidcpy(&n->oid, &s->oid);
		else if (*from == ':') {
			uintmax_t idnum = parse_mark_ref_eol(from);
			struct object_entry *oe = find_mark(marks, idnum);
			if (oe->type != OBJ_COMMIT)
				die("Mark :%" PRIuMAX " not a commit", idnum);
			oidcpy(&n->oid, &oe->idx.oid);
		} else if (!get_oid(from, &n->oid)) {
			unsigned long size;
			char *buf = read_object_with_reference(the_repository,
							       &n->oid,
							       commit_type,
							       &size, &n->oid);
			if (!buf || size < the_hash_algo->hexsz + 6)
				die("Not a valid commit: %s", from);
			free(buf);
		} else
			die("Invalid ref name or SHA1 expression: %s", from);

		n->next = NULL;
		*tail = n;
		tail = &n->next;

		(*count)++;
		read_next_command();
	}
	return list;
}

static void parse_new_commit(const char *arg)
{
	static struct strbuf msg = STRBUF_INIT;
	struct branch *b;
	char *author = NULL;
	char *committer = NULL;
	char *encoding = NULL;
	struct hash_list *merge_list = NULL;
	unsigned int merge_count;
	unsigned char prev_fanout, new_fanout;
	const char *v;

	b = lookup_branch(arg);
	if (!b)
		b = new_branch(arg);

	read_next_command();
	parse_mark();
	parse_original_identifier();
	if (skip_prefix(command_buf.buf, "author ", &v)) {
		author = parse_ident(v);
		read_next_command();
	}
	if (skip_prefix(command_buf.buf, "committer ", &v)) {
		committer = parse_ident(v);
		read_next_command();
	}
	if (!committer)
		die("Expected committer but didn't get one");
	if (skip_prefix(command_buf.buf, "encoding ", &v)) {
		encoding = xstrdup(v);
		read_next_command();
	}
	parse_data(&msg, 0, NULL);
	read_next_command();
	parse_from(b);
	merge_list = parse_merge(&merge_count);

	/* ensure the branch is active/loaded */
	if (!b->branch_tree.tree || !max_active_branches) {
		unload_one_branch();
		load_branch(b);
	}

	prev_fanout = convert_num_notes_to_fanout(b->num_notes);

	/* file_change* */
	while (command_buf.len > 0) {
		if (skip_prefix(command_buf.buf, "M ", &v))
			file_change_m(v, b);
		else if (skip_prefix(command_buf.buf, "D ", &v))
			file_change_d(v, b);
		else if (skip_prefix(command_buf.buf, "R ", &v))
			file_change_cr(v, b, 1);
		else if (skip_prefix(command_buf.buf, "C ", &v))
			file_change_cr(v, b, 0);
		else if (skip_prefix(command_buf.buf, "N ", &v))
			note_change_n(v, b, &prev_fanout);
		else if (!strcmp("deleteall", command_buf.buf))
			file_change_deleteall(b);
		else if (skip_prefix(command_buf.buf, "ls ", &v))
			parse_ls(v, b);
		else if (skip_prefix(command_buf.buf, "cat-blob ", &v))
			parse_cat_blob(v);
		else {
			unread_command_buf = 1;
			break;
		}
		if (read_next_command() == EOF)
			break;
	}

	new_fanout = convert_num_notes_to_fanout(b->num_notes);
	if (new_fanout != prev_fanout)
		b->num_notes = change_note_fanout(&b->branch_tree, new_fanout);

	/* build the tree and the commit */
	store_tree(&b->branch_tree);
	oidcpy(&b->branch_tree.versions[0].oid,
	       &b->branch_tree.versions[1].oid);

	strbuf_reset(&new_data);
	strbuf_addf(&new_data, "tree %s\n",
		oid_to_hex(&b->branch_tree.versions[1].oid));
	if (!is_null_oid(&b->oid))
		strbuf_addf(&new_data, "parent %s\n",
			    oid_to_hex(&b->oid));
	while (merge_list) {
		struct hash_list *next = merge_list->next;
		strbuf_addf(&new_data, "parent %s\n",
			    oid_to_hex(&merge_list->oid));
		free(merge_list);
		merge_list = next;
	}
	strbuf_addf(&new_data,
		"author %s\n"
		"committer %s\n",
		author ? author : committer, committer);
	if (encoding)
		strbuf_addf(&new_data,
			"encoding %s\n",
			encoding);
	strbuf_addch(&new_data, '\n');
	strbuf_addbuf(&new_data, &msg);
	free(author);
	free(committer);
	free(encoding);

	if (!store_object(OBJ_COMMIT, &new_data, NULL, &b->oid, next_mark))
		b->pack_id = pack_id;
	b->last_commit = object_count_by_type[OBJ_COMMIT];
}

static void parse_new_tag(const char *arg)
{
	static struct strbuf msg = STRBUF_INIT;
	const char *from;
	char *tagger;
	struct branch *s;
	struct tag *t;
	uintmax_t from_mark = 0;
	struct object_id oid;
	enum object_type type;
	const char *v;

	t = mem_pool_alloc(&fi_mem_pool, sizeof(struct tag));
	memset(t, 0, sizeof(struct tag));
	t->name = mem_pool_strdup(&fi_mem_pool, arg);
	if (last_tag)
		last_tag->next_tag = t;
	else
		first_tag = t;
	last_tag = t;
	read_next_command();
	parse_mark();

	/* from ... */
	if (!skip_prefix(command_buf.buf, "from ", &from))
		die("Expected from command, got %s", command_buf.buf);
	s = lookup_branch(from);
	if (s) {
		if (is_null_oid(&s->oid))
			die("Can't tag an empty branch.");
		oidcpy(&oid, &s->oid);
		type = OBJ_COMMIT;
	} else if (*from == ':') {
		struct object_entry *oe;
		from_mark = parse_mark_ref_eol(from);
		oe = find_mark(marks, from_mark);
		type = oe->type;
		oidcpy(&oid, &oe->idx.oid);
	} else if (!get_oid(from, &oid)) {
		struct object_entry *oe = find_object(&oid);
		if (!oe) {
			type = oid_object_info(the_repository, &oid, NULL);
			if (type < 0)
				die("Not a valid object: %s", from);
		} else
			type = oe->type;
	} else
		die("Invalid ref name or SHA1 expression: %s", from);
	read_next_command();

	/* original-oid ... */
	parse_original_identifier();

	/* tagger ... */
	if (skip_prefix(command_buf.buf, "tagger ", &v)) {
		tagger = parse_ident(v);
		read_next_command();
	} else
		tagger = NULL;

	/* tag payload/message */
	parse_data(&msg, 0, NULL);

	/* build the tag object */
	strbuf_reset(&new_data);

	strbuf_addf(&new_data,
		    "object %s\n"
		    "type %s\n"
		    "tag %s\n",
		    oid_to_hex(&oid), type_name(type), t->name);
	if (tagger)
		strbuf_addf(&new_data,
			    "tagger %s\n", tagger);
	strbuf_addch(&new_data, '\n');
	strbuf_addbuf(&new_data, &msg);
	free(tagger);

	if (store_object(OBJ_TAG, &new_data, NULL, &t->oid, next_mark))
		t->pack_id = MAX_PACK_ID;
	else
		t->pack_id = pack_id;
}

static void parse_reset_branch(const char *arg)
{
	struct branch *b;
	const char *tag_name;

	b = lookup_branch(arg);
	if (b) {
		oidclr(&b->oid);
		oidclr(&b->branch_tree.versions[0].oid);
		oidclr(&b->branch_tree.versions[1].oid);
		if (b->branch_tree.tree) {
			release_tree_content_recursive(b->branch_tree.tree);
			b->branch_tree.tree = NULL;
		}
	}
	else
		b = new_branch(arg);
	read_next_command();
	parse_from(b);
	if (b->delete && skip_prefix(b->name, "refs/tags/", &tag_name)) {
		/*
		 * Elsewhere, we call dump_branches() before dump_tags(),
		 * and dump_branches() will handle ref deletions first, so
		 * in order to make sure the deletion actually takes effect,
		 * we need to remove the tag from our list of tags to update.
		 *
		 * NEEDSWORK: replace list of tags with hashmap for faster
		 * deletion?
		 */
		struct tag *t, *prev = NULL;
		for (t = first_tag; t; t = t->next_tag) {
			if (!strcmp(t->name, tag_name))
				break;
			prev = t;
		}
		if (t) {
			if (prev)
				prev->next_tag = t->next_tag;
			else
				first_tag = t->next_tag;
			if (!t->next_tag)
				last_tag = prev;
			/* There is no mem_pool_free(t) function to call. */
		}
	}
	if (command_buf.len > 0)
		unread_command_buf = 1;
}

static void cat_blob_write(const char *buf, unsigned long size)
{
	if (write_in_full(cat_blob_fd, buf, size) < 0)
		die_errno("Write to frontend failed");
}

static void cat_blob(struct object_entry *oe, struct object_id *oid)
{
	struct strbuf line = STRBUF_INIT;
	unsigned long size;
	enum object_type type = 0;
	char *buf;

	if (!oe || oe->pack_id == MAX_PACK_ID) {
		buf = read_object_file(oid, &type, &size);
	} else {
		type = oe->type;
		buf = gfi_unpack_entry(oe, &size);
	}

	/*
	 * Output based on batch_one_object() from cat-file.c.
	 */
	if (type <= 0) {
		strbuf_reset(&line);
		strbuf_addf(&line, "%s missing\n", oid_to_hex(oid));
		cat_blob_write(line.buf, line.len);
		strbuf_release(&line);
		free(buf);
		return;
	}
	if (!buf)
		die("Can't read object %s", oid_to_hex(oid));
	if (type != OBJ_BLOB)
		die("Object %s is a %s but a blob was expected.",
		    oid_to_hex(oid), type_name(type));
	strbuf_reset(&line);
	strbuf_addf(&line, "%s %s %"PRIuMAX"\n", oid_to_hex(oid),
		    type_name(type), (uintmax_t)size);
	cat_blob_write(line.buf, line.len);
	strbuf_release(&line);
	cat_blob_write(buf, size);
	cat_blob_write("\n", 1);
	if (oe && oe->pack_id == pack_id) {
		last_blob.offset = oe->idx.offset;
		strbuf_attach(&last_blob.data, buf, size, size);
		last_blob.depth = oe->depth;
	} else
		free(buf);
}

static void parse_get_mark(const char *p)
{
	struct object_entry *oe;
	char output[GIT_MAX_HEXSZ + 2];

	/* get-mark SP <object> LF */
	if (*p != ':')
		die("Not a mark: %s", p);

	oe = find_mark(marks, parse_mark_ref_eol(p));
	if (!oe)
		die("Unknown mark: %s", command_buf.buf);

	xsnprintf(output, sizeof(output), "%s\n", oid_to_hex(&oe->idx.oid));
	cat_blob_write(output, the_hash_algo->hexsz + 1);
}

static void parse_cat_blob(const char *p)
{
	struct object_entry *oe;
	struct object_id oid;

	/* cat-blob SP <object> LF */
	if (*p == ':') {
		oe = find_mark(marks, parse_mark_ref_eol(p));
		if (!oe)
			die("Unknown mark: %s", command_buf.buf);
		oidcpy(&oid, &oe->idx.oid);
	} else {
		if (parse_mapped_oid_hex(p, &oid, &p))
			die("Invalid dataref: %s", command_buf.buf);
		if (*p)
			die("Garbage after SHA1: %s", command_buf.buf);
		oe = find_object(&oid);
	}

	cat_blob(oe, &oid);
}

static struct object_entry *dereference(struct object_entry *oe,
					struct object_id *oid)
{
	unsigned long size;
	char *buf = NULL;
	const unsigned hexsz = the_hash_algo->hexsz;

	if (!oe) {
		enum object_type type = oid_object_info(the_repository, oid,
							NULL);
		if (type < 0)
			die("object not found: %s", oid_to_hex(oid));
		/* cache it! */
		oe = insert_object(oid);
		oe->type = type;
		oe->pack_id = MAX_PACK_ID;
		oe->idx.offset = 1;
	}
	switch (oe->type) {
	case OBJ_TREE:	/* easy case. */
		return oe;
	case OBJ_COMMIT:
	case OBJ_TAG:
		break;
	default:
		die("Not a tree-ish: %s", command_buf.buf);
	}

	if (oe->pack_id != MAX_PACK_ID) {	/* in a pack being written */
		buf = gfi_unpack_entry(oe, &size);
	} else {
		enum object_type unused;
		buf = read_object_file(oid, &unused, &size);
	}
	if (!buf)
		die("Can't load object %s", oid_to_hex(oid));

	/* Peel one layer. */
	switch (oe->type) {
	case OBJ_TAG:
		if (size < hexsz + strlen("object ") ||
		    get_oid_hex(buf + strlen("object "), oid))
			die("Invalid SHA1 in tag: %s", command_buf.buf);
		break;
	case OBJ_COMMIT:
		if (size < hexsz + strlen("tree ") ||
		    get_oid_hex(buf + strlen("tree "), oid))
			die("Invalid SHA1 in commit: %s", command_buf.buf);
	}

	free(buf);
	return find_object(oid);
}

static void insert_mapped_mark(uintmax_t mark, void *object, void *cbp)
{
	struct object_id *fromoid = object;
	struct object_id *tooid = find_mark(cbp, mark);
	int ret;
	khiter_t it;

	it = kh_put_oid_map(sub_oid_map, *fromoid, &ret);
	/* We've already seen this object. */
	if (ret == 0)
		return;
	kh_value(sub_oid_map, it) = tooid;
}

static void build_mark_map_one(struct mark_set *from, struct mark_set *to)
{
	for_each_mark(from, 0, insert_mapped_mark, to);
}

static void build_mark_map(struct string_list *from, struct string_list *to)
{
	struct string_list_item *fromp, *top;

	sub_oid_map = kh_init_oid_map();

	for_each_string_list_item(fromp, from) {
		top = string_list_lookup(to, fromp->string);
		if (!fromp->util) {
			die(_("Missing from marks for submodule '%s'"), fromp->string);
		} else if (!top || !top->util) {
			die(_("Missing to marks for submodule '%s'"), fromp->string);
		}
		build_mark_map_one(fromp->util, top->util);
	}
}

static struct object_entry *parse_treeish_dataref(const char **p)
{
	struct object_id oid;
	struct object_entry *e;

	if (**p == ':') {	/* <mark> */
		e = find_mark(marks, parse_mark_ref_space(p));
		if (!e)
			die("Unknown mark: %s", command_buf.buf);
		oidcpy(&oid, &e->idx.oid);
	} else {	/* <sha1> */
		if (parse_mapped_oid_hex(*p, &oid, p))
			die("Invalid dataref: %s", command_buf.buf);
		e = find_object(&oid);
		if (*(*p)++ != ' ')
			die("Missing space after tree-ish: %s", command_buf.buf);
	}

	while (!e || e->type != OBJ_TREE)
		e = dereference(e, &oid);
	return e;
}

static void print_ls(int mode, const unsigned char *hash, const char *path)
{
	static struct strbuf line = STRBUF_INIT;

	/* See show_tree(). */
	const char *type =
		S_ISGITLINK(mode) ? commit_type :
		S_ISDIR(mode) ? tree_type :
		blob_type;

	if (!mode) {
		/* missing SP path LF */
		strbuf_reset(&line);
		strbuf_addstr(&line, "missing ");
		quote_c_style(path, &line, NULL, 0);
		strbuf_addch(&line, '\n');
	} else {
		/* mode SP type SP object_name TAB path LF */
		strbuf_reset(&line);
		strbuf_addf(&line, "%06o %s %s\t",
				mode & ~NO_DELTA, type, hash_to_hex(hash));
		quote_c_style(path, &line, NULL, 0);
		strbuf_addch(&line, '\n');
	}
	cat_blob_write(line.buf, line.len);
}

static void parse_ls(const char *p, struct branch *b)
{
	struct tree_entry *root = NULL;
	struct tree_entry leaf = {NULL};

	/* ls SP (<tree-ish> SP)? <path> */
	if (*p == '"') {
		if (!b)
			die("Not in a commit: %s", command_buf.buf);
		root = &b->branch_tree;
	} else {
		struct object_entry *e = parse_treeish_dataref(&p);
		root = new_tree_entry();
		oidcpy(&root->versions[1].oid, &e->idx.oid);
		if (!is_null_oid(&root->versions[1].oid))
			root->versions[1].mode = S_IFDIR;
		load_tree(root);
	}
	if (*p == '"') {
		static struct strbuf uq = STRBUF_INIT;
		const char *endp;
		strbuf_reset(&uq);
		if (unquote_c_style(&uq, p, &endp))
			die("Invalid path: %s", command_buf.buf);
		if (*endp)
			die("Garbage after path in: %s", command_buf.buf);
		p = uq.buf;
	}
	tree_content_get(root, p, &leaf, 1);
	/*
	 * A directory in preparation would have a sha1 of zero
	 * until it is saved.  Save, for simplicity.
	 */
	if (S_ISDIR(leaf.versions[1].mode))
		store_tree(&leaf);

	print_ls(leaf.versions[1].mode, leaf.versions[1].oid.hash, p);
	if (leaf.tree)
		release_tree_content_recursive(leaf.tree);
	if (!b || root != &b->branch_tree)
		release_tree_entry(root);
}

static void checkpoint(void)
{
	checkpoint_requested = 0;
	if (object_count) {
		cycle_packfile();
	}
	dump_branches();
	dump_tags();
	dump_marks();
}

static void parse_checkpoint(void)
{
	checkpoint_requested = 1;
	skip_optional_lf();
}

static void parse_progress(void)
{
	fwrite(command_buf.buf, 1, command_buf.len, stdout);
	fputc('\n', stdout);
	fflush(stdout);
	skip_optional_lf();
}

static void parse_alias(void)
{
	struct object_entry *e;
	struct branch b;

	skip_optional_lf();
	read_next_command();

	/* mark ... */
	parse_mark();
	if (!next_mark)
		die(_("Expected 'mark' command, got %s"), command_buf.buf);

	/* to ... */
	memset(&b, 0, sizeof(b));
	if (!parse_objectish_with_prefix(&b, "to "))
		die(_("Expected 'to' command, got %s"), command_buf.buf);
	e = find_object(&b.oid);
	assert(e);
	insert_mark(&marks, next_mark, e);
}

static char* make_fast_import_path(const char *path)
{
	if (!relative_marks_paths || is_absolute_path(path))
		return xstrdup(path);
	return git_pathdup("info/fast-import/%s", path);
}

static void option_import_marks(const char *marks,
					int from_stream, int ignore_missing)
{
	if (import_marks_file) {
		if (from_stream)
			die("Only one import-marks command allowed per stream");

		/* read previous mark file */
		if(!import_marks_file_from_stream)
			read_marks();
	}

	import_marks_file = make_fast_import_path(marks);
	import_marks_file_from_stream = from_stream;
	import_marks_file_ignore_missing = ignore_missing;
}

static void option_date_format(const char *fmt)
{
	if (!strcmp(fmt, "raw"))
		whenspec = WHENSPEC_RAW;
	else if (!strcmp(fmt, "raw-permissive"))
		whenspec = WHENSPEC_RAW_PERMISSIVE;
	else if (!strcmp(fmt, "rfc2822"))
		whenspec = WHENSPEC_RFC2822;
	else if (!strcmp(fmt, "now"))
		whenspec = WHENSPEC_NOW;
	else
		die("unknown --date-format argument %s", fmt);
}

static unsigned long ulong_arg(const char *option, const char *arg)
{
	char *endptr;
	unsigned long rv = strtoul(arg, &endptr, 0);
	if (strchr(arg, '-') || endptr == arg || *endptr)
		die("%s: argument must be a non-negative integer", option);
	return rv;
}

static void option_depth(const char *depth)
{
	max_depth = ulong_arg("--depth", depth);
	if (max_depth > MAX_DEPTH)
		die("--depth cannot exceed %u", MAX_DEPTH);
}

static void option_active_branches(const char *branches)
{
	max_active_branches = ulong_arg("--active-branches", branches);
}

static void option_export_marks(const char *marks)
{
	export_marks_file = make_fast_import_path(marks);
}

static void option_cat_blob_fd(const char *fd)
{
	unsigned long n = ulong_arg("--cat-blob-fd", fd);
	if (n > (unsigned long) INT_MAX)
		die("--cat-blob-fd cannot exceed %d", INT_MAX);
	cat_blob_fd = (int) n;
}

static void option_export_pack_edges(const char *edges)
{
	if (pack_edges)
		fclose(pack_edges);
	pack_edges = xfopen(edges, "a");
}

static void option_rewrite_submodules(const char *arg, struct string_list *list)
{
	struct mark_set *ms;
	FILE *fp;
	char *s = xstrdup(arg);
	char *f = strchr(s, ':');
	if (!f)
		die(_("Expected format name:filename for submodule rewrite option"));
	*f = '\0';
	f++;
	CALLOC_ARRAY(ms, 1);

	fp = fopen(f, "r");
	if (!fp)
		die_errno("cannot read '%s'", f);
	read_mark_file(&ms, fp, insert_oid_entry);
	fclose(fp);

	string_list_insert(list, s)->util = ms;
}

static int parse_one_option(const char *option)
{
	if (skip_prefix(option, "max-pack-size=", &option)) {
		unsigned long v;
		if (!git_parse_ulong(option, &v))
			return 0;
		if (v < 8192) {
			warning("max-pack-size is now in bytes, assuming --max-pack-size=%lum", v);
			v *= 1024 * 1024;
		} else if (v < 1024 * 1024) {
			warning("minimum max-pack-size is 1 MiB");
			v = 1024 * 1024;
		}
		max_packsize = v;
	} else if (skip_prefix(option, "big-file-threshold=", &option)) {
		unsigned long v;
		if (!git_parse_ulong(option, &v))
			return 0;
		big_file_threshold = v;
	} else if (skip_prefix(option, "depth=", &option)) {
		option_depth(option);
	} else if (skip_prefix(option, "active-branches=", &option)) {
		option_active_branches(option);
	} else if (skip_prefix(option, "export-pack-edges=", &option)) {
		option_export_pack_edges(option);
	} else if (!strcmp(option, "quiet")) {
		show_stats = 0;
	} else if (!strcmp(option, "stats")) {
		show_stats = 1;
	} else if (!strcmp(option, "allow-unsafe-features")) {
		; /* already handled during early option parsing */
	} else {
		return 0;
	}

	return 1;
}

static void check_unsafe_feature(const char *feature, int from_stream)
{
	if (from_stream && !allow_unsafe_features)
		die(_("feature '%s' forbidden in input without --allow-unsafe-features"),
		    feature);
}

static int parse_one_feature(const char *feature, int from_stream)
{
	const char *arg;

	if (skip_prefix(feature, "date-format=", &arg)) {
		option_date_format(arg);
	} else if (skip_prefix(feature, "import-marks=", &arg)) {
		check_unsafe_feature("import-marks", from_stream);
		option_import_marks(arg, from_stream, 0);
	} else if (skip_prefix(feature, "import-marks-if-exists=", &arg)) {
		check_unsafe_feature("import-marks-if-exists", from_stream);
		option_import_marks(arg, from_stream, 1);
	} else if (skip_prefix(feature, "export-marks=", &arg)) {
		check_unsafe_feature(feature, from_stream);
		option_export_marks(arg);
	} else if (!strcmp(feature, "alias")) {
		; /* Don't die - this feature is supported */
	} else if (skip_prefix(feature, "rewrite-submodules-to=", &arg)) {
		option_rewrite_submodules(arg, &sub_marks_to);
	} else if (skip_prefix(feature, "rewrite-submodules-from=", &arg)) {
		option_rewrite_submodules(arg, &sub_marks_from);
	} else if (!strcmp(feature, "get-mark")) {
		; /* Don't die - this feature is supported */
	} else if (!strcmp(feature, "cat-blob")) {
		; /* Don't die - this feature is supported */
	} else if (!strcmp(feature, "relative-marks")) {
		relative_marks_paths = 1;
	} else if (!strcmp(feature, "no-relative-marks")) {
		relative_marks_paths = 0;
	} else if (!strcmp(feature, "done")) {
		require_explicit_termination = 1;
	} else if (!strcmp(feature, "force")) {
		force_update = 1;
	} else if (!strcmp(feature, "notes") || !strcmp(feature, "ls")) {
		; /* do nothing; we have the feature */
	} else {
		return 0;
	}

	return 1;
}

static void parse_feature(const char *feature)
{
	if (seen_data_command)
		die("Got feature command '%s' after data command", feature);

	if (parse_one_feature(feature, 1))
		return;

	die("This version of fast-import does not support feature %s.", feature);
}

static void parse_option(const char *option)
{
	if (seen_data_command)
		die("Got option command '%s' after data command", option);

	if (parse_one_option(option))
		return;

	die("This version of fast-import does not support option: %s", option);
}

static void git_pack_config(void)
{
	int indexversion_value;
	int limit;
	unsigned long packsizelimit_value;

	if (!git_config_get_ulong("pack.depth", &max_depth)) {
		if (max_depth > MAX_DEPTH)
			max_depth = MAX_DEPTH;
	}
	if (!git_config_get_int("pack.indexversion", &indexversion_value)) {
		pack_idx_opts.version = indexversion_value;
		if (pack_idx_opts.version > 2)
			git_die_config("pack.indexversion",
					"bad pack.indexversion=%"PRIu32, pack_idx_opts.version);
	}
	if (!git_config_get_ulong("pack.packsizelimit", &packsizelimit_value))
		max_packsize = packsizelimit_value;

	if (!git_config_get_int("fastimport.unpacklimit", &limit))
		unpack_limit = limit;
	else if (!git_config_get_int("transfer.unpacklimit", &limit))
		unpack_limit = limit;

	git_config(git_default_config, NULL);
}

static const char fast_import_usage[] =
"git fast-import [--date-format=<f>] [--max-pack-size=<n>] [--big-file-threshold=<n>] [--depth=<n>] [--active-branches=<n>] [--export-marks=<marks.file>]";

static void parse_argv(void)
{
	unsigned int i;

	for (i = 1; i < global_argc; i++) {
		const char *a = global_argv[i];

		if (*a != '-' || !strcmp(a, "--"))
			break;

		if (!skip_prefix(a, "--", &a))
			die("unknown option %s", a);

		if (parse_one_option(a))
			continue;

		if (parse_one_feature(a, 0))
			continue;

		if (skip_prefix(a, "cat-blob-fd=", &a)) {
			option_cat_blob_fd(a);
			continue;
		}

		die("unknown option --%s", a);
	}
	if (i != global_argc)
		usage(fast_import_usage);

	seen_data_command = 1;
	if (import_marks_file)
		read_marks();
	build_mark_map(&sub_marks_from, &sub_marks_to);
}

int cmd_fast_import(int argc, const char **argv, const char *prefix)
{
	unsigned int i;

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(fast_import_usage);

	reset_pack_idx_option(&pack_idx_opts);
	git_pack_config();

	alloc_objects(object_entry_alloc);
	strbuf_init(&command_buf, 0);
	CALLOC_ARRAY(atom_table, atom_table_sz);
	CALLOC_ARRAY(branch_table, branch_table_sz);
	CALLOC_ARRAY(avail_tree_table, avail_tree_table_sz);
	marks = mem_pool_calloc(&fi_mem_pool, 1, sizeof(struct mark_set));

	hashmap_init(&object_table, object_entry_hashcmp, NULL, 0);

	/*
	 * We don't parse most options until after we've seen the set of
	 * "feature" lines at the start of the stream (which allows the command
	 * line to override stream data). But we must do an early parse of any
	 * command-line options that impact how we interpret the feature lines.
	 */
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (*arg != '-' || !strcmp(arg, "--"))
			break;
		if (!strcmp(arg, "--allow-unsafe-features"))
			allow_unsafe_features = 1;
	}

	global_argc = argc;
	global_argv = argv;

	rc_free = mem_pool_alloc(&fi_mem_pool, cmd_save * sizeof(*rc_free));
	for (i = 0; i < (cmd_save - 1); i++)
		rc_free[i].next = &rc_free[i + 1];
	rc_free[cmd_save - 1].next = NULL;

	start_packfile();
	set_die_routine(die_nicely);
	set_checkpoint_signal();
	while (read_next_command() != EOF) {
		const char *v;
		if (!strcmp("blob", command_buf.buf))
			parse_new_blob();
		else if (skip_prefix(command_buf.buf, "commit ", &v))
			parse_new_commit(v);
		else if (skip_prefix(command_buf.buf, "tag ", &v))
			parse_new_tag(v);
		else if (skip_prefix(command_buf.buf, "reset ", &v))
			parse_reset_branch(v);
		else if (skip_prefix(command_buf.buf, "ls ", &v))
			parse_ls(v, NULL);
		else if (skip_prefix(command_buf.buf, "cat-blob ", &v))
			parse_cat_blob(v);
		else if (skip_prefix(command_buf.buf, "get-mark ", &v))
			parse_get_mark(v);
		else if (!strcmp("checkpoint", command_buf.buf))
			parse_checkpoint();
		else if (!strcmp("done", command_buf.buf))
			break;
		else if (!strcmp("alias", command_buf.buf))
			parse_alias();
		else if (starts_with(command_buf.buf, "progress "))
			parse_progress();
		else if (skip_prefix(command_buf.buf, "feature ", &v))
			parse_feature(v);
		else if (skip_prefix(command_buf.buf, "option git ", &v))
			parse_option(v);
		else if (starts_with(command_buf.buf, "option "))
			/* ignore non-git options*/;
		else
			die("Unsupported command: %s", command_buf.buf);

		if (checkpoint_requested)
			checkpoint();
	}

	/* argv hasn't been parsed yet, do so */
	if (!seen_data_command)
		parse_argv();

	if (require_explicit_termination && feof(stdin))
		die("stream ends early");

	end_packfile();

	dump_branches();
	dump_tags();
	unkeep_all_packs();
	dump_marks();

	if (pack_edges)
		fclose(pack_edges);

	if (show_stats) {
		uintmax_t total_count = 0, duplicate_count = 0;
		for (i = 0; i < ARRAY_SIZE(object_count_by_type); i++)
			total_count += object_count_by_type[i];
		for (i = 0; i < ARRAY_SIZE(duplicate_count_by_type); i++)
			duplicate_count += duplicate_count_by_type[i];

		fprintf(stderr, "%s statistics:\n", argv[0]);
		fprintf(stderr, "---------------------------------------------------------------------\n");
		fprintf(stderr, "Alloc'd objects: %10" PRIuMAX "\n", alloc_count);
		fprintf(stderr, "Total objects:   %10" PRIuMAX " (%10" PRIuMAX " duplicates                  )\n", total_count, duplicate_count);
		fprintf(stderr, "      blobs  :   %10" PRIuMAX " (%10" PRIuMAX " duplicates %10" PRIuMAX " deltas of %10" PRIuMAX" attempts)\n", object_count_by_type[OBJ_BLOB], duplicate_count_by_type[OBJ_BLOB], delta_count_by_type[OBJ_BLOB], delta_count_attempts_by_type[OBJ_BLOB]);
		fprintf(stderr, "      trees  :   %10" PRIuMAX " (%10" PRIuMAX " duplicates %10" PRIuMAX " deltas of %10" PRIuMAX" attempts)\n", object_count_by_type[OBJ_TREE], duplicate_count_by_type[OBJ_TREE], delta_count_by_type[OBJ_TREE], delta_count_attempts_by_type[OBJ_TREE]);
		fprintf(stderr, "      commits:   %10" PRIuMAX " (%10" PRIuMAX " duplicates %10" PRIuMAX " deltas of %10" PRIuMAX" attempts)\n", object_count_by_type[OBJ_COMMIT], duplicate_count_by_type[OBJ_COMMIT], delta_count_by_type[OBJ_COMMIT], delta_count_attempts_by_type[OBJ_COMMIT]);
		fprintf(stderr, "      tags   :   %10" PRIuMAX " (%10" PRIuMAX " duplicates %10" PRIuMAX " deltas of %10" PRIuMAX" attempts)\n", object_count_by_type[OBJ_TAG], duplicate_count_by_type[OBJ_TAG], delta_count_by_type[OBJ_TAG], delta_count_attempts_by_type[OBJ_TAG]);
		fprintf(stderr, "Total branches:  %10lu (%10lu loads     )\n", branch_count, branch_load_count);
		fprintf(stderr, "      marks:     %10" PRIuMAX " (%10" PRIuMAX " unique    )\n", (((uintmax_t)1) << marks->shift) * 1024, marks_set_count);
		fprintf(stderr, "      atoms:     %10u\n", atom_cnt);
		fprintf(stderr, "Memory total:    %10" PRIuMAX " KiB\n", (tree_entry_allocd + fi_mem_pool.pool_alloc + alloc_count*sizeof(struct object_entry))/1024);
		fprintf(stderr, "       pools:    %10lu KiB\n", (unsigned long)((tree_entry_allocd + fi_mem_pool.pool_alloc) /1024));
		fprintf(stderr, "     objects:    %10" PRIuMAX " KiB\n", (alloc_count*sizeof(struct object_entry))/1024);
		fprintf(stderr, "---------------------------------------------------------------------\n");
		pack_report();
		fprintf(stderr, "---------------------------------------------------------------------\n");
		fprintf(stderr, "\n");
	}

	return failure ? 1 : 0;
}
