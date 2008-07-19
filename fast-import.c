/*
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
    ('author' sp name '<' email '>' when lf)?
    'committer' sp name '<' email '>' when lf
    commit_msg
    ('from' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf)?
    ('merge' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf)*
    file_change*
    lf?;
  commit_msg ::= data;

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

  new_tag ::= 'tag' sp tag_str lf
    'from' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf
    'tagger' sp name '<' email '>' when lf
    tag_msg;
  tag_msg ::= data;

  reset_branch ::= 'reset' sp ref_str lf
    ('from' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf)?
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
     # declen does not include the lf preceeding the binary data.
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

     # note: comments may appear anywhere in the input, except
     # within a data command.  Any form of the data command
     # always escapes the related input from comment processing.
     #
     # In case it is not clear, the '#' that starts the comment
     # must be the first character on that the line (an lf have
     # preceeded it).
     #
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

#define PACK_ID_BITS 16
#define MAX_PACK_ID ((1<<PACK_ID_BITS)-1)
#define DEPTH_BITS 13
#define MAX_DEPTH ((1<<DEPTH_BITS)-1)

struct object_entry
{
	struct object_entry *next;
	uint32_t offset;
	uint32_t type : TYPE_BITS,
		pack_id : PACK_ID_BITS,
		depth : DEPTH_BITS;
	unsigned char sha1[20];
};

struct object_entry_pool
{
	struct object_entry_pool *next_pool;
	struct object_entry *next_free;
	struct object_entry *end;
	struct object_entry entries[FLEX_ARRAY]; /* more */
};

struct mark_set
{
	union {
		struct object_entry *marked[1024];
		struct mark_set *sets[1024];
	} data;
	unsigned int shift;
};

struct last_object
{
	struct strbuf data;
	uint32_t offset;
	unsigned int depth;
	unsigned no_swap : 1;
};

struct mem_pool
{
	struct mem_pool *next_pool;
	char *next_free;
	char *end;
	uintmax_t space[FLEX_ARRAY]; /* more */
};

struct atom_str
{
	struct atom_str *next_atom;
	unsigned short str_len;
	char str_dat[FLEX_ARRAY]; /* more */
};

struct tree_content;
struct tree_entry
{
	struct tree_content *tree;
	struct atom_str* name;
	struct tree_entry_ms
	{
		uint16_t mode;
		unsigned char sha1[20];
	} versions[2];
};

struct tree_content
{
	unsigned int entry_capacity; /* must match avail_tree_content */
	unsigned int entry_count;
	unsigned int delta_depth;
	struct tree_entry *entries[FLEX_ARRAY]; /* more */
};

struct avail_tree_content
{
	unsigned int entry_capacity; /* must match tree_content */
	struct avail_tree_content *next_avail;
};

struct branch
{
	struct branch *table_next_branch;
	struct branch *active_next_branch;
	const char *name;
	struct tree_entry branch_tree;
	uintmax_t last_commit;
	unsigned active : 1;
	unsigned pack_id : PACK_ID_BITS;
	unsigned char sha1[20];
};

struct tag
{
	struct tag *next_tag;
	const char *name;
	unsigned int pack_id;
	unsigned char sha1[20];
};

struct hash_list
{
	struct hash_list *next;
	unsigned char sha1[20];
};

typedef enum {
	WHENSPEC_RAW = 1,
	WHENSPEC_RFC2822,
	WHENSPEC_NOW,
} whenspec_type;

struct recent_command
{
	struct recent_command *prev;
	struct recent_command *next;
	char *buf;
};

/* Configured limits on output */
static unsigned long max_depth = 10;
static off_t max_packsize = (1LL << 32) - 1;
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
static struct packed_git *pack_data;
static struct packed_git **all_packs;
static unsigned long pack_size;

/* Table of objects we've written. */
static unsigned int object_entry_alloc = 5000;
static struct object_entry_pool *blocks;
static struct object_entry *object_table[1 << 16];
static struct mark_set *marks;
static const char* mark_file;

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
	char *loc = git_path("fast_import_crash_%d", getpid());
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
	fprintf(rpt, "    fast-import process: %d\n", getpid());
	fprintf(rpt, "    parent process     : %d\n", getppid());
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
	if (mark_file)
		fprintf(rpt, "  exported to %s\n", mark_file);
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
	hashcpy(e->sha1, sha1);
	return e;
}

static struct object_entry *find_object(unsigned char *sha1)
{
	unsigned int h = sha1[0] << 8 | sha1[1];
	struct object_entry *e;
	for (e = object_table[h]; e; e = e->next)
		if (!hashcmp(sha1, e->sha1))
			return e;
	return NULL;
}

static struct object_entry *insert_object(unsigned char *sha1)
{
	unsigned int h = sha1[0] << 8 | sha1[1];
	struct object_entry *e = object_table[h];
	struct object_entry *p = NULL;

	while (e) {
		if (!hashcmp(sha1, e->sha1))
			return e;
		p = e;
		e = e->next;
	}

	e = new_object(sha1);
	e->next = NULL;
	e->offset = 0;
	if (p)
		p->next = e;
	else
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
	/* round out to a 'uintmax_t' alignment */
	if (len & (sizeof(uintmax_t) - 1))
		len += sizeof(uintmax_t) - (len & (sizeof(uintmax_t) - 1));
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
	struct branch* b = lookup_branch(name);

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

	snprintf(tmpfile, sizeof(tmpfile),
		"%s/tmp_pack_XXXXXX", get_object_directory());
	pack_fd = xmkstemp(tmpfile);
	p = xcalloc(1, sizeof(*p) + strlen(tmpfile) + 2);
	strcpy(p->pack_name, tmpfile);
	p->pack_fd = pack_fd;

	hdr.hdr_signature = htonl(PACK_SIGNATURE);
	hdr.hdr_version = htonl(2);
	hdr.hdr_entries = 0;
	write_or_die(p->pack_fd, &hdr, sizeof(hdr));

	pack_data = p;
	pack_size = sizeof(hdr);
	object_count = 0;

	all_packs = xrealloc(all_packs, sizeof(*all_packs) * (pack_id + 1));
	all_packs[pack_id] = p;
}

static int oecmp (const void *a_, const void *b_)
{
	struct object_entry *a = *((struct object_entry**)a_);
	struct object_entry *b = *((struct object_entry**)b_);
	return hashcmp(a->sha1, b->sha1);
}

static char *create_index(void)
{
	static char tmpfile[PATH_MAX];
	SHA_CTX ctx;
	struct sha1file *f;
	struct object_entry **idx, **c, **last, *e;
	struct object_entry_pool *o;
	uint32_t array[256];
	int i, idx_fd;

	/* Build the sorted table of object IDs. */
	idx = xmalloc(object_count * sizeof(struct object_entry*));
	c = idx;
	for (o = blocks; o; o = o->next_pool)
		for (e = o->next_free; e-- != o->entries;)
			if (pack_id == e->pack_id)
				*c++ = e;
	last = idx + object_count;
	if (c != last)
		die("internal consistency error creating the index");
	qsort(idx, object_count, sizeof(struct object_entry*), oecmp);

	/* Generate the fan-out array. */
	c = idx;
	for (i = 0; i < 256; i++) {
		struct object_entry **next = c;;
		while (next < last) {
			if ((*next)->sha1[0] != i)
				break;
			next++;
		}
		array[i] = htonl(next - idx);
		c = next;
	}

	snprintf(tmpfile, sizeof(tmpfile),
		"%s/tmp_idx_XXXXXX", get_object_directory());
	idx_fd = xmkstemp(tmpfile);
	f = sha1fd(idx_fd, tmpfile);
	sha1write(f, array, 256 * sizeof(int));
	SHA1_Init(&ctx);
	for (c = idx; c != last; c++) {
		uint32_t offset = htonl((*c)->offset);
		sha1write(f, &offset, 4);
		sha1write(f, (*c)->sha1, sizeof((*c)->sha1));
		SHA1_Update(&ctx, (*c)->sha1, 20);
	}
	sha1write(f, pack_data->sha1, sizeof(pack_data->sha1));
	sha1close(f, NULL, CSUM_FSYNC);
	free(idx);
	SHA1_Final(pack_data->sha1, &ctx);
	return tmpfile;
}

static char *keep_pack(char *curr_index_name)
{
	static char name[PATH_MAX];
	static const char *keep_msg = "fast-import";
	int keep_fd;

	chmod(pack_data->pack_name, 0444);
	chmod(curr_index_name, 0444);

	snprintf(name, sizeof(name), "%s/pack/pack-%s.keep",
		 get_object_directory(), sha1_to_hex(pack_data->sha1));
	keep_fd = open(name, O_RDWR|O_CREAT|O_EXCL, 0600);
	if (keep_fd < 0)
		die("cannot create keep file");
	write_or_die(keep_fd, keep_msg, strlen(keep_msg));
	if (close(keep_fd))
		die("failed to write keep file");

	snprintf(name, sizeof(name), "%s/pack/pack-%s.pack",
		 get_object_directory(), sha1_to_hex(pack_data->sha1));
	if (move_temp_to_file(pack_data->pack_name, name))
		die("cannot store pack file");

	snprintf(name, sizeof(name), "%s/pack/pack-%s.idx",
		 get_object_directory(), sha1_to_hex(pack_data->sha1));
	if (move_temp_to_file(curr_index_name, name))
		die("cannot store index file");
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
		unlink(name);
	}
}

static void end_packfile(void)
{
	struct packed_git *old_p = pack_data, *new_p;

	if (object_count) {
		char *idx_name;
		int i;
		struct branch *b;
		struct tag *t;

		close_pack_windows(pack_data);
		fixup_pack_header_footer(pack_data->pack_fd, pack_data->sha1,
				    pack_data->pack_name, object_count);
		close(pack_data->pack_fd);
		idx_name = keep_pack(create_index());

		/* Register the packfile with core git's machinary. */
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
	else
		unlink(old_p->pack_name);
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

static size_t encode_header(
	enum object_type type,
	size_t size,
	unsigned char *hdr)
{
	int n = 1;
	unsigned char c;

	if (type < OBJ_COMMIT || type > OBJ_REF_DELTA)
		die("bad type %d", type);

	c = (type << 4) | (size & 15);
	size >>= 4;
	while (size) {
		*hdr++ = c | 0x80;
		c = size & 0x7f;
		size >>= 7;
		n++;
	}
	*hdr = c;
	return n;
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
	SHA_CTX c;
	z_stream s;

	hdrlen = sprintf((char*)hdr,"%s %lu", typename(type),
		(unsigned long)dat->len) + 1;
	SHA1_Init(&c);
	SHA1_Update(&c, hdr, hdrlen);
	SHA1_Update(&c, dat->buf, dat->len);
	SHA1_Final(sha1, &c);
	if (sha1out)
		hashcpy(sha1out, sha1);

	e = insert_object(sha1);
	if (mark)
		insert_mark(mark, e);
	if (e->offset) {
		duplicate_count_by_type[type]++;
		return 1;
	} else if (find_sha1_pack(sha1, packed_git)) {
		e->type = type;
		e->pack_id = MAX_PACK_ID;
		e->offset = 1; /* just not zero! */
		duplicate_count_by_type[type]++;
		return 1;
	}

	if (last && last->data.buf && last->depth < max_depth) {
		delta = diff_delta(last->data.buf, last->data.len,
			dat->buf, dat->len,
			&deltalen, 0);
		if (delta && deltalen >= dat->len) {
			free(delta);
			delta = NULL;
		}
	} else
		delta = NULL;

	memset(&s, 0, sizeof(s));
	deflateInit(&s, pack_compression_level);
	if (delta) {
		s.next_in = delta;
		s.avail_in = deltalen;
	} else {
		s.next_in = (void *)dat->buf;
		s.avail_in = dat->len;
	}
	s.avail_out = deflateBound(&s, s.avail_in);
	s.next_out = out = xmalloc(s.avail_out);
	while (deflate(&s, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&s);

	/* Determine if we should auto-checkpoint. */
	if ((pack_size + 60 + s.total_out) > max_packsize
		|| (pack_size + 60 + s.total_out) < pack_size) {

		/* This new object needs to *not* have the current pack_id. */
		e->pack_id = pack_id + 1;
		cycle_packfile();

		/* We cannot carry a delta into the new pack. */
		if (delta) {
			free(delta);
			delta = NULL;

			memset(&s, 0, sizeof(s));
			deflateInit(&s, pack_compression_level);
			s.next_in = (void *)dat->buf;
			s.avail_in = dat->len;
			s.avail_out = deflateBound(&s, s.avail_in);
			s.next_out = out = xrealloc(out, s.avail_out);
			while (deflate(&s, Z_FINISH) == Z_OK)
				/* nothing */;
			deflateEnd(&s);
		}
	}

	e->type = type;
	e->pack_id = pack_id;
	e->offset = pack_size;
	object_count++;
	object_count_by_type[type]++;

	if (delta) {
		unsigned long ofs = e->offset - last->offset;
		unsigned pos = sizeof(hdr) - 1;

		delta_count_by_type[type]++;
		e->depth = last->depth + 1;

		hdrlen = encode_header(OBJ_OFS_DELTA, deltalen, hdr);
		write_or_die(pack_data->pack_fd, hdr, hdrlen);
		pack_size += hdrlen;

		hdr[pos] = ofs & 127;
		while (ofs >>= 7)
			hdr[--pos] = 128 | (--ofs & 127);
		write_or_die(pack_data->pack_fd, hdr + pos, sizeof(hdr) - pos);
		pack_size += sizeof(hdr) - pos;
	} else {
		e->depth = 0;
		hdrlen = encode_header(type, dat->len, hdr);
		write_or_die(pack_data->pack_fd, hdr, hdrlen);
		pack_size += hdrlen;
	}

	write_or_die(pack_data->pack_fd, out, s.total_out);
	pack_size += s.total_out;

	free(out);
	free(delta);
	if (last) {
		if (last->no_swap) {
			last->data = *dat;
		} else {
			strbuf_swap(&last->data, dat);
		}
		last->offset = e->offset;
		last->depth = e->depth;
	}
	return 0;
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

		/* We have to offer 20 bytes additional on the end of
		 * the packfile as the core unpacker code assumes the
		 * footer is present at the file end and must promise
		 * at least 20 bytes within any window it maps.  But
		 * we don't actually create the footer here.
		 */
		p->pack_size = pack_size + 20;
	}
	return unpack_entry(p, oe->offset, &type, sizep);
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
	unsigned char* sha1 = root->versions[1].sha1;
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
		hashcpy(e->versions[0].sha1, (unsigned char*)c);
		hashcpy(e->versions[1].sha1, (unsigned char*)c);
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
		lo.offset = le->offset;
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

static int tree_content_set(
	struct tree_entry *root,
	const char *p,
	const unsigned char *sha1,
	const uint16_t mode,
	struct tree_content *subtree)
{
	struct tree_content *t = root->tree;
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

	for (i = 0; i < t->entry_count; i++) {
		e = t->entries[i];
		if (e->name->str_len == n && !strncmp(p, e->name->str_dat, n)) {
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
	struct tree_content *t = root->tree;
	const char *slash1;
	unsigned int i, n;
	struct tree_entry *e;

	slash1 = strchr(p, '/');
	if (slash1)
		n = slash1 - p;
	else
		n = strlen(p);

	for (i = 0; i < t->entry_count; i++) {
		e = t->entries[i];
		if (e->name->str_len == n && !strncmp(p, e->name->str_dat, n)) {
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
	struct tree_content *t = root->tree;
	const char *slash1;
	unsigned int i, n;
	struct tree_entry *e;

	slash1 = strchr(p, '/');
	if (slash1)
		n = slash1 - p;
	else
		n = strlen(p);

	for (i = 0; i < t->entry_count; i++) {
		e = t->entries[i];
		if (e->name->str_len == n && !strncmp(p, e->name->str_dat, n)) {
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
				dump_marks_helper(f, (base + k) << m->shift,
					m->data.sets[k]);
		}
	} else {
		for (k = 0; k < 1024; k++) {
			if (m->data.marked[k])
				fprintf(f, ":%" PRIuMAX " %s\n", base + k,
					sha1_to_hex(m->data.marked[k]->sha1));
		}
	}
}

static void dump_marks(void)
{
	static struct lock_file mark_lock;
	int mark_fd;
	FILE *f;

	if (!mark_file)
		return;

	mark_fd = hold_lock_file_for_update(&mark_lock, mark_file, 0);
	if (mark_fd < 0) {
		failure |= error("Unable to write marks file %s: %s",
			mark_file, strerror(errno));
		return;
	}

	f = fdopen(mark_fd, "w");
	if (!f) {
		int saved_errno = errno;
		rollback_lock_file(&mark_lock);
		failure |= error("Unable to write marks file %s: %s",
			mark_file, strerror(saved_errno));
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
			mark_file, strerror(saved_errno));
		return;
	}

	if (commit_lock_file(&mark_lock)) {
		int saved_errno = errno;
		rollback_lock_file(&mark_lock);
		failure |= error("Unable to commit marks file %s: %s",
			mark_file, strerror(saved_errno));
		return;
	}
}

static int read_next_command(void)
{
	static int stdin_eof = 0;

	if (stdin_eof) {
		unread_command_buf = 0;
		return EOF;
	}

	do {
		if (unread_command_buf) {
			unread_command_buf = 0;
		} else {
			struct recent_command *rc;

			strbuf_detach(&command_buf, NULL);
			stdin_eof = strbuf_getline(&command_buf, stdin, '\n');
			if (stdin_eof)
				return EOF;

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
	} while (command_buf.buf[0] == '#');

	return 0;
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

static void parse_data(struct strbuf *sb)
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
		size_t n = 0, length;

		length = strtoul(command_buf.buf + 5, NULL, 10);

		while (n < length) {
			size_t s = strbuf_fread(sb, length - n, stdin);
			if (!s && feof(stdin))
				die("EOF in data (%lu bytes remaining)",
					(unsigned long)(length - n));
			n += s;
		}
	}

	skip_optional_lf();
}

static int validate_raw_date(const char *src, char *result, int maxlen)
{
	const char *orig_src = src;
	char *endp, sign;

	strtoul(src, &endp, 10);
	if (endp == src || *endp != ' ')
		return -1;

	src = endp + 1;
	if (*src != '-' && *src != '+')
		return -1;
	sign = *src;

	strtoul(src + 1, &endp, 10);
	if (endp == src || *endp || (endp - orig_src) >= maxlen)
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

static void parse_new_blob(void)
{
	static struct strbuf buf = STRBUF_INIT;

	read_next_command();
	parse_mark();
	parse_data(&buf);
	store_object(OBJ_BLOB, &buf, &last_blob, NULL, next_mark);
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
	case S_IFREG | 0644:
	case S_IFREG | 0755:
	case S_IFLNK:
	case S_IFGITLINK:
	case 0644:
	case 0755:
		/* ok */
		break;
	default:
		die("Corrupt mode: %s", command_buf.buf);
	}

	if (*p == ':') {
		char *x;
		oe = find_mark(strtoumax(p + 1, &x, 10));
		hashcpy(sha1, oe->sha1);
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
		static struct strbuf buf = STRBUF_INIT;

		if (p != uq.buf) {
			strbuf_addstr(&uq, p);
			p = uq.buf;
		}
		read_next_command();
		parse_data(&buf);
		store_object(OBJ_BLOB, &buf, &last_blob, sha1, 0);
	} else if (oe) {
		if (oe->type != OBJ_BLOB)
			die("Not a blob (actually a %s): %s",
				typename(oe->type), command_buf.buf);
	} else {
		enum object_type type = sha1_object_info(sha1, NULL);
		if (type < 0)
			die("Blob not found: %s", command_buf.buf);
		if (type != OBJ_BLOB)
			die("Not a blob (actually a %s): %s",
			    typename(type), command_buf.buf);
	}

	tree_content_set(&b->branch_tree, p, sha1, S_IFREG | mode, NULL);
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
	tree_content_set(&b->branch_tree, d,
		leaf.versions[1].sha1,
		leaf.versions[1].mode,
		leaf.tree);
}

static void file_change_deleteall(struct branch *b)
{
	release_tree_content_recursive(b->branch_tree.tree);
	hashclr(b->branch_tree.versions[0].sha1);
	hashclr(b->branch_tree.versions[1].sha1);
	load_tree(&b->branch_tree);
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
		hashcpy(b->sha1, oe->sha1);
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
			hashcpy(n->sha1, oe->sha1);
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
	parse_data(&msg);
	read_next_command();
	parse_from(b);
	merge_list = parse_merge(&merge_count);

	/* ensure the branch is active/loaded */
	if (!b->branch_tree.tree || !max_active_branches) {
		unload_one_branch();
		load_branch(b);
	}

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
		else if (!strcmp("deleteall", command_buf.buf))
			file_change_deleteall(b);
		else {
			unread_command_buf = 1;
			break;
		}
		if (read_next_command() == EOF)
			break;
	}

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
	} else if (*from == ':') {
		struct object_entry *oe;
		from_mark = strtoumax(from + 1, NULL, 10);
		oe = find_mark(from_mark);
		if (oe->type != OBJ_COMMIT)
			die("Mark :%" PRIuMAX " not a commit", from_mark);
		hashcpy(sha1, oe->sha1);
	} else if (!get_sha1(from, sha1)) {
		unsigned long size;
		char *buf;

		buf = read_object_with_reference(sha1,
			commit_type, &size, sha1);
		if (!buf || size < 46)
			die("Not a valid commit: %s", from);
		free(buf);
	} else
		die("Invalid ref name or SHA1 expression: %s", from);
	read_next_command();

	/* tagger ... */
	if (prefixcmp(command_buf.buf, "tagger "))
		die("Expected tagger command, got %s", command_buf.buf);
	tagger = parse_ident(command_buf.buf + 7);

	/* tag payload/message */
	read_next_command();
	parse_data(&msg);

	/* build the tag object */
	strbuf_reset(&new_data);
	strbuf_addf(&new_data,
		"object %s\n"
		"type %s\n"
		"tag %s\n"
		"tagger %s\n"
		"\n",
		sha1_to_hex(sha1), commit_type, t->name, tagger);
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

static void parse_checkpoint(void)
{
	if (object_count) {
		cycle_packfile();
		dump_branches();
		dump_tags();
		dump_marks();
	}
	skip_optional_lf();
}

static void parse_progress(void)
{
	fwrite(command_buf.buf, 1, command_buf.len, stdout);
	fputc('\n', stdout);
	fflush(stdout);
	skip_optional_lf();
}

static void import_marks(const char *input_file)
{
	char line[512];
	FILE *f = fopen(input_file, "r");
	if (!f)
		die("cannot read %s: %s", input_file, strerror(errno));
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
			e->offset = 1; /* just not zero! */
		}
		insert_mark(mark, e);
	}
	fclose(f);
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
	return git_default_config(k, v, cb);
}

static const char fast_import_usage[] =
"git fast-import [--date-format=f] [--max-pack-size=n] [--depth=n] [--active-branches=n] [--export-marks=marks.file]";

int main(int argc, const char **argv)
{
	unsigned int i, show_stats = 1;

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

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (*a != '-' || !strcmp(a, "--"))
			break;
		else if (!prefixcmp(a, "--date-format=")) {
			const char *fmt = a + 14;
			if (!strcmp(fmt, "raw"))
				whenspec = WHENSPEC_RAW;
			else if (!strcmp(fmt, "rfc2822"))
				whenspec = WHENSPEC_RFC2822;
			else if (!strcmp(fmt, "now"))
				whenspec = WHENSPEC_NOW;
			else
				die("unknown --date-format argument %s", fmt);
		}
		else if (!prefixcmp(a, "--max-pack-size="))
			max_packsize = strtoumax(a + 16, NULL, 0) * 1024 * 1024;
		else if (!prefixcmp(a, "--depth=")) {
			max_depth = strtoul(a + 8, NULL, 0);
			if (max_depth > MAX_DEPTH)
				die("--depth cannot exceed %u", MAX_DEPTH);
		}
		else if (!prefixcmp(a, "--active-branches="))
			max_active_branches = strtoul(a + 18, NULL, 0);
		else if (!prefixcmp(a, "--import-marks="))
			import_marks(a + 15);
		else if (!prefixcmp(a, "--export-marks="))
			mark_file = a + 15;
		else if (!prefixcmp(a, "--export-pack-edges=")) {
			if (pack_edges)
				fclose(pack_edges);
			pack_edges = fopen(a + 20, "a");
			if (!pack_edges)
				die("Cannot open %s: %s", a + 20, strerror(errno));
		} else if (!strcmp(a, "--force"))
			force_update = 1;
		else if (!strcmp(a, "--quiet"))
			show_stats = 0;
		else if (!strcmp(a, "--stats"))
			show_stats = 1;
		else
			die("unknown option %s", a);
	}
	if (i != argc)
		usage(fast_import_usage);

	rc_free = pool_alloc(cmd_save * sizeof(*rc_free));
	for (i = 0; i < (cmd_save - 1); i++)
		rc_free[i].next = &rc_free[i + 1];
	rc_free[cmd_save - 1].next = NULL;

	prepare_packed_git();
	start_packfile();
	set_die_routine(die_nicely);
	while (read_next_command() != EOF) {
		if (!strcmp("blob", command_buf.buf))
			parse_new_blob();
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
		else
			die("Unsupported command: %s", command_buf.buf);
	}
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
