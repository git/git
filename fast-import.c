/*
(See Documentation/git-fast-import.txt for maintained documentation.)
Format of STDIN stream:

  stream ::= cmd*;

  cmd ::= new_blob
        | new_commit
        | new_tag
        | reset_branch
        | checkpoint
        | progress
        ;

  new_blob ::= 'blob' lf
    mark?
    file_content;
  file_content ::= data;

  new_commit ::= 'commit' sp ref_str lf
    mark?
    ('author' (sp name)? sp '<' email '>' sp when lf)?
    'committer' (sp name)? sp '<' email '>' sp when lf
    commit_msg
    ('from' sp committish lf)?
    ('merge' sp committish lf)*
    (file_change | ls)*
    lf?;
  commit_msg ::= data;

  ls ::= 'ls' sp '"' quoted(path) '"' lf;

  file_change ::= file_clr
    | file_del
    | file_rnm
    | file_cpy
    | file_obm
    | file_inm;
  file_clr ::= 'deleteall' lf;
  file_del ::= 'D' sp path_str lf;
  file_rnm ::= 'R' sp path_str sp path_str lf;
  file_cpy ::= 'C' sp path_str sp path_str lf;
  file_obm ::= 'M' sp mode sp (hexsha1 | idnum) sp path_str lf;
  file_inm ::= 'M' sp mode sp 'inline' sp path_str lf
    data;
  note_obm ::= 'N' sp (hexsha1 | idnum) sp committish lf;
  note_inm ::= 'N' sp 'inline' sp committish lf
    data;

  new_tag ::= 'tag' sp tag_str lf
    'from' sp committish lf
    ('tagger' (sp name)? sp '<' email '>' sp when lf)?
    tag_msg;
  tag_msg ::= data;

  reset_branch ::= 'reset' sp ref_str lf
    ('from' sp committish lf)?
    lf?;

  checkpoint ::= 'checkpoint' lf
    lf?;

  progress ::= 'progress' sp not_lf* lf
    lf?;

     # note: the first idnum in a stream should be 1 and subsequent
     # idnums should not have gaps between values as this will cause
     # the stream parser to reserve space for the gapped values.  An
     # idnum can be updated in the future to a new object by issuing
     # a new mark directive with the old idnum.
     #
  mark ::= 'mark' sp idnum lf;
  data ::= (delimited_data | exact_data)
    lf?;

    # note: delim may be any string but must not contain lf.
    # data_line may contain any data but must not be exactly
    # delim.
  delimited_data ::= 'data' sp '<<' delim lf
    (data_line lf)*
    delim lf;

     # note: declen indicates the length of binary_data in bytes.
     # declen does not include the lf preceding the binary data.
     #
  exact_data ::= 'data' sp declen lf
    binary_data;

     # note: quoted strings are C-style quoting supporting \c for
     # common escapes of 'c' (e..g \n, \t, \\, \") or \nnn where nnn
     # is the signed byte value in octal.  Note that the only
     # characters which must actually be escaped to protect the
     # stream formatting is: \, " and LF.  Otherwise these values
     # are UTF8.
     #
  committish  ::= (ref_str | hexsha1 | sha1exp_str | idnum);
  ref_str     ::= ref;
  sha1exp_str ::= sha1exp;
  tag_str     ::= tag;
  path_str    ::= path    | '"' quoted(path)    '"' ;
  mode        ::= '100644' | '644'
                | '100755' | '755'
                | '120000'
                ;

  declen ::= # unsigned 32 bit value, ascii base10 notation;
  bigint ::= # unsigned integer value, ascii base10 notation;
  binary_data ::= # file content, not interpreted;

  when         ::= raw_when | rfc2822_when;
  raw_when     ::= ts sp tz;
  rfc2822_when ::= # Valid RFC 2822 date and time;

  sp ::= # ASCII space character;
  lf ::= # ASCII newline (LF) character;

     # note: a colon (':') must precede the numerical value assigned to
     # an idnum.  This is to distinguish it from a ref or tag name as
     # GIT does not permit ':' in ref or tag strings.
     #
  idnum   ::= ':' bigint;
  path    ::= # GIT style file path, e.g. "a/b/c";
  ref     ::= # GIT ref name, e.g. "refs/heads/MOZ_GECKO_EXPERIMENT";
  tag     ::= # GIT tag name, e.g. "FIREFOX_1_5";
  sha1exp ::= # Any valid GIT SHA1 expression;
  hexsha1 ::= # SHA1 in hexadecimal format;

     # note: name and email are UTF8 strings, however name must not
     # contain '<' or lf and email must not contain any of the
     # following: '<', '>', lf.
     #
  name  ::= # valid GIT author/committer name;
  email ::= # valid GIT author/committer email;
  ts    ::= # time since the epoch in seconds, ascii base10 notation;
  tz    ::= # GIT style timezone;

     # note: comments, ls and cat requests may appear anywhere
     # in the input, except within a data command.  Any form
     # of the data command always escapes the related input
     # from comment processing.
     #
     # In case it is not clear, the '#' that starts the comment
     # must be the first character on that line (an lf
     # preceded it).
     #

  cat_blob ::= 'cat-blob' sp (hexsha1 | idnum) lf;
  ls_tree  ::= 'ls' sp (hexsha1 | idnum) sp path_str lf;

  comment ::= '#' not_lf* lf;
  not_lf  ::= # Any byte that is not ASCII newline (LF);
*/

#include "builtin.h"
#include "cache.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "commit.h"
#include "delta.h"
#include "pack.h"
#include "refs.h"
#include "csum-file.h"
#include "quote.h"
#include "exec_cmd.h"
#include "dir.h"

#define PACK_ID_BITS 16
#define MAX_PACK_ID ((1<<PACK_ID_BITS)-1)
#define DEPTH_BITS 13
#define MAX_DEPTH ((1<<DEPTH_BITS)-1)

struct object_entry {
	struct pack_idx_entry idx;
	struct object_entry *next;
	uint32_t type : TYPE_BITS,
		pack_id : PACK_ID_BITS,
		depth : DEPTH_BITS;
};

struct object_entry_pool {
	struct object_entry_pool *next_pool;
	struct object_entry *next_free;
	struct object_entry *end;
	struct object_entry entries[FLEX_ARRAY]; /* more */
};

struct mark_set {
	union {
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

struct mem_pool {
	struct mem_pool *next_pool;
	char *next_free;
	char *end;
	uintmax_t space[FLEX_ARRAY]; /* more */
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
		unsigned char sha1[20];
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
	unsigned pack_id : PACK_ID_BITS;
	unsigned char sha1[20];
};

struct tag {
	struct tag *next_tag;
	const char *name;
	unsigned int pack_id;
	unsigned char sha1[20];
};

struct hash_list {
	struct hash_list *next;
	unsigned char sha1[20];
};

typedef enum {
	WHENSPEC_RAW = 1,
	WHENSPEC_RFC2822,
	WHENSPEC_NOW
} whenspec_type;

struct recent_command {
	struct recent_command *prev;
	struct recent_command *next;
	char *buf;
};

/* Configured limits on output */
static unsigned long max_depth = 10;
static off_t max_packsize;
static int force_update;
static int pack_compression_level = Z_DEFAULT_COMPRESSION;
static int pack_compression_seen;

/* Stats and misc. counters */
static uintmax_t alloc_count;
static uintmax_t marks_set_count;
static uintmax_t object_count_by_type[1 << TYPE_BITS];
static uintmax_t duplicate_count_by_type[1 << TYPE_BITS];
static uintmax_t delta_count_by_type[1 << TYPE_BITS];
static unsigned long object_count;
static unsigned long branch_count;
static unsigned long branch_load_count;
static int failure;
static FILE *pack_edges;
static unsigned int show_stats = 1;
static int global_argc;
static const char **global_argv;

/* Memory pools */
static size_t mem_pool_alloc = 2*1024*1024 - sizeof(struct mem_pool);
static size_t total_allocd;
static struct mem_pool *mem_pool;

/* Atom management */
static unsigned int atom_table_sz = 4451;
static unsigned int atom_cnt;
static struct atom_str **atom_table;

/* The .pack file being generated */
static unsigned int pack_id;
static struct sha1file *pack_file;
static struct packed_git *pack_data;
static struct packed_git **all_packs;
static off_t pack_size;

/* Table of objects we've written. */
static unsigned int object_entry_alloc = 5000;
static struct object_entry_pool *blocks;
static struct object_entry *object_table[1 << 16];
static struct mark_set *marks;
static const char *export_marks_file;
static const char *import_marks_file;
static int import_marks_file_from_stream;
static int import_marks_file_ignore_missing;
static int relative_marks_paths;

/* Our last blob */
static struct last_object last_blob = { STRBUF_INIT, 0, 0, 0 };

/* Tree management */
static unsigned int tree_entry_alloc = 1000;
static void *avail_tree_entry;
static unsigned int avail_tree_table_sz = 100;
static struct avail_tree_content **avail_tree_table;
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

/* Signal handling */
static volatile sig_atomic_t checkpoint_requested;

/* Where to write output of cat-blob commands */
static int cat_blob_fd = STDOUT_FILENO;

static void parse_argv(void);
static void parse_cat_blob(void);
static void parse_ls(struct branch *b);

static void write_branch_report(FILE *rpt, struct branch *b)
{
	fprintf(rpt, "%s:\n", b->name);

	fprintf(rpt, "  status      :");
	if (b->active)
		fputs(" active", rpt);
	if (b->branch_tree.tree)
		fputs(" loaded", rpt);
	if (is_null_sha1(b->branch_tree.versions[1].sha1))
		fputs(" dirty", rpt);
	fputc('\n', rpt);

	fprintf(rpt, "  tip commit  : %s\n", sha1_to_hex(b->sha1));
	fprintf(rpt, "  old tree    : %s\n", sha1_to_hex(b->branch_tree.versions[0].sha1));
	fprintf(rpt, "  cur tree    : %s\n", sha1_to_hex(b->branch_tree.versions[1].sha1));
	fprintf(rpt, "  commit clock: %" PRIuMAX "\n", b->last_commit);

	fputs("  last pack   : ", rpt);
	if (b->pack_id < MAX_PACK_ID)
		fprintf(rpt, "%u", b->pack_id);
	fputc('\n', rpt);

	fputc('\n', rpt);
}

static void dump_marks_helper(FILE *, uintmax_t, struct mark_set *);

static void write_crash_report(const char *err)
{
	char *loc = git_path("fast_import_crash_%"PRIuMAX, (uintmax_t) getpid());
	FILE *rpt = fopen(loc, "w");
	struct branch *b;
	unsigned long lu;
	struct recent_command *rc;

	if (!rpt) {
		error("can't write crash report %s: %s", loc, strerror(errno));
		return;
	}

	fprintf(stderr, "fast-import: dumping crash report to %s\n", loc);

	fprintf(rpt, "fast-import crash report:\n");
	fprintf(rpt, "    fast-import process: %"PRIuMAX"\n", (uintmax_t) getpid());
	fprintf(rpt, "    parent process     : %"PRIuMAX"\n", (uintmax_t) getppid());
	fprintf(rpt, "    at %s\n", show_date(time(NULL), 0, DATE_LOCAL));
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
			fputs(sha1_to_hex(tg->sha1), rpt);
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
		dump_marks_helper(rpt, 0, marks);

	fputc('\n', rpt);
	fputs("-------------------\n", rpt);
	fputs("END OF CRASH REPORT\n", rpt);
	fclose(rpt);
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

static struct object_entry *new_object(unsigned char *sha1)
{
	struct object_entry *e;

	if (blocks->next_free == blocks->end)
		alloc_objects(object_entry_alloc);

	e = blocks->next_free++;
	hashcpy(e->idx.sha1, sha1);
	return e;
}

static struct object_entry *find_object(unsigned char *sha1)
{
	unsigned int h = sha1[0] << 8 | sha1[1];
	struct object_entry *e;
	for (e = object_table[h]; e; e = e->next)
		if (!hashcmp(sha1, e->idx.sha1))
			return e;
	return NULL;
}

static struct object_entry *insert_object(unsigned char *sha1)
{
	unsigned int h = sha1[0] << 8 | sha1[1];
	struct object_entry *e = object_table[h];

	while (e) {
		if (!hashcmp(sha1, e->idx.sha1))
			return e;
		e = e->next;
	}

	e = new_object(sha1);
	e->next = object_table[h];
	e->idx.offset = 0;
	object_table[h] = e;
	return e;
}

static unsigned int hc_str(const char *s, size_t len)
{
	unsigned int r = 0;
	while (len-- > 0)
		r = r * 31 + *s++;
	return r;
}

static void *pool_alloc(size_t len)
{
	struct mem_pool *p;
	void *r;

	/* round up to a 'uintmax_t' alignment */
	if (len & (sizeof(uintmax_t) - 1))
		len += sizeof(uintmax_t) - (len & (sizeof(uintmax_t) - 1));

	for (p = mem_pool; p; p = p->next_pool)
		if ((p->end - p->next_free >= len))
			break;

	if (!p) {
		if (len >= (mem_pool_alloc/2)) {
			total_allocd += len;
			return xmalloc(len);
		}
		total_allocd += sizeof(struct mem_pool) + mem_pool_alloc;
		p = xmalloc(sizeof(struct mem_pool) + mem_pool_alloc);
		p->next_pool = mem_pool;
		p->next_free = (char *) p->space;
		p->end = p->next_free + mem_pool_alloc;
		mem_pool = p;
	}

	r = p->next_free;
	p->next_free += len;
	return r;
}

static void *pool_calloc(size_t count, size_t size)
{
	size_t len = count * size;
	void *r = pool_alloc(len);
	memset(r, 0, len);
	return r;
}

static char *pool_strdup(const char *s)
{
	char *r = pool_alloc(strlen(s) + 1);
	strcpy(r, s);
	return r;
}

static void insert_mark(uintmax_t idnum, struct object_entry *oe)
{
	struct mark_set *s = marks;
	while ((idnum >> s->shift) >= 1024) {
		s = pool_calloc(1, sizeof(struct mark_set));
		s->shift = marks->shift + 10;
		s->data.sets[0] = marks;
		marks = s;
	}
	while (s->shift) {
		uintmax_t i = idnum >> s->shift;
		idnum -= i << s->shift;
		if (!s->data.sets[i]) {
			s->data.sets[i] = pool_calloc(1, sizeof(struct mark_set));
			s->data.sets[i]->shift = s->shift - 10;
		}
		s = s->data.sets[i];
	}
	if (!s->data.marked[idnum])
		marks_set_count++;
	s->data.marked[idnum] = oe;
}

static struct object_entry *find_mark(uintmax_t idnum)
{
	uintmax_t orig_idnum = idnum;
	struct mark_set *s = marks;
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

	c = pool_alloc(sizeof(struct atom_str) + len + 1);
	c->str_len = len;
	strncpy(c->str_dat, s, len);
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
	switch (check_ref_format(name)) {
	case 0: break; /* its valid */
	case CHECK_REF_FORMAT_ONELEVEL:
		break; /* valid, but too few '/', allow anyway */
	default:
		die("Branch name doesn't conform to GIT standards: %s", name);
	}

	b = pool_calloc(1, sizeof(struct branch));
	b->name = pool_strdup(name);
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
		f = pool_alloc(sizeof(*t) + sizeof(t->entries[0]) * cnt);
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
	memcpy(r->entries,t->entries,t->entry_count*sizeof(t->entries[0]));
	release_tree_content(t);
	return r;
}

static struct tree_entry *new_tree_entry(void)
{
	struct tree_entry *e;

	if (!avail_tree_entry) {
		unsigned int n = tree_entry_alloc;
		total_allocd += n * sizeof(struct tree_entry);
		avail_tree_entry = e = xmalloc(n * sizeof(struct tree_entry));
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
		if (a->tree && is_null_sha1(b->versions[1].sha1))
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
	static char tmpfile[PATH_MAX];
	struct packed_git *p;
	struct pack_header hdr;
	int pack_fd;

	pack_fd = odb_mkstemp(tmpfile, sizeof(tmpfile),
			      "pack/tmp_pack_XXXXXX");
	p = xcalloc(1, sizeof(*p) + strlen(tmpfile) + 2);
	strcpy(p->pack_name, tmpfile);
	p->pack_fd = pack_fd;
	p->do_not_close = 1;
	pack_file = sha1fd(pack_fd, p->pack_name);

	hdr.hdr_signature = htonl(PACK_SIGNATURE);
	hdr.hdr_version = htonl(2);
	hdr.hdr_entries = 0;
	sha1write(pack_file, &hdr, sizeof(hdr));

	pack_data = p;
	pack_size = sizeof(hdr);
	object_count = 0;

	all_packs = xrealloc(all_packs, sizeof(*all_packs) * (pack_id + 1));
	all_packs[pack_id] = p;
}

static const char *create_index(void)
{
	const char *tmpfile;
	struct pack_idx_entry **idx, **c, **last;
	struct object_entry *e;
	struct object_entry_pool *o;

	/* Build the table of object IDs. */
	idx = xmalloc(object_count * sizeof(*idx));
	c = idx;
	for (o = blocks; o; o = o->next_pool)
		for (e = o->next_free; e-- != o->entries;)
			if (pack_id == e->pack_id)
				*c++ = &e->idx;
	last = idx + object_count;
	if (c != last)
		die("internal consistency error creating the index");

	tmpfile = write_idx_file(NULL, idx, object_count, pack_data->sha1);
	free(idx);
	return tmpfile;
}

static char *keep_pack(const char *curr_index_name)
{
	static char name[PATH_MAX];
	static const char *keep_msg = "fast-import";
	int keep_fd;

	keep_fd = odb_pack_keep(name, sizeof(name), pack_data->sha1);
	if (keep_fd < 0)
		die_errno("cannot create keep file");
	write_or_die(keep_fd, keep_msg, strlen(keep_msg));
	if (close(keep_fd))
		die_errno("failed to write keep file");

	snprintf(name, sizeof(name), "%s/pack/pack-%s.pack",
		 get_object_directory(), sha1_to_hex(pack_data->sha1));
	if (move_temp_to_file(pack_data->pack_name, name))
		die("cannot store pack file");

	snprintf(name, sizeof(name), "%s/pack/pack-%s.idx",
		 get_object_directory(), sha1_to_hex(pack_data->sha1));
	if (move_temp_to_file(curr_index_name, name))
		die("cannot store index file");
	free((void *)curr_index_name);
	return name;
}

static void unkeep_all_packs(void)
{
	static char name[PATH_MAX];
	int k;

	for (k = 0; k < pack_id; k++) {
		struct packed_git *p = all_packs[k];
		snprintf(name, sizeof(name), "%s/pack/pack-%s.keep",
			 get_object_directory(), sha1_to_hex(p->sha1));
		unlink_or_warn(name);
	}
}

static void end_packfile(void)
{
	struct packed_git *old_p = pack_data, *new_p;

	clear_delta_base_cache();
	if (object_count) {
		unsigned char cur_pack_sha1[20];
		char *idx_name;
		int i;
		struct branch *b;
		struct tag *t;

		close_pack_windows(pack_data);
		sha1close(pack_file, cur_pack_sha1, 0);
		fixup_pack_header_footer(pack_data->pack_fd, pack_data->sha1,
				    pack_data->pack_name, object_count,
				    cur_pack_sha1, pack_size);
		close(pack_data->pack_fd);
		idx_name = keep_pack(create_index());

		/* Register the packfile with core git's machinery. */
		new_p = add_packed_git(idx_name, strlen(idx_name), 1);
		if (!new_p)
			die("core git rejected index %s", idx_name);
		all_packs[pack_id] = new_p;
		install_packed_git(new_p);

		/* Print the boundary */
		if (pack_edges) {
			fprintf(pack_edges, "%s:", new_p->pack_name);
			for (i = 0; i < branch_table_sz; i++) {
				for (b = branch_table[i]; b; b = b->table_next_branch) {
					if (b->pack_id == pack_id)
						fprintf(pack_edges, " %s", sha1_to_hex(b->sha1));
				}
			}
			for (t = first_tag; t; t = t->next_tag) {
				if (t->pack_id == pack_id)
					fprintf(pack_edges, " %s", sha1_to_hex(t->sha1));
			}
			fputc('\n', pack_edges);
			fflush(pack_edges);
		}

		pack_id++;
	}
	else {
		close(old_p->pack_fd);
		unlink_or_warn(old_p->pack_name);
	}
	free(old_p);

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
	unsigned char *sha1out,
	uintmax_t mark)
{
	void *out, *delta;
	struct object_entry *e;
	unsigned char hdr[96];
	unsigned char sha1[20];
	unsigned long hdrlen, deltalen;
	git_SHA_CTX c;
	git_zstream s;

	hdrlen = sprintf((char *)hdr,"%s %lu", typename(type),
		(unsigned long)dat->len) + 1;
	git_SHA1_Init(&c);
	git_SHA1_Update(&c, hdr, hdrlen);
	git_SHA1_Update(&c, dat->buf, dat->len);
	git_SHA1_Final(sha1, &c);
	if (sha1out)
		hashcpy(sha1out, sha1);

	e = insert_object(sha1);
	if (mark)
		insert_mark(mark, e);
	if (e->idx.offset) {
		duplicate_count_by_type[type]++;
		return 1;
	} else if (find_sha1_pack(sha1, packed_git)) {
		e->type = type;
		e->pack_id = MAX_PACK_ID;
		e->idx.offset = 1; /* just not zero! */
		duplicate_count_by_type[type]++;
		return 1;
	}

	if (last && last->data.buf && last->depth < max_depth && dat->len > 20) {
		delta = diff_delta(last->data.buf, last->data.len,
			dat->buf, dat->len,
			&deltalen, dat->len - 20);
	} else
		delta = NULL;

	memset(&s, 0, sizeof(s));
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
	if ((max_packsize && (pack_size + 60 + s.total_out) > max_packsize)
		|| (pack_size + 60 + s.total_out) < pack_size) {

		/* This new object needs to *not* have the current pack_id. */
		e->pack_id = pack_id + 1;
		cycle_packfile();

		/* We cannot carry a delta into the new pack. */
		if (delta) {
			free(delta);
			delta = NULL;

			memset(&s, 0, sizeof(s));
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

		hdrlen = encode_in_pack_object_header(OBJ_OFS_DELTA, deltalen, hdr);
		sha1write(pack_file, hdr, hdrlen);
		pack_size += hdrlen;

		hdr[pos] = ofs & 127;
		while (ofs >>= 7)
			hdr[--pos] = 128 | (--ofs & 127);
		sha1write(pack_file, hdr + pos, sizeof(hdr) - pos);
		pack_size += sizeof(hdr) - pos;
	} else {
		e->depth = 0;
		hdrlen = encode_in_pack_object_header(type, dat->len, hdr);
		sha1write(pack_file, hdr, hdrlen);
		pack_size += hdrlen;
	}

	sha1write(pack_file, out, s.total_out);
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

static void truncate_pack(off_t to, git_SHA_CTX *ctx)
{
	if (ftruncate(pack_data->pack_fd, to)
	 || lseek(pack_data->pack_fd, to, SEEK_SET) != to)
		die_errno("cannot truncate pack to skip duplicate");
	pack_size = to;

	/* yes this is a layering violation */
	pack_file->total = to;
	pack_file->offset = 0;
	pack_file->ctx = *ctx;
}

static void stream_blob(uintmax_t len, unsigned char *sha1out, uintmax_t mark)
{
	size_t in_sz = 64 * 1024, out_sz = 64 * 1024;
	unsigned char *in_buf = xmalloc(in_sz);
	unsigned char *out_buf = xmalloc(out_sz);
	struct object_entry *e;
	unsigned char sha1[20];
	unsigned long hdrlen;
	off_t offset;
	git_SHA_CTX c;
	git_SHA_CTX pack_file_ctx;
	git_zstream s;
	int status = Z_OK;

	/* Determine if we should auto-checkpoint. */
	if ((max_packsize && (pack_size + 60 + len) > max_packsize)
		|| (pack_size + 60 + len) < pack_size)
		cycle_packfile();

	offset = pack_size;

	/* preserve the pack_file SHA1 ctx in case we have to truncate later */
	sha1flush(pack_file);
	pack_file_ctx = pack_file->ctx;

	hdrlen = snprintf((char *)out_buf, out_sz, "blob %" PRIuMAX, len) + 1;
	if (out_sz <= hdrlen)
		die("impossibly large object header");

	git_SHA1_Init(&c);
	git_SHA1_Update(&c, out_buf, hdrlen);

	crc32_begin(pack_file);

	memset(&s, 0, sizeof(s));
	git_deflate_init(&s, pack_compression_level);

	hdrlen = encode_in_pack_object_header(OBJ_BLOB, len, out_buf);
	if (out_sz <= hdrlen)
		die("impossibly large object header");

	s.next_out = out_buf + hdrlen;
	s.avail_out = out_sz - hdrlen;

	while (status != Z_STREAM_END) {
		if (0 < len && !s.avail_in) {
			size_t cnt = in_sz < len ? in_sz : (size_t)len;
			size_t n = fread(in_buf, 1, cnt, stdin);
			if (!n && feof(stdin))
				die("EOF in data (%" PRIuMAX " bytes remaining)", len);

			git_SHA1_Update(&c, in_buf, n);
			s.next_in = in_buf;
			s.avail_in = n;
			len -= n;
		}

		status = git_deflate(&s, len ? 0 : Z_FINISH);

		if (!s.avail_out || status == Z_STREAM_END) {
			size_t n = s.next_out - out_buf;
			sha1write(pack_file, out_buf, n);
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
	git_SHA1_Final(sha1, &c);

	if (sha1out)
		hashcpy(sha1out, sha1);

	e = insert_object(sha1);

	if (mark)
		insert_mark(mark, e);

	if (e->idx.offset) {
		duplicate_count_by_type[OBJ_BLOB]++;
		truncate_pack(offset, &pack_file_ctx);

	} else if (find_sha1_pack(sha1, packed_git)) {
		e->type = OBJ_BLOB;
		e->pack_id = MAX_PACK_ID;
		e->idx.offset = 1; /* just not zero! */
		duplicate_count_by_type[OBJ_BLOB]++;
		truncate_pack(offset, &pack_file_ctx);

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
	if (p == pack_data && p->pack_size < (pack_size + 20)) {
		/* The object is stored in the packfile we are writing to
		 * and we have modified it since the last time we scanned
		 * back to read a previously written object.  If an old
		 * window covered [p->pack_size, p->pack_size + 20) its
		 * data is stale and is not valid.  Closing all windows
		 * and updating the packfile length ensures we can read
		 * the newly written data.
		 */
		close_pack_windows(p);
		sha1flush(pack_file);

		/* We have to offer 20 bytes additional on the end of
		 * the packfile as the core unpacker code assumes the
		 * footer is present at the file end and must promise
		 * at least 20 bytes within any window it maps.  But
		 * we don't actually create the footer here.
		 */
		p->pack_size = pack_size + 20;
	}
	return unpack_entry(p, oe->idx.offset, &type, sizep);
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
	unsigned char *sha1 = root->versions[1].sha1;
	struct object_entry *myoe;
	struct tree_content *t;
	unsigned long size;
	char *buf;
	const char *c;

	root->tree = t = new_tree_content(8);
	if (is_null_sha1(sha1))
		return;

	myoe = find_object(sha1);
	if (myoe && myoe->pack_id != MAX_PACK_ID) {
		if (myoe->type != OBJ_TREE)
			die("Not a tree: %s", sha1_to_hex(sha1));
		t->delta_depth = myoe->depth;
		buf = gfi_unpack_entry(myoe, &size);
		if (!buf)
			die("Can't load tree %s", sha1_to_hex(sha1));
	} else {
		enum object_type type;
		buf = read_sha1_file(sha1, &type, &size);
		if (!buf || type != OBJ_TREE)
			die("Can't load tree %s", sha1_to_hex(sha1));
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
			die("Corrupt mode in %s", sha1_to_hex(sha1));
		e->versions[0].mode = e->versions[1].mode;
		e->name = to_atom(c, strlen(c));
		c += e->name->str_len + 1;
		hashcpy(e->versions[0].sha1, (unsigned char *)c);
		hashcpy(e->versions[1].sha1, (unsigned char *)c);
		c += 20;
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
		qsort(t->entries,t->entry_count,sizeof(t->entries[0]),tecmp0);
	else
		qsort(t->entries,t->entry_count,sizeof(t->entries[0]),tecmp1);

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
		strbuf_addf(b, "%o %s%c", (unsigned int)e->versions[v].mode,
					e->name->str_dat, '\0');
		strbuf_add(b, e->versions[v].sha1, 20);
	}
}

static void store_tree(struct tree_entry *root)
{
	struct tree_content *t = root->tree;
	unsigned int i, j, del;
	struct last_object lo = { STRBUF_INIT, 0, 0, /* no_swap */ 1 };
	struct object_entry *le;

	if (!is_null_sha1(root->versions[1].sha1))
		return;

	for (i = 0; i < t->entry_count; i++) {
		if (t->entries[i]->tree)
			store_tree(t->entries[i]);
	}

	le = find_object(root->versions[0].sha1);
	if (S_ISDIR(root->versions[0].mode) && le && le->pack_id == pack_id) {
		mktree(t, 0, &old_tree);
		lo.data = old_tree;
		lo.offset = le->idx.offset;
		lo.depth = t->delta_depth;
	}

	mktree(t, 1, &new_tree);
	store_object(OBJ_TREE, &new_tree, &lo, root->versions[1].sha1, 0);

	t->delta_depth = lo.depth;
	for (i = 0, j = 0, del = 0; i < t->entry_count; i++) {
		struct tree_entry *e = t->entries[i];
		if (e->versions[1].mode) {
			e->versions[0].mode = e->versions[1].mode;
			hashcpy(e->versions[0].sha1, e->versions[1].sha1);
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
	const unsigned char *sha1,
	const uint16_t mode,
	struct tree_content *newtree)
{
	if (!S_ISDIR(mode))
		die("Root cannot be a non-directory");
	hashcpy(root->versions[1].sha1, sha1);
	if (root->tree)
		release_tree_content_recursive(root->tree);
	root->tree = newtree;
}

static int tree_content_set(
	struct tree_entry *root,
	const char *p,
	const unsigned char *sha1,
	const uint16_t mode,
	struct tree_content *subtree)
{
	struct tree_content *t;
	const char *slash1;
	unsigned int i, n;
	struct tree_entry *e;

	slash1 = strchr(p, '/');
	if (slash1)
		n = slash1 - p;
	else
		n = strlen(p);
	if (!n)
		die("Empty path component found in input");
	if (!slash1 && !S_ISDIR(mode) && subtree)
		die("Non-directories cannot have subtrees");

	if (!root->tree)
		load_tree(root);
	t = root->tree;
	for (i = 0; i < t->entry_count; i++) {
		e = t->entries[i];
		if (e->name->str_len == n && !strncmp_icase(p, e->name->str_dat, n)) {
			if (!slash1) {
				if (!S_ISDIR(mode)
						&& e->versions[1].mode == mode
						&& !hashcmp(e->versions[1].sha1, sha1))
					return 0;
				e->versions[1].mode = mode;
				hashcpy(e->versions[1].sha1, sha1);
				if (e->tree)
					release_tree_content_recursive(e->tree);
				e->tree = subtree;
				hashclr(root->versions[1].sha1);
				return 1;
			}
			if (!S_ISDIR(e->versions[1].mode)) {
				e->tree = new_tree_content(8);
				e->versions[1].mode = S_IFDIR;
			}
			if (!e->tree)
				load_tree(e);
			if (tree_content_set(e, slash1 + 1, sha1, mode, subtree)) {
				hashclr(root->versions[1].sha1);
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
	hashclr(e->versions[0].sha1);
	t->entries[t->entry_count++] = e;
	if (slash1) {
		e->tree = new_tree_content(8);
		e->versions[1].mode = S_IFDIR;
		tree_content_set(e, slash1 + 1, sha1, mode, subtree);
	} else {
		e->tree = subtree;
		e->versions[1].mode = mode;
		hashcpy(e->versions[1].sha1, sha1);
	}
	hashclr(root->versions[1].sha1);
	return 1;
}

static int tree_content_remove(
	struct tree_entry *root,
	const char *p,
	struct tree_entry *backup_leaf)
{
	struct tree_content *t;
	const char *slash1;
	unsigned int i, n;
	struct tree_entry *e;

	slash1 = strchr(p, '/');
	if (slash1)
		n = slash1 - p;
	else
		n = strlen(p);

	if (!root->tree)
		load_tree(root);
	t = root->tree;
	for (i = 0; i < t->entry_count; i++) {
		e = t->entries[i];
		if (e->name->str_len == n && !strncmp_icase(p, e->name->str_dat, n)) {
			if (slash1 && !S_ISDIR(e->versions[1].mode))
				/*
				 * If p names a file in some subdirectory, and a
				 * file or symlink matching the name of the
				 * parent directory of p exists, then p cannot
				 * exist and need not be deleted.
				 */
				return 1;
			if (!slash1 || !S_ISDIR(e->versions[1].mode))
				goto del_entry;
			if (!e->tree)
				load_tree(e);
			if (tree_content_remove(e, slash1 + 1, backup_leaf)) {
				for (n = 0; n < e->tree->entry_count; n++) {
					if (e->tree->entries[n]->versions[1].mode) {
						hashclr(root->versions[1].sha1);
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
	hashclr(e->versions[1].sha1);
	hashclr(root->versions[1].sha1);
	return 1;
}

static int tree_content_get(
	struct tree_entry *root,
	const char *p,
	struct tree_entry *leaf)
{
	struct tree_content *t;
	const char *slash1;
	unsigned int i, n;
	struct tree_entry *e;

	slash1 = strchr(p, '/');
	if (slash1)
		n = slash1 - p;
	else
		n = strlen(p);

	if (!root->tree)
		load_tree(root);
	t = root->tree;
	for (i = 0; i < t->entry_count; i++) {
		e = t->entries[i];
		if (e->name->str_len == n && !strncmp_icase(p, e->name->str_dat, n)) {
			if (!slash1) {
				memcpy(leaf, e, sizeof(*leaf));
				if (e->tree && is_null_sha1(e->versions[1].sha1))
					leaf->tree = dup_tree_content(e->tree);
				else
					leaf->tree = NULL;
				return 1;
			}
			if (!S_ISDIR(e->versions[1].mode))
				return 0;
			if (!e->tree)
				load_tree(e);
			return tree_content_get(e, slash1 + 1, leaf);
		}
	}
	return 0;
}

static int update_branch(struct branch *b)
{
	static const char *msg = "fast-import";
	struct ref_lock *lock;
	unsigned char old_sha1[20];

	if (is_null_sha1(b->sha1))
		return 0;
	if (read_ref(b->name, old_sha1))
		hashclr(old_sha1);
	lock = lock_any_ref_for_update(b->name, old_sha1, 0);
	if (!lock)
		return error("Unable to lock %s", b->name);
	if (!force_update && !is_null_sha1(old_sha1)) {
		struct commit *old_cmit, *new_cmit;

		old_cmit = lookup_commit_reference_gently(old_sha1, 0);
		new_cmit = lookup_commit_reference_gently(b->sha1, 0);
		if (!old_cmit || !new_cmit) {
			unlock_ref(lock);
			return error("Branch %s is missing commits.", b->name);
		}

		if (!in_merge_bases(old_cmit, &new_cmit, 1)) {
			unlock_ref(lock);
			warning("Not updating %s"
				" (new tip %s does not contain %s)",
				b->name, sha1_to_hex(b->sha1), sha1_to_hex(old_sha1));
			return -1;
		}
	}
	if (write_ref_sha1(lock, b->sha1, msg) < 0)
		return error("Unable to update %s", b->name);
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
	struct ref_lock *lock;
	char ref_name[PATH_MAX];

	for (t = first_tag; t; t = t->next_tag) {
		sprintf(ref_name, "tags/%s", t->name);
		lock = lock_ref_sha1(ref_name, NULL);
		if (!lock || write_ref_sha1(lock, t->sha1, msg) < 0)
			failure |= error("Unable to update %s", ref_name);
	}
}

static void dump_marks_helper(FILE *f,
	uintmax_t base,
	struct mark_set *m)
{
	uintmax_t k;
	if (m->shift) {
		for (k = 0; k < 1024; k++) {
			if (m->data.sets[k])
				dump_marks_helper(f, base + (k << m->shift),
					m->data.sets[k]);
		}
	} else {
		for (k = 0; k < 1024; k++) {
			if (m->data.marked[k])
				fprintf(f, ":%" PRIuMAX " %s\n", base + k,
					sha1_to_hex(m->data.marked[k]->idx.sha1));
		}
	}
}

static void dump_marks(void)
{
	static struct lock_file mark_lock;
	int mark_fd;
	FILE *f;

	if (!export_marks_file)
		return;

	mark_fd = hold_lock_file_for_update(&mark_lock, export_marks_file, 0);
	if (mark_fd < 0) {
		failure |= error("Unable to write marks file %s: %s",
			export_marks_file, strerror(errno));
		return;
	}

	f = fdopen(mark_fd, "w");
	if (!f) {
		int saved_errno = errno;
		rollback_lock_file(&mark_lock);
		failure |= error("Unable to write marks file %s: %s",
			export_marks_file, strerror(saved_errno));
		return;
	}

	/*
	 * Since the lock file was fdopen()'ed, it should not be close()'ed.
	 * Assign -1 to the lock file descriptor so that commit_lock_file()
	 * won't try to close() it.
	 */
	mark_lock.fd = -1;

	dump_marks_helper(f, 0, marks);
	if (ferror(f) || fclose(f)) {
		int saved_errno = errno;
		rollback_lock_file(&mark_lock);
		failure |= error("Unable to write marks file %s: %s",
			export_marks_file, strerror(saved_errno));
		return;
	}

	if (commit_lock_file(&mark_lock)) {
		int saved_errno = errno;
		rollback_lock_file(&mark_lock);
		failure |= error("Unable to commit marks file %s: %s",
			export_marks_file, strerror(saved_errno));
		return;
	}
}

static void read_marks(void)
{
	char line[512];
	FILE *f = fopen(import_marks_file, "r");
	if (f)
		;
	else if (import_marks_file_ignore_missing && errno == ENOENT)
		return; /* Marks file does not exist */
	else
		die_errno("cannot read '%s'", import_marks_file);
	while (fgets(line, sizeof(line), f)) {
		uintmax_t mark;
		char *end;
		unsigned char sha1[20];
		struct object_entry *e;

		end = strchr(line, '\n');
		if (line[0] != ':' || !end)
			die("corrupt mark line: %s", line);
		*end = 0;
		mark = strtoumax(line + 1, &end, 10);
		if (!mark || end == line + 1
			|| *end != ' ' || get_sha1(end + 1, sha1))
			die("corrupt mark line: %s", line);
		e = find_object(sha1);
		if (!e) {
			enum object_type type = sha1_object_info(sha1, NULL);
			if (type < 0)
				die("object not found: %s", sha1_to_hex(sha1));
			e = insert_object(sha1);
			e->type = type;
			e->pack_id = MAX_PACK_ID;
			e->idx.offset = 1; /* just not zero! */
		}
		insert_mark(mark, e);
	}
	fclose(f);
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

			strbuf_detach(&command_buf, NULL);
			stdin_eof = strbuf_getline(&command_buf, stdin, '\n');
			if (stdin_eof)
				return EOF;

			if (!seen_data_command
				&& prefixcmp(command_buf.buf, "feature ")
				&& prefixcmp(command_buf.buf, "option ")) {
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

			rc->buf = command_buf.buf;
			rc->prev = cmd_tail;
			rc->next = cmd_hist.prev;
			rc->prev->next = rc;
			cmd_tail = rc;
		}
		if (!prefixcmp(command_buf.buf, "cat-blob ")) {
			parse_cat_blob();
			continue;
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
	if (!prefixcmp(command_buf.buf, "mark :")) {
		next_mark = strtoumax(command_buf.buf + 6, NULL, 10);
		read_next_command();
	}
	else
		next_mark = 0;
}

static int parse_data(struct strbuf *sb, uintmax_t limit, uintmax_t *len_res)
{
	strbuf_reset(sb);

	if (prefixcmp(command_buf.buf, "data "))
		die("Expected 'data n' command, found: %s", command_buf.buf);

	if (!prefixcmp(command_buf.buf + 5, "<<")) {
		char *term = xstrdup(command_buf.buf + 5 + 2);
		size_t term_len = command_buf.len - 5 - 2;

		strbuf_detach(&command_buf, NULL);
		for (;;) {
			if (strbuf_getline(&command_buf, stdin, '\n') == EOF)
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
		uintmax_t len = strtoumax(command_buf.buf + 5, NULL, 10);
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

static int validate_raw_date(const char *src, char *result, int maxlen)
{
	const char *orig_src = src;
	char *endp;
	unsigned long num;

	errno = 0;

	num = strtoul(src, &endp, 10);
	/* NEEDSWORK: perhaps check for reasonable values? */
	if (errno || endp == src || *endp != ' ')
		return -1;

	src = endp + 1;
	if (*src != '-' && *src != '+')
		return -1;

	num = strtoul(src + 1, &endp, 10);
	if (errno || endp == src + 1 || *endp || (endp - orig_src) >= maxlen ||
	    1400 < num)
		return -1;

	strcpy(result, orig_src);
	return 0;
}

static char *parse_ident(const char *buf)
{
	const char *gt;
	size_t name_len;
	char *ident;

	gt = strrchr(buf, '>');
	if (!gt)
		die("Missing > in ident string: %s", buf);
	gt++;
	if (*gt != ' ')
		die("Missing space after > in ident string: %s", buf);
	gt++;
	name_len = gt - buf;
	ident = xmalloc(name_len + 24);
	strncpy(ident, buf, name_len);

	switch (whenspec) {
	case WHENSPEC_RAW:
		if (validate_raw_date(gt, ident + name_len, 24) < 0)
			die("Invalid raw date \"%s\" in ident: %s", gt, buf);
		break;
	case WHENSPEC_RFC2822:
		if (parse_date(gt, ident + name_len, 24) < 0)
			die("Invalid rfc2822 date \"%s\" in ident: %s", gt, buf);
		break;
	case WHENSPEC_NOW:
		if (strcmp("now", gt))
			die("Date in ident must be 'now': %s", buf);
		datestamp(ident + name_len, 24);
		break;
	}

	return ident;
}

static void parse_and_store_blob(
	struct last_object *last,
	unsigned char *sha1out,
	uintmax_t mark)
{
	static struct strbuf buf = STRBUF_INIT;
	uintmax_t len;

	if (parse_data(&buf, big_file_threshold, &len))
		store_object(OBJ_BLOB, &buf, last, sha1out, mark);
	else {
		if (last) {
			strbuf_release(&last->data);
			last->offset = 0;
			last->depth = 0;
		}
		stream_blob(len, sha1out, mark);
		skip_optional_lf();
	}
}

static void parse_new_blob(void)
{
	read_next_command();
	parse_mark();
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
	if (fanout >= 20)
		die("Too large fanout (%u)", fanout);
	while (fanout) {
		path[i++] = hex_sha1[j++];
		path[i++] = hex_sha1[j++];
		path[i++] = '/';
		fanout--;
	}
	memcpy(path + i, hex_sha1 + j, 40 - j);
	path[i + 40 - j] = '\0';
}

static uintmax_t do_change_note_fanout(
		struct tree_entry *orig_root, struct tree_entry *root,
		char *hex_sha1, unsigned int hex_sha1_len,
		char *fullpath, unsigned int fullpath_len,
		unsigned char fanout)
{
	struct tree_content *t = root->tree;
	struct tree_entry *e, leaf;
	unsigned int i, tmp_hex_sha1_len, tmp_fullpath_len;
	uintmax_t num_notes = 0;
	unsigned char sha1[20];
	char realpath[60];

	for (i = 0; t && i < t->entry_count; i++) {
		e = t->entries[i];
		tmp_hex_sha1_len = hex_sha1_len + e->name->str_len;
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
		    tmp_hex_sha1_len > 40 ||
		    e->name->str_len % 2)
			continue;

		/* This _may_ be a note entry, or a subdir containing notes */
		memcpy(hex_sha1 + hex_sha1_len, e->name->str_dat,
		       e->name->str_len);
		if (tmp_fullpath_len)
			fullpath[tmp_fullpath_len++] = '/';
		memcpy(fullpath + tmp_fullpath_len, e->name->str_dat,
		       e->name->str_len);
		tmp_fullpath_len += e->name->str_len;
		fullpath[tmp_fullpath_len] = '\0';

		if (tmp_hex_sha1_len == 40 && !get_sha1_hex(hex_sha1, sha1)) {
			/* This is a note entry */
			construct_path_with_fanout(hex_sha1, fanout, realpath);
			if (!strcmp(fullpath, realpath)) {
				/* Note entry is in correct location */
				num_notes++;
				continue;
			}

			/* Rename fullpath to realpath */
			if (!tree_content_remove(orig_root, fullpath, &leaf))
				die("Failed to remove path %s", fullpath);
			tree_content_set(orig_root, realpath,
				leaf.versions[1].sha1,
				leaf.versions[1].mode,
				leaf.tree);
		} else if (S_ISDIR(e->versions[1].mode)) {
			/* This is a subdir that may contain note entries */
			if (!e->tree)
				load_tree(e);
			num_notes += do_change_note_fanout(orig_root, e,
				hex_sha1, tmp_hex_sha1_len,
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
	char hex_sha1[40], path[60];
	return do_change_note_fanout(root, root, hex_sha1, 0, path, 0, fanout);
}

static void file_change_m(struct branch *b)
{
	const char *p = command_buf.buf + 2;
	static struct strbuf uq = STRBUF_INIT;
	const char *endp;
	struct object_entry *oe = oe;
	unsigned char sha1[20];
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
		char *x;
		oe = find_mark(strtoumax(p + 1, &x, 10));
		hashcpy(sha1, oe->idx.sha1);
		p = x;
	} else if (!prefixcmp(p, "inline")) {
		inline_data = 1;
		p += 6;
	} else {
		if (get_sha1_hex(p, sha1))
			die("Invalid SHA1: %s", command_buf.buf);
		oe = find_object(sha1);
		p += 40;
	}
	if (*p++ != ' ')
		die("Missing space after SHA1: %s", command_buf.buf);

	strbuf_reset(&uq);
	if (!unquote_c_style(&uq, p, &endp)) {
		if (*endp)
			die("Garbage after path in: %s", command_buf.buf);
		p = uq.buf;
	}

	/* Git does not track empty, non-toplevel directories. */
	if (S_ISDIR(mode) && !memcmp(sha1, EMPTY_TREE_SHA1_BIN, 20) && *p) {
		tree_content_remove(&b->branch_tree, p, NULL);
		return;
	}

	if (S_ISGITLINK(mode)) {
		if (inline_data)
			die("Git links cannot be specified 'inline': %s",
				command_buf.buf);
		else if (oe) {
			if (oe->type != OBJ_COMMIT)
				die("Not a commit (actually a %s): %s",
					typename(oe->type), command_buf.buf);
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
		read_next_command();
		parse_and_store_blob(&last_blob, sha1, 0);
	} else {
		enum object_type expected = S_ISDIR(mode) ?
						OBJ_TREE: OBJ_BLOB;
		enum object_type type = oe ? oe->type :
					sha1_object_info(sha1, NULL);
		if (type < 0)
			die("%s not found: %s",
					S_ISDIR(mode) ?  "Tree" : "Blob",
					command_buf.buf);
		if (type != expected)
			die("Not a %s (actually a %s): %s",
				typename(expected), typename(type),
				command_buf.buf);
	}

	if (!*p) {
		tree_content_replace(&b->branch_tree, sha1, mode, NULL);
		return;
	}
	tree_content_set(&b->branch_tree, p, sha1, mode, NULL);
}

static void file_change_d(struct branch *b)
{
	const char *p = command_buf.buf + 2;
	static struct strbuf uq = STRBUF_INIT;
	const char *endp;

	strbuf_reset(&uq);
	if (!unquote_c_style(&uq, p, &endp)) {
		if (*endp)
			die("Garbage after path in: %s", command_buf.buf);
		p = uq.buf;
	}
	tree_content_remove(&b->branch_tree, p, NULL);
}

static void file_change_cr(struct branch *b, int rename)
{
	const char *s, *d;
	static struct strbuf s_uq = STRBUF_INIT;
	static struct strbuf d_uq = STRBUF_INIT;
	const char *endp;
	struct tree_entry leaf;

	s = command_buf.buf + 2;
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
		tree_content_remove(&b->branch_tree, s, &leaf);
	else
		tree_content_get(&b->branch_tree, s, &leaf);
	if (!leaf.versions[1].mode)
		die("Path %s not in branch", s);
	if (!*d) {	/* C "path/to/subdir" "" */
		tree_content_replace(&b->branch_tree,
			leaf.versions[1].sha1,
			leaf.versions[1].mode,
			leaf.tree);
		return;
	}
	tree_content_set(&b->branch_tree, d,
		leaf.versions[1].sha1,
		leaf.versions[1].mode,
		leaf.tree);
}

static void note_change_n(struct branch *b, unsigned char old_fanout)
{
	const char *p = command_buf.buf + 2;
	static struct strbuf uq = STRBUF_INIT;
	struct object_entry *oe = oe;
	struct branch *s;
	unsigned char sha1[20], commit_sha1[20];
	char path[60];
	uint16_t inline_data = 0;
	unsigned char new_fanout;

	/* <dataref> or 'inline' */
	if (*p == ':') {
		char *x;
		oe = find_mark(strtoumax(p + 1, &x, 10));
		hashcpy(sha1, oe->idx.sha1);
		p = x;
	} else if (!prefixcmp(p, "inline")) {
		inline_data = 1;
		p += 6;
	} else {
		if (get_sha1_hex(p, sha1))
			die("Invalid SHA1: %s", command_buf.buf);
		oe = find_object(sha1);
		p += 40;
	}
	if (*p++ != ' ')
		die("Missing space after SHA1: %s", command_buf.buf);

	/* <committish> */
	s = lookup_branch(p);
	if (s) {
		hashcpy(commit_sha1, s->sha1);
	} else if (*p == ':') {
		uintmax_t commit_mark = strtoumax(p + 1, NULL, 10);
		struct object_entry *commit_oe = find_mark(commit_mark);
		if (commit_oe->type != OBJ_COMMIT)
			die("Mark :%" PRIuMAX " not a commit", commit_mark);
		hashcpy(commit_sha1, commit_oe->idx.sha1);
	} else if (!get_sha1(p, commit_sha1)) {
		unsigned long size;
		char *buf = read_object_with_reference(commit_sha1,
			commit_type, &size, commit_sha1);
		if (!buf || size < 46)
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
		parse_and_store_blob(&last_blob, sha1, 0);
	} else if (oe) {
		if (oe->type != OBJ_BLOB)
			die("Not a blob (actually a %s): %s",
				typename(oe->type), command_buf.buf);
	} else if (!is_null_sha1(sha1)) {
		enum object_type type = sha1_object_info(sha1, NULL);
		if (type < 0)
			die("Blob not found: %s", command_buf.buf);
		if (type != OBJ_BLOB)
			die("Not a blob (actually a %s): %s",
			    typename(type), command_buf.buf);
	}

	construct_path_with_fanout(sha1_to_hex(commit_sha1), old_fanout, path);
	if (tree_content_remove(&b->branch_tree, path, NULL))
		b->num_notes--;

	if (is_null_sha1(sha1))
		return; /* nothing to insert */

	b->num_notes++;
	new_fanout = convert_num_notes_to_fanout(b->num_notes);
	construct_path_with_fanout(sha1_to_hex(commit_sha1), new_fanout, path);
	tree_content_set(&b->branch_tree, path, sha1, S_IFREG | 0644, NULL);
}

static void file_change_deleteall(struct branch *b)
{
	release_tree_content_recursive(b->branch_tree.tree);
	hashclr(b->branch_tree.versions[0].sha1);
	hashclr(b->branch_tree.versions[1].sha1);
	load_tree(&b->branch_tree);
	b->num_notes = 0;
}

static void parse_from_commit(struct branch *b, char *buf, unsigned long size)
{
	if (!buf || size < 46)
		die("Not a valid commit: %s", sha1_to_hex(b->sha1));
	if (memcmp("tree ", buf, 5)
		|| get_sha1_hex(buf + 5, b->branch_tree.versions[1].sha1))
		die("The commit %s is corrupt", sha1_to_hex(b->sha1));
	hashcpy(b->branch_tree.versions[0].sha1,
		b->branch_tree.versions[1].sha1);
}

static void parse_from_existing(struct branch *b)
{
	if (is_null_sha1(b->sha1)) {
		hashclr(b->branch_tree.versions[0].sha1);
		hashclr(b->branch_tree.versions[1].sha1);
	} else {
		unsigned long size;
		char *buf;

		buf = read_object_with_reference(b->sha1,
			commit_type, &size, b->sha1);
		parse_from_commit(b, buf, size);
		free(buf);
	}
}

static int parse_from(struct branch *b)
{
	const char *from;
	struct branch *s;

	if (prefixcmp(command_buf.buf, "from "))
		return 0;

	if (b->branch_tree.tree) {
		release_tree_content_recursive(b->branch_tree.tree);
		b->branch_tree.tree = NULL;
	}

	from = strchr(command_buf.buf, ' ') + 1;
	s = lookup_branch(from);
	if (b == s)
		die("Can't create a branch from itself: %s", b->name);
	else if (s) {
		unsigned char *t = s->branch_tree.versions[1].sha1;
		hashcpy(b->sha1, s->sha1);
		hashcpy(b->branch_tree.versions[0].sha1, t);
		hashcpy(b->branch_tree.versions[1].sha1, t);
	} else if (*from == ':') {
		uintmax_t idnum = strtoumax(from + 1, NULL, 10);
		struct object_entry *oe = find_mark(idnum);
		if (oe->type != OBJ_COMMIT)
			die("Mark :%" PRIuMAX " not a commit", idnum);
		hashcpy(b->sha1, oe->idx.sha1);
		if (oe->pack_id != MAX_PACK_ID) {
			unsigned long size;
			char *buf = gfi_unpack_entry(oe, &size);
			parse_from_commit(b, buf, size);
			free(buf);
		} else
			parse_from_existing(b);
	} else if (!get_sha1(from, b->sha1))
		parse_from_existing(b);
	else
		die("Invalid ref name or SHA1 expression: %s", from);

	read_next_command();
	return 1;
}

static struct hash_list *parse_merge(unsigned int *count)
{
	struct hash_list *list = NULL, *n, *e = e;
	const char *from;
	struct branch *s;

	*count = 0;
	while (!prefixcmp(command_buf.buf, "merge ")) {
		from = strchr(command_buf.buf, ' ') + 1;
		n = xmalloc(sizeof(*n));
		s = lookup_branch(from);
		if (s)
			hashcpy(n->sha1, s->sha1);
		else if (*from == ':') {
			uintmax_t idnum = strtoumax(from + 1, NULL, 10);
			struct object_entry *oe = find_mark(idnum);
			if (oe->type != OBJ_COMMIT)
				die("Mark :%" PRIuMAX " not a commit", idnum);
			hashcpy(n->sha1, oe->idx.sha1);
		} else if (!get_sha1(from, n->sha1)) {
			unsigned long size;
			char *buf = read_object_with_reference(n->sha1,
				commit_type, &size, n->sha1);
			if (!buf || size < 46)
				die("Not a valid commit: %s", from);
			free(buf);
		} else
			die("Invalid ref name or SHA1 expression: %s", from);

		n->next = NULL;
		if (list)
			e->next = n;
		else
			list = n;
		e = n;
		(*count)++;
		read_next_command();
	}
	return list;
}

static void parse_new_commit(void)
{
	static struct strbuf msg = STRBUF_INIT;
	struct branch *b;
	char *sp;
	char *author = NULL;
	char *committer = NULL;
	struct hash_list *merge_list = NULL;
	unsigned int merge_count;
	unsigned char prev_fanout, new_fanout;

	/* Obtain the branch name from the rest of our command */
	sp = strchr(command_buf.buf, ' ') + 1;
	b = lookup_branch(sp);
	if (!b)
		b = new_branch(sp);

	read_next_command();
	parse_mark();
	if (!prefixcmp(command_buf.buf, "author ")) {
		author = parse_ident(command_buf.buf + 7);
		read_next_command();
	}
	if (!prefixcmp(command_buf.buf, "committer ")) {
		committer = parse_ident(command_buf.buf + 10);
		read_next_command();
	}
	if (!committer)
		die("Expected committer but didn't get one");
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
		if (!prefixcmp(command_buf.buf, "M "))
			file_change_m(b);
		else if (!prefixcmp(command_buf.buf, "D "))
			file_change_d(b);
		else if (!prefixcmp(command_buf.buf, "R "))
			file_change_cr(b, 1);
		else if (!prefixcmp(command_buf.buf, "C "))
			file_change_cr(b, 0);
		else if (!prefixcmp(command_buf.buf, "N "))
			note_change_n(b, prev_fanout);
		else if (!strcmp("deleteall", command_buf.buf))
			file_change_deleteall(b);
		else if (!prefixcmp(command_buf.buf, "ls "))
			parse_ls(b);
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
	hashcpy(b->branch_tree.versions[0].sha1,
		b->branch_tree.versions[1].sha1);

	strbuf_reset(&new_data);
	strbuf_addf(&new_data, "tree %s\n",
		sha1_to_hex(b->branch_tree.versions[1].sha1));
	if (!is_null_sha1(b->sha1))
		strbuf_addf(&new_data, "parent %s\n", sha1_to_hex(b->sha1));
	while (merge_list) {
		struct hash_list *next = merge_list->next;
		strbuf_addf(&new_data, "parent %s\n", sha1_to_hex(merge_list->sha1));
		free(merge_list);
		merge_list = next;
	}
	strbuf_addf(&new_data,
		"author %s\n"
		"committer %s\n"
		"\n",
		author ? author : committer, committer);
	strbuf_addbuf(&new_data, &msg);
	free(author);
	free(committer);

	if (!store_object(OBJ_COMMIT, &new_data, NULL, b->sha1, next_mark))
		b->pack_id = pack_id;
	b->last_commit = object_count_by_type[OBJ_COMMIT];
}

static void parse_new_tag(void)
{
	static struct strbuf msg = STRBUF_INIT;
	char *sp;
	const char *from;
	char *tagger;
	struct branch *s;
	struct tag *t;
	uintmax_t from_mark = 0;
	unsigned char sha1[20];
	enum object_type type;

	/* Obtain the new tag name from the rest of our command */
	sp = strchr(command_buf.buf, ' ') + 1;
	t = pool_alloc(sizeof(struct tag));
	t->next_tag = NULL;
	t->name = pool_strdup(sp);
	if (last_tag)
		last_tag->next_tag = t;
	else
		first_tag = t;
	last_tag = t;
	read_next_command();

	/* from ... */
	if (prefixcmp(command_buf.buf, "from "))
		die("Expected from command, got %s", command_buf.buf);
	from = strchr(command_buf.buf, ' ') + 1;
	s = lookup_branch(from);
	if (s) {
		hashcpy(sha1, s->sha1);
		type = OBJ_COMMIT;
	} else if (*from == ':') {
		struct object_entry *oe;
		from_mark = strtoumax(from + 1, NULL, 10);
		oe = find_mark(from_mark);
		type = oe->type;
		hashcpy(sha1, oe->idx.sha1);
	} else if (!get_sha1(from, sha1)) {
		unsigned long size;
		char *buf;

		buf = read_sha1_file(sha1, &type, &size);
		if (!buf || size < 46)
			die("Not a valid commit: %s", from);
		free(buf);
	} else
		die("Invalid ref name or SHA1 expression: %s", from);
	read_next_command();

	/* tagger ... */
	if (!prefixcmp(command_buf.buf, "tagger ")) {
		tagger = parse_ident(command_buf.buf + 7);
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
		    sha1_to_hex(sha1), typename(type), t->name);
	if (tagger)
		strbuf_addf(&new_data,
			    "tagger %s\n", tagger);
	strbuf_addch(&new_data, '\n');
	strbuf_addbuf(&new_data, &msg);
	free(tagger);

	if (store_object(OBJ_TAG, &new_data, NULL, t->sha1, 0))
		t->pack_id = MAX_PACK_ID;
	else
		t->pack_id = pack_id;
}

static void parse_reset_branch(void)
{
	struct branch *b;
	char *sp;

	/* Obtain the branch name from the rest of our command */
	sp = strchr(command_buf.buf, ' ') + 1;
	b = lookup_branch(sp);
	if (b) {
		hashclr(b->sha1);
		hashclr(b->branch_tree.versions[0].sha1);
		hashclr(b->branch_tree.versions[1].sha1);
		if (b->branch_tree.tree) {
			release_tree_content_recursive(b->branch_tree.tree);
			b->branch_tree.tree = NULL;
		}
	}
	else
		b = new_branch(sp);
	read_next_command();
	parse_from(b);
	if (command_buf.len > 0)
		unread_command_buf = 1;
}

static void cat_blob_write(const char *buf, unsigned long size)
{
	if (write_in_full(cat_blob_fd, buf, size) != size)
		die_errno("Write to frontend failed");
}

static void cat_blob(struct object_entry *oe, unsigned char sha1[20])
{
	struct strbuf line = STRBUF_INIT;
	unsigned long size;
	enum object_type type = 0;
	char *buf;

	if (!oe || oe->pack_id == MAX_PACK_ID) {
		buf = read_sha1_file(sha1, &type, &size);
	} else {
		type = oe->type;
		buf = gfi_unpack_entry(oe, &size);
	}

	/*
	 * Output based on batch_one_object() from cat-file.c.
	 */
	if (type <= 0) {
		strbuf_reset(&line);
		strbuf_addf(&line, "%s missing\n", sha1_to_hex(sha1));
		cat_blob_write(line.buf, line.len);
		strbuf_release(&line);
		free(buf);
		return;
	}
	if (!buf)
		die("Can't read object %s", sha1_to_hex(sha1));
	if (type != OBJ_BLOB)
		die("Object %s is a %s but a blob was expected.",
		    sha1_to_hex(sha1), typename(type));
	strbuf_reset(&line);
	strbuf_addf(&line, "%s %s %lu\n", sha1_to_hex(sha1),
						typename(type), size);
	cat_blob_write(line.buf, line.len);
	strbuf_release(&line);
	cat_blob_write(buf, size);
	cat_blob_write("\n", 1);
	free(buf);
}

static void parse_cat_blob(void)
{
	const char *p;
	struct object_entry *oe = oe;
	unsigned char sha1[20];

	/* cat-blob SP <object> LF */
	p = command_buf.buf + strlen("cat-blob ");
	if (*p == ':') {
		char *x;
		oe = find_mark(strtoumax(p + 1, &x, 10));
		if (x == p + 1)
			die("Invalid mark: %s", command_buf.buf);
		if (!oe)
			die("Unknown mark: %s", command_buf.buf);
		if (*x)
			die("Garbage after mark: %s", command_buf.buf);
		hashcpy(sha1, oe->idx.sha1);
	} else {
		if (get_sha1_hex(p, sha1))
			die("Invalid SHA1: %s", command_buf.buf);
		if (p[40])
			die("Garbage after SHA1: %s", command_buf.buf);
		oe = find_object(sha1);
	}

	cat_blob(oe, sha1);
}

static struct object_entry *dereference(struct object_entry *oe,
					unsigned char sha1[20])
{
	unsigned long size;
	char *buf = NULL;
	if (!oe) {
		enum object_type type = sha1_object_info(sha1, NULL);
		if (type < 0)
			die("object not found: %s", sha1_to_hex(sha1));
		/* cache it! */
		oe = insert_object(sha1);
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
		die("Not a treeish: %s", command_buf.buf);
	}

	if (oe->pack_id != MAX_PACK_ID) {	/* in a pack being written */
		buf = gfi_unpack_entry(oe, &size);
	} else {
		enum object_type unused;
		buf = read_sha1_file(sha1, &unused, &size);
	}
	if (!buf)
		die("Can't load object %s", sha1_to_hex(sha1));

	/* Peel one layer. */
	switch (oe->type) {
	case OBJ_TAG:
		if (size < 40 + strlen("object ") ||
		    get_sha1_hex(buf + strlen("object "), sha1))
			die("Invalid SHA1 in tag: %s", command_buf.buf);
		break;
	case OBJ_COMMIT:
		if (size < 40 + strlen("tree ") ||
		    get_sha1_hex(buf + strlen("tree "), sha1))
			die("Invalid SHA1 in commit: %s", command_buf.buf);
	}

	free(buf);
	return find_object(sha1);
}

static struct object_entry *parse_treeish_dataref(const char **p)
{
	unsigned char sha1[20];
	struct object_entry *e;

	if (**p == ':') {	/* <mark> */
		char *endptr;
		e = find_mark(strtoumax(*p + 1, &endptr, 10));
		if (endptr == *p + 1)
			die("Invalid mark: %s", command_buf.buf);
		if (!e)
			die("Unknown mark: %s", command_buf.buf);
		*p = endptr;
		hashcpy(sha1, e->idx.sha1);
	} else {	/* <sha1> */
		if (get_sha1_hex(*p, sha1))
			die("Invalid SHA1: %s", command_buf.buf);
		e = find_object(sha1);
		*p += 40;
	}

	while (!e || e->type != OBJ_TREE)
		e = dereference(e, sha1);
	return e;
}

static void print_ls(int mode, const unsigned char *sha1, const char *path)
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
				mode, type, sha1_to_hex(sha1));
		quote_c_style(path, &line, NULL, 0);
		strbuf_addch(&line, '\n');
	}
	cat_blob_write(line.buf, line.len);
}

static void parse_ls(struct branch *b)
{
	const char *p;
	struct tree_entry *root = NULL;
	struct tree_entry leaf = {NULL};

	/* ls SP (<treeish> SP)? <path> */
	p = command_buf.buf + strlen("ls ");
	if (*p == '"') {
		if (!b)
			die("Not in a commit: %s", command_buf.buf);
		root = &b->branch_tree;
	} else {
		struct object_entry *e = parse_treeish_dataref(&p);
		root = new_tree_entry();
		hashcpy(root->versions[1].sha1, e->idx.sha1);
		load_tree(root);
		if (*p++ != ' ')
			die("Missing space after tree-ish: %s", command_buf.buf);
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
	tree_content_get(root, p, &leaf);
	/*
	 * A directory in preparation would have a sha1 of zero
	 * until it is saved.  Save, for simplicity.
	 */
	if (S_ISDIR(leaf.versions[1].mode))
		store_tree(&leaf);

	print_ls(leaf.versions[1].mode, leaf.versions[1].sha1, p);
	if (!b || root != &b->branch_tree)
		release_tree_entry(root);
}

static void checkpoint(void)
{
	checkpoint_requested = 0;
	if (object_count) {
		cycle_packfile();
		dump_branches();
		dump_tags();
		dump_marks();
	}
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

static char* make_fast_import_path(const char *path)
{
	struct strbuf abs_path = STRBUF_INIT;

	if (!relative_marks_paths || is_absolute_path(path))
		return xstrdup(path);
	strbuf_addf(&abs_path, "%s/info/fast-import/%s", get_git_dir(), path);
	return strbuf_detach(&abs_path, NULL);
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
	safe_create_leading_directories_const(import_marks_file);
	import_marks_file_from_stream = from_stream;
	import_marks_file_ignore_missing = ignore_missing;
}

static void option_date_format(const char *fmt)
{
	if (!strcmp(fmt, "raw"))
		whenspec = WHENSPEC_RAW;
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
	safe_create_leading_directories_const(export_marks_file);
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
	pack_edges = fopen(edges, "a");
	if (!pack_edges)
		die_errno("Cannot open '%s'", edges);
}

static int parse_one_option(const char *option)
{
	if (!prefixcmp(option, "max-pack-size=")) {
		unsigned long v;
		if (!git_parse_ulong(option + 14, &v))
			return 0;
		if (v < 8192) {
			warning("max-pack-size is now in bytes, assuming --max-pack-size=%lum", v);
			v *= 1024 * 1024;
		} else if (v < 1024 * 1024) {
			warning("minimum max-pack-size is 1 MiB");
			v = 1024 * 1024;
		}
		max_packsize = v;
	} else if (!prefixcmp(option, "big-file-threshold=")) {
		unsigned long v;
		if (!git_parse_ulong(option + 19, &v))
			return 0;
		big_file_threshold = v;
	} else if (!prefixcmp(option, "depth=")) {
		option_depth(option + 6);
	} else if (!prefixcmp(option, "active-branches=")) {
		option_active_branches(option + 16);
	} else if (!prefixcmp(option, "export-pack-edges=")) {
		option_export_pack_edges(option + 18);
	} else if (!prefixcmp(option, "quiet")) {
		show_stats = 0;
	} else if (!prefixcmp(option, "stats")) {
		show_stats = 1;
	} else {
		return 0;
	}

	return 1;
}

static int parse_one_feature(const char *feature, int from_stream)
{
	if (!prefixcmp(feature, "date-format=")) {
		option_date_format(feature + 12);
	} else if (!prefixcmp(feature, "import-marks=")) {
		option_import_marks(feature + 13, from_stream, 0);
	} else if (!prefixcmp(feature, "import-marks-if-exists=")) {
		option_import_marks(feature + strlen("import-marks-if-exists="),
					from_stream, 1);
	} else if (!prefixcmp(feature, "export-marks=")) {
		option_export_marks(feature + 13);
	} else if (!strcmp(feature, "cat-blob")) {
		; /* Don't die - this feature is supported */
	} else if (!strcmp(feature, "relative-marks")) {
		relative_marks_paths = 1;
	} else if (!strcmp(feature, "no-relative-marks")) {
		relative_marks_paths = 0;
	} else if (!strcmp(feature, "force")) {
		force_update = 1;
	} else if (!strcmp(feature, "notes") || !strcmp(feature, "ls")) {
		; /* do nothing; we have the feature */
	} else {
		return 0;
	}

	return 1;
}

static void parse_feature(void)
{
	char *feature = command_buf.buf + 8;

	if (seen_data_command)
		die("Got feature command '%s' after data command", feature);

	if (parse_one_feature(feature, 1))
		return;

	die("This version of fast-import does not support feature %s.", feature);
}

static void parse_option(void)
{
	char *option = command_buf.buf + 11;

	if (seen_data_command)
		die("Got option command '%s' after data command", option);

	if (parse_one_option(option))
		return;

	die("This version of fast-import does not support option: %s", option);
}

static int git_pack_config(const char *k, const char *v, void *cb)
{
	if (!strcmp(k, "pack.depth")) {
		max_depth = git_config_int(k, v);
		if (max_depth > MAX_DEPTH)
			max_depth = MAX_DEPTH;
		return 0;
	}
	if (!strcmp(k, "pack.compression")) {
		int level = git_config_int(k, v);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die("bad pack compression level %d", level);
		pack_compression_level = level;
		pack_compression_seen = 1;
		return 0;
	}
	if (!strcmp(k, "pack.indexversion")) {
		pack_idx_default_version = git_config_int(k, v);
		if (pack_idx_default_version > 2)
			die("bad pack.indexversion=%"PRIu32,
			    pack_idx_default_version);
		return 0;
	}
	if (!strcmp(k, "pack.packsizelimit")) {
		max_packsize = git_config_ulong(k, v);
		return 0;
	}
	return git_default_config(k, v, cb);
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

		if (parse_one_option(a + 2))
			continue;

		if (parse_one_feature(a + 2, 0))
			continue;

		if (!prefixcmp(a + 2, "cat-blob-fd=")) {
			option_cat_blob_fd(a + 2 + strlen("cat-blob-fd="));
			continue;
		}

		die("unknown option %s", a);
	}
	if (i != global_argc)
		usage(fast_import_usage);

	seen_data_command = 1;
	if (import_marks_file)
		read_marks();
}

int main(int argc, const char **argv)
{
	unsigned int i;

	git_extract_argv0_path(argv[0]);

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(fast_import_usage);

	setup_git_directory();
	git_config(git_pack_config, NULL);
	if (!pack_compression_seen && core_compression_seen)
		pack_compression_level = core_compression_level;

	alloc_objects(object_entry_alloc);
	strbuf_init(&command_buf, 0);
	atom_table = xcalloc(atom_table_sz, sizeof(struct atom_str*));
	branch_table = xcalloc(branch_table_sz, sizeof(struct branch*));
	avail_tree_table = xcalloc(avail_tree_table_sz, sizeof(struct avail_tree_content*));
	marks = pool_calloc(1, sizeof(struct mark_set));

	global_argc = argc;
	global_argv = argv;

	rc_free = pool_alloc(cmd_save * sizeof(*rc_free));
	for (i = 0; i < (cmd_save - 1); i++)
		rc_free[i].next = &rc_free[i + 1];
	rc_free[cmd_save - 1].next = NULL;

	prepare_packed_git();
	start_packfile();
	set_die_routine(die_nicely);
	set_checkpoint_signal();
	while (read_next_command() != EOF) {
		if (!strcmp("blob", command_buf.buf))
			parse_new_blob();
		else if (!prefixcmp(command_buf.buf, "ls "))
			parse_ls(NULL);
		else if (!prefixcmp(command_buf.buf, "commit "))
			parse_new_commit();
		else if (!prefixcmp(command_buf.buf, "tag "))
			parse_new_tag();
		else if (!prefixcmp(command_buf.buf, "reset "))
			parse_reset_branch();
		else if (!strcmp("checkpoint", command_buf.buf))
			parse_checkpoint();
		else if (!prefixcmp(command_buf.buf, "progress "))
			parse_progress();
		else if (!prefixcmp(command_buf.buf, "feature "))
			parse_feature();
		else if (!prefixcmp(command_buf.buf, "option git "))
			parse_option();
		else if (!prefixcmp(command_buf.buf, "option "))
			/* ignore non-git options*/;
		else
			die("Unsupported command: %s", command_buf.buf);

		if (checkpoint_requested)
			checkpoint();
	}

	/* argv hasn't been parsed yet, do so */
	if (!seen_data_command)
		parse_argv();

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
		fprintf(stderr, "      blobs  :   %10" PRIuMAX " (%10" PRIuMAX " duplicates %10" PRIuMAX " deltas)\n", object_count_by_type[OBJ_BLOB], duplicate_count_by_type[OBJ_BLOB], delta_count_by_type[OBJ_BLOB]);
		fprintf(stderr, "      trees  :   %10" PRIuMAX " (%10" PRIuMAX " duplicates %10" PRIuMAX " deltas)\n", object_count_by_type[OBJ_TREE], duplicate_count_by_type[OBJ_TREE], delta_count_by_type[OBJ_TREE]);
		fprintf(stderr, "      commits:   %10" PRIuMAX " (%10" PRIuMAX " duplicates %10" PRIuMAX " deltas)\n", object_count_by_type[OBJ_COMMIT], duplicate_count_by_type[OBJ_COMMIT], delta_count_by_type[OBJ_COMMIT]);
		fprintf(stderr, "      tags   :   %10" PRIuMAX " (%10" PRIuMAX " duplicates %10" PRIuMAX " deltas)\n", object_count_by_type[OBJ_TAG], duplicate_count_by_type[OBJ_TAG], delta_count_by_type[OBJ_TAG]);
		fprintf(stderr, "Total branches:  %10lu (%10lu loads     )\n", branch_count, branch_load_count);
		fprintf(stderr, "      marks:     %10" PRIuMAX " (%10" PRIuMAX " unique    )\n", (((uintmax_t)1) << marks->shift) * 1024, marks_set_count);
		fprintf(stderr, "      atoms:     %10u\n", atom_cnt);
		fprintf(stderr, "Memory total:    %10" PRIuMAX " KiB\n", (total_allocd + alloc_count*sizeof(struct object_entry))/1024);
		fprintf(stderr, "       pools:    %10lu KiB\n", (unsigned long)(total_allocd/1024));
		fprintf(stderr, "     objects:    %10" PRIuMAX " KiB\n", (alloc_count*sizeof(struct object_entry))/1024);
		fprintf(stderr, "---------------------------------------------------------------------\n");
		pack_report();
		fprintf(stderr, "---------------------------------------------------------------------\n");
		fprintf(stderr, "\n");
	}

	return failure ? 1 : 0;
}
