/*
Format of STDIN stream:

  stream ::= cmd*;

  cmd ::= new_blob
        | new_commit
        | new_tag
        | reset_branch
        ;

  new_blob ::= 'blob' lf
	mark?
    file_content;
  file_content ::= data;

  new_commit ::= 'commit' sp ref_str lf
    mark?
    ('author' sp name '<' email '>' ts tz lf)?
    'committer' sp name '<' email '>' ts tz lf
    commit_msg
    ('from' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf)?
    ('merge' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf)*
    file_change*
    lf;
  commit_msg ::= data;

  file_change ::= file_del | file_obm | file_inm;
  file_del ::= 'D' sp path_str lf;
  file_obm ::= 'M' sp mode sp (hexsha1 | idnum) sp path_str lf;
  file_inm ::= 'M' sp mode sp 'inline' sp path_str lf
    data;

  new_tag ::= 'tag' sp tag_str lf
    'from' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf
	'tagger' sp name '<' email '>' ts tz lf
    tag_msg;
  tag_msg ::= data;

  reset_branch ::= 'reset' sp ref_str lf
    ('from' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf)?
    lf;

  checkpoint ::= 'checkpoint' lf
    lf;

     # note: the first idnum in a stream should be 1 and subsequent
     # idnums should not have gaps between values as this will cause
     # the stream parser to reserve space for the gapped values.  An
	 # idnum can be updated in the future to a new object by issuing
     # a new mark directive with the old idnum.
	 #
  mark ::= 'mark' sp idnum lf;
  data ::= (delimited_data | exact_data)
    lf;

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
  ref_str     ::= ref     | '"' quoted(ref)     '"' ;
  sha1exp_str ::= sha1exp | '"' quoted(sha1exp) '"' ;
  tag_str     ::= tag     | '"' quoted(tag)     '"' ;
  path_str    ::= path    | '"' quoted(path)    '"' ;
  mode        ::= '100644' | '644'
                | '100755' | '755'
                | '140000'
                ;

  declen ::= # unsigned 32 bit value, ascii base10 notation;
  bigint ::= # unsigned integer value, ascii base10 notation;
  binary_data ::= # file content, not interpreted;

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
*/

#include "builtin.h"
#include "cache.h"
#include "object.h"
#include "blob.h"
#include "tree.h"
#include "delta.h"
#include "pack.h"
#include "refs.h"
#include "csum-file.h"
#include "strbuf.h"
#include "quote.h"

#define PACK_ID_BITS 16
#define MAX_PACK_ID ((1<<PACK_ID_BITS)-1)

struct object_entry
{
	struct object_entry *next;
	unsigned long offset;
	unsigned type : TYPE_BITS;
	unsigned pack_id : PACK_ID_BITS;
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
	void *data;
	unsigned long len;
	unsigned long offset;
	unsigned int depth;
	unsigned no_free:1;
};

struct mem_pool
{
	struct mem_pool *next_pool;
	char *next_free;
	char *end;
	char space[FLEX_ARRAY]; /* more */
};

struct atom_str
{
	struct atom_str *next_atom;
	unsigned int str_len;
	char str_dat[FLEX_ARRAY]; /* more */
};

struct tree_content;
struct tree_entry
{
	struct tree_content *tree;
	struct atom_str* name;
	struct tree_entry_ms
	{
		unsigned int mode;
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
	unsigned int pack_id;
	unsigned char sha1[20];
};

struct tag
{
	struct tag *next_tag;
	const char *name;
	unsigned int pack_id;
	unsigned char sha1[20];
};

struct dbuf
{
	void *buffer;
	size_t capacity;
};

struct hash_list
{
	struct hash_list *next;
	unsigned char sha1[20];
};

/* Configured limits on output */
static unsigned long max_depth = 10;
static unsigned long max_packsize = (1LL << 32) - 1;

/* Stats and misc. counters */
static uintmax_t alloc_count;
static uintmax_t marks_set_count;
static uintmax_t object_count_by_type[1 << TYPE_BITS];
static uintmax_t duplicate_count_by_type[1 << TYPE_BITS];
static uintmax_t delta_count_by_type[1 << TYPE_BITS];
static unsigned long object_count;
static unsigned long branch_count;
static unsigned long branch_load_count;

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
static struct last_object last_blob;

/* Tree management */
static unsigned int tree_entry_alloc = 1000;
static void *avail_tree_entry;
static unsigned int avail_tree_table_sz = 100;
static struct avail_tree_content **avail_tree_table;
static struct dbuf old_tree;
static struct dbuf new_tree;

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
static struct strbuf command_buf;
static uintmax_t next_mark;
static struct dbuf new_data;
static FILE* branch_log;


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

static struct object_entry* new_object(unsigned char *sha1)
{
	struct object_entry *e;

	if (blocks->next_free == blocks->end)
		alloc_objects(object_entry_alloc);

	e = blocks->next_free++;
	hashcpy(e->sha1, sha1);
	return e;
}

static struct object_entry* find_object(unsigned char *sha1)
{
	unsigned int h = sha1[0] << 8 | sha1[1];
	struct object_entry *e;
	for (e = object_table[h]; e; e = e->next)
		if (!hashcmp(sha1, e->sha1))
			return e;
	return NULL;
}

static struct object_entry* insert_object(unsigned char *sha1)
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

static void* pool_alloc(size_t len)
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
		p->next_free = p->space;
		p->end = p->next_free + mem_pool_alloc;
		mem_pool = p;
	}

	r = p->next_free;
	/* round out to a pointer alignment */
	if (len & (sizeof(void*) - 1))
		len += sizeof(void*) - (len & (sizeof(void*) - 1));
	p->next_free += len;
	return r;
}

static void* pool_calloc(size_t count, size_t size)
{
	size_t len = count * size;
	void *r = pool_alloc(len);
	memset(r, 0, len);
	return r;
}

static char* pool_strdup(const char *s)
{
	char *r = pool_alloc(strlen(s) + 1);
	strcpy(r, s);
	return r;
}

static void size_dbuf(struct dbuf *b, size_t maxlen)
{
	if (b->buffer) {
		if (b->capacity >= maxlen)
			return;
		free(b->buffer);
	}
	b->capacity = ((maxlen / 1024) + 1) * 1024;
	b->buffer = xmalloc(b->capacity);
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

static struct object_entry* find_mark(uintmax_t idnum)
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
		die("mark :%ju not declared", orig_idnum);
	return oe;
}

static struct atom_str* to_atom(const char *s, size_t len)
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

static struct branch* lookup_branch(const char *name)
{
	unsigned int hc = hc_str(name, strlen(name)) % branch_table_sz;
	struct branch *b;

	for (b = branch_table[hc]; b; b = b->table_next_branch)
		if (!strcmp(name, b->name))
			return b;
	return NULL;
}

static struct branch* new_branch(const char *name)
{
	unsigned int hc = hc_str(name, strlen(name)) % branch_table_sz;
	struct branch* b = lookup_branch(name);

	if (b)
		die("Invalid attempt to create duplicate branch: %s", name);
	if (check_ref_format(name))
		die("Branch name doesn't conform to GIT standards: %s", name);

	b = pool_calloc(1, sizeof(struct branch));
	b->name = pool_strdup(name);
	b->table_next_branch = branch_table[hc];
	b->branch_tree.versions[0].mode = S_IFDIR;
	b->branch_tree.versions[1].mode = S_IFDIR;
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

static struct tree_content* new_tree_content(unsigned int cnt)
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

static struct tree_content* grow_tree_content(
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

static struct tree_entry* new_tree_entry(void)
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

static void start_packfile(void)
{
	static char tmpfile[PATH_MAX];
	struct packed_git *p;
	struct pack_header hdr;
	int pack_fd;

	snprintf(tmpfile, sizeof(tmpfile),
		"%s/pack_XXXXXX", get_object_directory());
	pack_fd = mkstemp(tmpfile);
	if (pack_fd < 0)
		die("Can't create %s: %s", tmpfile, strerror(errno));
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

static void fixup_header_footer(void)
{
	static const int buf_sz = 128 * 1024;
	int pack_fd = pack_data->pack_fd;
	SHA_CTX c;
	struct pack_header hdr;
	char *buf;

	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die("Failed seeking to start: %s", strerror(errno));
	if (read_in_full(pack_fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		die("Unable to reread header of %s", pack_data->pack_name);
	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die("Failed seeking to start: %s", strerror(errno));
	hdr.hdr_entries = htonl(object_count);
	write_or_die(pack_fd, &hdr, sizeof(hdr));

	SHA1_Init(&c);
	SHA1_Update(&c, &hdr, sizeof(hdr));

	buf = xmalloc(buf_sz);
	for (;;) {
		size_t n = xread(pack_fd, buf, buf_sz);
		if (!n)
			break;
		if (n < 0)
			die("Failed to checksum %s", pack_data->pack_name);
		SHA1_Update(&c, buf, n);
	}
	free(buf);

	SHA1_Final(pack_data->sha1, &c);
	write_or_die(pack_fd, pack_data->sha1, sizeof(pack_data->sha1));
	close(pack_fd);
}

static int oecmp (const void *a_, const void *b_)
{
	struct object_entry *a = *((struct object_entry**)a_);
	struct object_entry *b = *((struct object_entry**)b_);
	return hashcmp(a->sha1, b->sha1);
}

static char* create_index(void)
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
		"%s/index_XXXXXX", get_object_directory());
	idx_fd = mkstemp(tmpfile);
	if (idx_fd < 0)
		die("Can't create %s: %s", tmpfile, strerror(errno));
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
	sha1close(f, NULL, 1);
	free(idx);
	SHA1_Final(pack_data->sha1, &ctx);
	return tmpfile;
}

static char* keep_pack(char *curr_index_name)
{
	static char name[PATH_MAX];
	static char *keep_msg = "fast-import";
	int keep_fd;

	chmod(pack_data->pack_name, 0444);
	chmod(curr_index_name, 0444);

	snprintf(name, sizeof(name), "%s/pack/pack-%s.keep",
		 get_object_directory(), sha1_to_hex(pack_data->sha1));
	keep_fd = open(name, O_RDWR|O_CREAT|O_EXCL, 0600);
	if (keep_fd < 0)
		die("cannot create keep file");
	write(keep_fd, keep_msg, strlen(keep_msg));
	close(keep_fd);

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

		fixup_header_footer();
		idx_name = keep_pack(create_index());

		/* Register the packfile with core git's machinary. */
		new_p = add_packed_git(idx_name, strlen(idx_name), 1);
		if (!new_p)
			die("core git rejected index %s", idx_name);
		new_p->windows = old_p->windows;
		all_packs[pack_id] = new_p;
		install_packed_git(new_p);

		/* Print the boundary */
		fprintf(stdout, "%s:", new_p->pack_name);
		for (i = 0; i < branch_table_sz; i++) {
			for (b = branch_table[i]; b; b = b->table_next_branch) {
				if (b->pack_id == pack_id)
					fprintf(stdout, " %s", sha1_to_hex(b->sha1));
			}
		}
		for (t = first_tag; t; t = t->next_tag) {
			if (t->pack_id == pack_id)
				fprintf(stdout, " %s", sha1_to_hex(t->sha1));
		}
		fputc('\n', stdout);

		pack_id++;
	}
	else
		unlink(old_p->pack_name);
	free(old_p);

	/* We can't carry a delta across packfiles. */
	free(last_blob.data);
	last_blob.data = NULL;
	last_blob.len = 0;
	last_blob.offset = 0;
	last_blob.depth = 0;
}

static void checkpoint(void)
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
	void *dat,
	size_t datlen,
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

	hdrlen = sprintf((char*)hdr,"%s %lu",type_names[type],datlen) + 1;
	SHA1_Init(&c);
	SHA1_Update(&c, hdr, hdrlen);
	SHA1_Update(&c, dat, datlen);
	SHA1_Final(sha1, &c);
	if (sha1out)
		hashcpy(sha1out, sha1);

	e = insert_object(sha1);
	if (mark)
		insert_mark(mark, e);
	if (e->offset) {
		duplicate_count_by_type[type]++;
		return 1;
	}

	if (last && last->data && last->depth < max_depth) {
		delta = diff_delta(last->data, last->len,
			dat, datlen,
			&deltalen, 0);
		if (delta && deltalen >= datlen) {
			free(delta);
			delta = NULL;
		}
	} else
		delta = NULL;

	memset(&s, 0, sizeof(s));
	deflateInit(&s, zlib_compression_level);
	if (delta) {
		s.next_in = delta;
		s.avail_in = deltalen;
	} else {
		s.next_in = dat;
		s.avail_in = datlen;
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
		checkpoint();

		/* We cannot carry a delta into the new pack. */
		if (delta) {
			free(delta);
			delta = NULL;

			memset(&s, 0, sizeof(s));
			deflateInit(&s, zlib_compression_level);
			s.next_in = dat;
			s.avail_in = datlen;
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
		last->depth++;

		hdrlen = encode_header(OBJ_OFS_DELTA, deltalen, hdr);
		write_or_die(pack_data->pack_fd, hdr, hdrlen);
		pack_size += hdrlen;

		hdr[pos] = ofs & 127;
		while (ofs >>= 7)
			hdr[--pos] = 128 | (--ofs & 127);
		write_or_die(pack_data->pack_fd, hdr + pos, sizeof(hdr) - pos);
		pack_size += sizeof(hdr) - pos;
	} else {
		if (last)
			last->depth = 0;
		hdrlen = encode_header(type, datlen, hdr);
		write_or_die(pack_data->pack_fd, hdr, hdrlen);
		pack_size += hdrlen;
	}

	write_or_die(pack_data->pack_fd, out, s.total_out);
	pack_size += s.total_out;

	free(out);
	if (delta)
		free(delta);
	if (last) {
		if (last->data && !last->no_free)
			free(last->data);
		last->data = dat;
		last->offset = e->offset;
		last->len = datlen;
	}
	return 0;
}

static void *gfi_unpack_entry(
	struct object_entry *oe,
	unsigned long *sizep)
{
	static char type[20];
	struct packed_git *p = all_packs[oe->pack_id];
	if (p == pack_data)
		p->pack_size = pack_size + 20;
	return unpack_entry(p, oe->offset, type, sizep);
}

static const char *get_mode(const char *str, unsigned int *modep)
{
	unsigned char c;
	unsigned int mode = 0;

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
	if (myoe) {
		if (myoe->type != OBJ_TREE)
			die("Not a tree: %s", sha1_to_hex(sha1));
		t->delta_depth = 0;
		buf = gfi_unpack_entry(myoe, &size);
	} else {
		char type[20];
		buf = read_sha1_file(sha1, type, &size);
		if (!buf || strcmp(type, tree_type))
			die("Can't load tree %s", sha1_to_hex(sha1));
	}

	c = buf;
	while (c != (buf + size)) {
		struct tree_entry *e = new_tree_entry();

		if (t->entry_count == t->entry_capacity)
			root->tree = t = grow_tree_content(t, 8);
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

static void mktree(struct tree_content *t,
	int v,
	unsigned long *szp,
	struct dbuf *b)
{
	size_t maxlen = 0;
	unsigned int i;
	char *c;

	if (!v)
		qsort(t->entries,t->entry_count,sizeof(t->entries[0]),tecmp0);
	else
		qsort(t->entries,t->entry_count,sizeof(t->entries[0]),tecmp1);

	for (i = 0; i < t->entry_count; i++) {
		if (t->entries[i]->versions[v].mode)
			maxlen += t->entries[i]->name->str_len + 34;
	}

	size_dbuf(b, maxlen);
	c = b->buffer;
	for (i = 0; i < t->entry_count; i++) {
		struct tree_entry *e = t->entries[i];
		if (!e->versions[v].mode)
			continue;
		c += sprintf(c, "%o", e->versions[v].mode);
		*c++ = ' ';
		strcpy(c, e->name->str_dat);
		c += e->name->str_len + 1;
		hashcpy((unsigned char*)c, e->versions[v].sha1);
		c += 20;
	}
	*szp = c - (char*)b->buffer;
}

static void store_tree(struct tree_entry *root)
{
	struct tree_content *t = root->tree;
	unsigned int i, j, del;
	unsigned long new_len;
	struct last_object lo;
	struct object_entry *le;

	if (!is_null_sha1(root->versions[1].sha1))
		return;

	for (i = 0; i < t->entry_count; i++) {
		if (t->entries[i]->tree)
			store_tree(t->entries[i]);
	}

	le = find_object(root->versions[0].sha1);
	if (!S_ISDIR(root->versions[0].mode)
		|| !le
		|| le->pack_id != pack_id) {
		lo.data = NULL;
		lo.depth = 0;
	} else {
		mktree(t, 0, &lo.len, &old_tree);
		lo.data = old_tree.buffer;
		lo.offset = le->offset;
		lo.depth = t->delta_depth;
		lo.no_free = 1;
	}

	mktree(t, 1, &new_len, &new_tree);
	store_object(OBJ_TREE, new_tree.buffer, new_len,
		&lo, root->versions[1].sha1, 0);

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
	const unsigned int mode)
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
				if (e->versions[1].mode == mode
						&& !hashcmp(e->versions[1].sha1, sha1))
					return 0;
				e->versions[1].mode = mode;
				hashcpy(e->versions[1].sha1, sha1);
				if (e->tree) {
					release_tree_content_recursive(e->tree);
					e->tree = NULL;
				}
				hashclr(root->versions[1].sha1);
				return 1;
			}
			if (!S_ISDIR(e->versions[1].mode)) {
				e->tree = new_tree_content(8);
				e->versions[1].mode = S_IFDIR;
			}
			if (!e->tree)
				load_tree(e);
			if (tree_content_set(e, slash1 + 1, sha1, mode)) {
				hashclr(root->versions[1].sha1);
				return 1;
			}
			return 0;
		}
	}

	if (t->entry_count == t->entry_capacity)
		root->tree = t = grow_tree_content(t, 8);
	e = new_tree_entry();
	e->name = to_atom(p, n);
	e->versions[0].mode = 0;
	hashclr(e->versions[0].sha1);
	t->entries[t->entry_count++] = e;
	if (slash1) {
		e->tree = new_tree_content(8);
		e->versions[1].mode = S_IFDIR;
		tree_content_set(e, slash1 + 1, sha1, mode);
	} else {
		e->tree = NULL;
		e->versions[1].mode = mode;
		hashcpy(e->versions[1].sha1, sha1);
	}
	hashclr(root->versions[1].sha1);
	return 1;
}

static int tree_content_remove(struct tree_entry *root, const char *p)
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
			if (tree_content_remove(e, slash1 + 1)) {
				for (n = 0; n < e->tree->entry_count; n++) {
					if (e->tree->entries[n]->versions[1].mode) {
						hashclr(root->versions[1].sha1);
						return 1;
					}
				}
				goto del_entry;
			}
			return 0;
		}
	}
	return 0;

del_entry:
	if (e->tree) {
		release_tree_content_recursive(e->tree);
		e->tree = NULL;
	}
	e->versions[1].mode = 0;
	hashclr(e->versions[1].sha1);
	hashclr(root->versions[1].sha1);
	return 1;
}

static void dump_branches(void)
{
	static const char *msg = "fast-import";
	unsigned int i;
	struct branch *b;
	struct ref_lock *lock;

	for (i = 0; i < branch_table_sz; i++) {
		for (b = branch_table[i]; b; b = b->table_next_branch) {
			lock = lock_any_ref_for_update(b->name, NULL);
			if (!lock || write_ref_sha1(lock, b->sha1, msg) < 0)
				die("Can't write %s", b->name);
		}
	}
}

static void dump_tags(void)
{
	static const char *msg = "fast-import";
	struct tag *t;
	struct ref_lock *lock;
	char path[PATH_MAX];

	for (t = first_tag; t; t = t->next_tag) {
		sprintf(path, "refs/tags/%s", t->name);
		lock = lock_any_ref_for_update(path, NULL);
		if (!lock || write_ref_sha1(lock, t->sha1, msg) < 0)
			die("Can't write %s", path);
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
				fprintf(f, ":%ju %s\n", base + k,
					sha1_to_hex(m->data.marked[k]->sha1));
		}
	}
}

static void dump_marks(void)
{
	if (mark_file)
	{
		FILE *f = fopen(mark_file, "w");
		dump_marks_helper(f, 0, marks);
		fclose(f);
	}
}

static void read_next_command(void)
{
	read_line(&command_buf, stdin, '\n');
}

static void cmd_mark(void)
{
	if (!strncmp("mark :", command_buf.buf, 6)) {
		next_mark = strtoumax(command_buf.buf + 6, NULL, 10);
		read_next_command();
	}
	else
		next_mark = 0;
}

static void* cmd_data (size_t *size)
{
	size_t length;
	char *buffer;

	if (strncmp("data ", command_buf.buf, 5))
		die("Expected 'data n' command, found: %s", command_buf.buf);

	if (!strncmp("<<", command_buf.buf + 5, 2)) {
		char *term = xstrdup(command_buf.buf + 5 + 2);
		size_t sz = 8192, term_len = command_buf.len - 5 - 2;
		length = 0;
		buffer = xmalloc(sz);
		for (;;) {
			read_next_command();
			if (command_buf.eof)
				die("EOF in data (terminator '%s' not found)", term);
			if (term_len == command_buf.len
				&& !strcmp(term, command_buf.buf))
				break;
			if (sz < (length + command_buf.len)) {
				sz = sz * 3 / 2 + 16;
				if (sz < (length + command_buf.len))
					sz = length + command_buf.len;
				buffer = xrealloc(buffer, sz);
			}
			memcpy(buffer + length,
				command_buf.buf,
				command_buf.len - 1);
			length += command_buf.len - 1;
			buffer[length++] = '\n';
		}
		free(term);
	}
	else {
		size_t n = 0;
		length = strtoul(command_buf.buf + 5, NULL, 10);
		buffer = xmalloc(length);
		while (n < length) {
			size_t s = fread(buffer + n, 1, length - n, stdin);
			if (!s && feof(stdin))
				die("EOF in data (%lu bytes remaining)", length - n);
			n += s;
		}
	}

	if (fgetc(stdin) != '\n')
		die("An lf did not trail the binary data as expected.");

	*size = length;
	return buffer;
}

static void cmd_new_blob(void)
{
	size_t l;
	void *d;

	read_next_command();
	cmd_mark();
	d = cmd_data(&l);

	if (store_object(OBJ_BLOB, d, l, &last_blob, NULL, next_mark))
		free(d);
}

static void unload_one_branch(void)
{
	while (cur_active_branches
		&& cur_active_branches >= max_active_branches) {
		unsigned long min_commit = ULONG_MAX;
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
	b->active_next_branch = active_branches;
	active_branches = b;
	cur_active_branches++;
	branch_load_count++;
}

static void file_change_m(struct branch *b)
{
	const char *p = command_buf.buf + 2;
	char *p_uq;
	const char *endp;
	struct object_entry *oe;
	unsigned char sha1[20];
	unsigned int mode, inline_data = 0;
	char type[20];

	p = get_mode(p, &mode);
	if (!p)
		die("Corrupt mode: %s", command_buf.buf);
	switch (mode) {
	case S_IFREG | 0644:
	case S_IFREG | 0755:
	case S_IFLNK:
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
	} else if (!strncmp("inline", p, 6)) {
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

	p_uq = unquote_c_style(p, &endp);
	if (p_uq) {
		if (*endp)
			die("Garbage after path in: %s", command_buf.buf);
		p = p_uq;
	}

	if (inline_data) {
		size_t l;
		void *d;
		if (!p_uq)
			p = p_uq = xstrdup(p);
		read_next_command();
		d = cmd_data(&l);
		if (store_object(OBJ_BLOB, d, l, &last_blob, sha1, 0))
			free(d);
	} else if (oe) {
		if (oe->type != OBJ_BLOB)
			die("Not a blob (actually a %s): %s",
				command_buf.buf, type_names[oe->type]);
	} else {
		if (sha1_object_info(sha1, type, NULL))
			die("Blob not found: %s", command_buf.buf);
		if (strcmp(blob_type, type))
			die("Not a blob (actually a %s): %s",
				command_buf.buf, type);
	}

	tree_content_set(&b->branch_tree, p, sha1, S_IFREG | mode);

	if (p_uq)
		free(p_uq);
}

static void file_change_d(struct branch *b)
{
	const char *p = command_buf.buf + 2;
	char *p_uq;
	const char *endp;

	p_uq = unquote_c_style(p, &endp);
	if (p_uq) {
		if (*endp)
			die("Garbage after path in: %s", command_buf.buf);
		p = p_uq;
	}
	tree_content_remove(&b->branch_tree, p);
	if (p_uq)
		free(p_uq);
}

static void cmd_from(struct branch *b)
{
	const char *from, *endp;
	char *str_uq;
	struct branch *s;

	if (strncmp("from ", command_buf.buf, 5))
		return;

	if (b->last_commit)
		die("Can't reinitailize branch %s", b->name);

	from = strchr(command_buf.buf, ' ') + 1;
	str_uq = unquote_c_style(from, &endp);
	if (str_uq) {
		if (*endp)
			die("Garbage after string in: %s", command_buf.buf);
		from = str_uq;
	}

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
		unsigned long size;
		char *buf;
		if (oe->type != OBJ_COMMIT)
			die("Mark :%ju not a commit", idnum);
		hashcpy(b->sha1, oe->sha1);
		buf = gfi_unpack_entry(oe, &size);
		if (!buf || size < 46)
			die("Not a valid commit: %s", from);
		if (memcmp("tree ", buf, 5)
			|| get_sha1_hex(buf + 5, b->branch_tree.versions[1].sha1))
			die("The commit %s is corrupt", sha1_to_hex(b->sha1));
		free(buf);
		hashcpy(b->branch_tree.versions[0].sha1,
			b->branch_tree.versions[1].sha1);
	} else if (!get_sha1(from, b->sha1)) {
		if (is_null_sha1(b->sha1)) {
			hashclr(b->branch_tree.versions[0].sha1);
			hashclr(b->branch_tree.versions[1].sha1);
		} else {
			unsigned long size;
			char *buf;

			buf = read_object_with_reference(b->sha1,
				type_names[OBJ_COMMIT], &size, b->sha1);
			if (!buf || size < 46)
				die("Not a valid commit: %s", from);
			if (memcmp("tree ", buf, 5)
				|| get_sha1_hex(buf + 5, b->branch_tree.versions[1].sha1))
				die("The commit %s is corrupt", sha1_to_hex(b->sha1));
			free(buf);
			hashcpy(b->branch_tree.versions[0].sha1,
				b->branch_tree.versions[1].sha1);
		}
	} else
		die("Invalid ref name or SHA1 expression: %s", from);

	read_next_command();
}

static struct hash_list* cmd_merge(unsigned int *count)
{
	struct hash_list *list = NULL, *n, *e;
	const char *from, *endp;
	char *str_uq;
	struct branch *s;

	*count = 0;
	while (!strncmp("merge ", command_buf.buf, 6)) {
		from = strchr(command_buf.buf, ' ') + 1;
		str_uq = unquote_c_style(from, &endp);
		if (str_uq) {
			if (*endp)
				die("Garbage after string in: %s", command_buf.buf);
			from = str_uq;
		}

		n = xmalloc(sizeof(*n));
		s = lookup_branch(from);
		if (s)
			hashcpy(n->sha1, s->sha1);
		else if (*from == ':') {
			uintmax_t idnum = strtoumax(from + 1, NULL, 10);
			struct object_entry *oe = find_mark(idnum);
			if (oe->type != OBJ_COMMIT)
				die("Mark :%ju not a commit", idnum);
			hashcpy(n->sha1, oe->sha1);
		} else if (get_sha1(from, n->sha1))
			die("Invalid ref name or SHA1 expression: %s", from);

		n->next = NULL;
		if (list)
			e->next = n;
		else
			list = n;
		e = n;
		*count++;
		read_next_command();
	}
	return list;
}

static void cmd_new_commit(void)
{
	struct branch *b;
	void *msg;
	size_t msglen;
	char *str_uq;
	const char *endp;
	char *sp;
	char *author = NULL;
	char *committer = NULL;
	struct hash_list *merge_list = NULL;
	unsigned int merge_count;

	/* Obtain the branch name from the rest of our command */
	sp = strchr(command_buf.buf, ' ') + 1;
	str_uq = unquote_c_style(sp, &endp);
	if (str_uq) {
		if (*endp)
			die("Garbage after ref in: %s", command_buf.buf);
		sp = str_uq;
	}
	b = lookup_branch(sp);
	if (!b)
		b = new_branch(sp);
	if (str_uq)
		free(str_uq);

	read_next_command();
	cmd_mark();
	if (!strncmp("author ", command_buf.buf, 7)) {
		author = strdup(command_buf.buf);
		read_next_command();
	}
	if (!strncmp("committer ", command_buf.buf, 10)) {
		committer = strdup(command_buf.buf);
		read_next_command();
	}
	if (!committer)
		die("Expected committer but didn't get one");
	msg = cmd_data(&msglen);
	read_next_command();
	cmd_from(b);
	merge_list = cmd_merge(&merge_count);

	/* ensure the branch is active/loaded */
	if (!b->branch_tree.tree || !max_active_branches) {
		unload_one_branch();
		load_branch(b);
	}

	/* file_change* */
	for (;;) {
		if (1 == command_buf.len)
			break;
		else if (!strncmp("M ", command_buf.buf, 2))
			file_change_m(b);
		else if (!strncmp("D ", command_buf.buf, 2))
			file_change_d(b);
		else
			die("Unsupported file_change: %s", command_buf.buf);
		read_next_command();
	}

	/* build the tree and the commit */
	store_tree(&b->branch_tree);
	hashcpy(b->branch_tree.versions[0].sha1,
		b->branch_tree.versions[1].sha1);
	size_dbuf(&new_data, 97 + msglen
		+ merge_count * 49
		+ (author
			? strlen(author) + strlen(committer)
			: 2 * strlen(committer)));
	sp = new_data.buffer;
	sp += sprintf(sp, "tree %s\n",
		sha1_to_hex(b->branch_tree.versions[1].sha1));
	if (!is_null_sha1(b->sha1))
		sp += sprintf(sp, "parent %s\n", sha1_to_hex(b->sha1));
	while (merge_list) {
		struct hash_list *next = merge_list->next;
		sp += sprintf(sp, "parent %s\n", sha1_to_hex(merge_list->sha1));
		free(merge_list);
		merge_list = next;
	}
	if (author)
		sp += sprintf(sp, "%s\n", author);
	else
		sp += sprintf(sp, "author %s\n", committer + 10);
	sp += sprintf(sp, "%s\n\n", committer);
	memcpy(sp, msg, msglen);
	sp += msglen;
	if (author)
		free(author);
	free(committer);
	free(msg);

	if (!store_object(OBJ_COMMIT,
		new_data.buffer, sp - (char*)new_data.buffer,
		NULL, b->sha1, next_mark))
		b->pack_id = pack_id;
	b->last_commit = object_count_by_type[OBJ_COMMIT];

	if (branch_log) {
		int need_dq = quote_c_style(b->name, NULL, NULL, 0);
		fprintf(branch_log, "commit ");
		if (need_dq) {
			fputc('"', branch_log);
			quote_c_style(b->name, NULL, branch_log, 0);
			fputc('"', branch_log);
		} else
			fprintf(branch_log, "%s", b->name);
		fprintf(branch_log," :%ju %s\n",next_mark,sha1_to_hex(b->sha1));
	}
}

static void cmd_new_tag(void)
{
	char *str_uq;
	const char *endp;
	char *sp;
	const char *from;
	char *tagger;
	struct branch *s;
	void *msg;
	size_t msglen;
	struct tag *t;
	uintmax_t from_mark = 0;
	unsigned char sha1[20];

	/* Obtain the new tag name from the rest of our command */
	sp = strchr(command_buf.buf, ' ') + 1;
	str_uq = unquote_c_style(sp, &endp);
	if (str_uq) {
		if (*endp)
			die("Garbage after tag name in: %s", command_buf.buf);
		sp = str_uq;
	}
	t = pool_alloc(sizeof(struct tag));
	t->next_tag = NULL;
	t->name = pool_strdup(sp);
	if (last_tag)
		last_tag->next_tag = t;
	else
		first_tag = t;
	last_tag = t;
	if (str_uq)
		free(str_uq);
	read_next_command();

	/* from ... */
	if (strncmp("from ", command_buf.buf, 5))
		die("Expected from command, got %s", command_buf.buf);

	from = strchr(command_buf.buf, ' ') + 1;
	str_uq = unquote_c_style(from, &endp);
	if (str_uq) {
		if (*endp)
			die("Garbage after string in: %s", command_buf.buf);
		from = str_uq;
	}

	s = lookup_branch(from);
	if (s) {
		hashcpy(sha1, s->sha1);
	} else if (*from == ':') {
		from_mark = strtoumax(from + 1, NULL, 10);
		struct object_entry *oe = find_mark(from_mark);
		if (oe->type != OBJ_COMMIT)
			die("Mark :%ju not a commit", from_mark);
		hashcpy(sha1, oe->sha1);
	} else if (!get_sha1(from, sha1)) {
		unsigned long size;
		char *buf;

		buf = read_object_with_reference(sha1,
			type_names[OBJ_COMMIT], &size, sha1);
		if (!buf || size < 46)
			die("Not a valid commit: %s", from);
		free(buf);
	} else
		die("Invalid ref name or SHA1 expression: %s", from);

	if (str_uq)
		free(str_uq);
	read_next_command();

	/* tagger ... */
	if (strncmp("tagger ", command_buf.buf, 7))
		die("Expected tagger command, got %s", command_buf.buf);
	tagger = strdup(command_buf.buf);

	/* tag payload/message */
	read_next_command();
	msg = cmd_data(&msglen);

	/* build the tag object */
	size_dbuf(&new_data, 67+strlen(t->name)+strlen(tagger)+msglen);
	sp = new_data.buffer;
	sp += sprintf(sp, "object %s\n", sha1_to_hex(sha1));
	sp += sprintf(sp, "type %s\n", type_names[OBJ_COMMIT]);
	sp += sprintf(sp, "tag %s\n", t->name);
	sp += sprintf(sp, "%s\n\n", tagger);
	memcpy(sp, msg, msglen);
	sp += msglen;
	free(tagger);
	free(msg);

	if (store_object(OBJ_TAG, new_data.buffer,
		sp - (char*)new_data.buffer,
		NULL, t->sha1, 0))
		t->pack_id = MAX_PACK_ID;
	else
		t->pack_id = pack_id;

	if (branch_log) {
		int need_dq = quote_c_style(t->name, NULL, NULL, 0);
		fprintf(branch_log, "tag ");
		if (need_dq) {
			fputc('"', branch_log);
			quote_c_style(t->name, NULL, branch_log, 0);
			fputc('"', branch_log);
		} else
			fprintf(branch_log, "%s", t->name);
		fprintf(branch_log," :%ju %s\n",from_mark,sha1_to_hex(t->sha1));
	}
}

static void cmd_reset_branch(void)
{
	struct branch *b;
	char *str_uq;
	const char *endp;
	char *sp;

	/* Obtain the branch name from the rest of our command */
	sp = strchr(command_buf.buf, ' ') + 1;
	str_uq = unquote_c_style(sp, &endp);
	if (str_uq) {
		if (*endp)
			die("Garbage after ref in: %s", command_buf.buf);
		sp = str_uq;
	}
	b = lookup_branch(sp);
	if (b) {
		b->last_commit = 0;
		if (b->branch_tree.tree) {
			release_tree_content_recursive(b->branch_tree.tree);
			b->branch_tree.tree = NULL;
		}
	}
	else
		b = new_branch(sp);
	if (str_uq)
		free(str_uq);
	read_next_command();
	cmd_from(b);
}

static void cmd_checkpoint(void)
{
	if (object_count)
		checkpoint();
	read_next_command();
}

static const char fast_import_usage[] =
"git-fast-import [--depth=n] [--active-branches=n] [--export-marks=marks.file] [--branch-log=log]";

int main(int argc, const char **argv)
{
	int i;
	uintmax_t total_count, duplicate_count;

	git_config(git_default_config);

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (*a != '-' || !strcmp(a, "--"))
			break;
		else if (!strncmp(a, "--max-pack-size=", 16))
			max_packsize = strtoumax(a + 16, NULL, 0) * 1024 * 1024;
		else if (!strncmp(a, "--depth=", 8))
			max_depth = strtoul(a + 8, NULL, 0);
		else if (!strncmp(a, "--active-branches=", 18))
			max_active_branches = strtoul(a + 18, NULL, 0);
		else if (!strncmp(a, "--export-marks=", 15))
			mark_file = a + 15;
		else if (!strncmp(a, "--branch-log=", 13)) {
			branch_log = fopen(a + 13, "w");
			if (!branch_log)
				die("Can't create %s: %s", a + 13, strerror(errno));
		}
		else
			die("unknown option %s", a);
	}
	if (i != argc)
		usage(fast_import_usage);

	alloc_objects(object_entry_alloc);
	strbuf_init(&command_buf);

	atom_table = xcalloc(atom_table_sz, sizeof(struct atom_str*));
	branch_table = xcalloc(branch_table_sz, sizeof(struct branch*));
	avail_tree_table = xcalloc(avail_tree_table_sz, sizeof(struct avail_tree_content*));
	marks = pool_calloc(1, sizeof(struct mark_set));

	start_packfile();
	for (;;) {
		read_next_command();
		if (command_buf.eof)
			break;
		else if (!strcmp("blob", command_buf.buf))
			cmd_new_blob();
		else if (!strncmp("commit ", command_buf.buf, 7))
			cmd_new_commit();
		else if (!strncmp("tag ", command_buf.buf, 4))
			cmd_new_tag();
		else if (!strncmp("reset ", command_buf.buf, 6))
			cmd_reset_branch();
		else if (!strcmp("checkpoint", command_buf.buf))
			cmd_checkpoint();
		else
			die("Unsupported command: %s", command_buf.buf);
	}
	end_packfile();

	dump_branches();
	dump_tags();
	unkeep_all_packs();
	dump_marks();
	if (branch_log)
		fclose(branch_log);

	total_count = 0;
	for (i = 0; i < ARRAY_SIZE(object_count_by_type); i++)
		total_count += object_count_by_type[i];
	duplicate_count = 0;
	for (i = 0; i < ARRAY_SIZE(duplicate_count_by_type); i++)
		duplicate_count += duplicate_count_by_type[i];

	fprintf(stderr, "%s statistics:\n", argv[0]);
	fprintf(stderr, "---------------------------------------------------------------------\n");
	fprintf(stderr, "Alloc'd objects: %10ju\n", alloc_count);
	fprintf(stderr, "Total objects:   %10ju (%10ju duplicates                  )\n", total_count, duplicate_count);
	fprintf(stderr, "      blobs  :   %10ju (%10ju duplicates %10ju deltas)\n", object_count_by_type[OBJ_BLOB], duplicate_count_by_type[OBJ_BLOB], delta_count_by_type[OBJ_BLOB]);
	fprintf(stderr, "      trees  :   %10ju (%10ju duplicates %10ju deltas)\n", object_count_by_type[OBJ_TREE], duplicate_count_by_type[OBJ_TREE], delta_count_by_type[OBJ_TREE]);
	fprintf(stderr, "      commits:   %10ju (%10ju duplicates %10ju deltas)\n", object_count_by_type[OBJ_COMMIT], duplicate_count_by_type[OBJ_COMMIT], delta_count_by_type[OBJ_COMMIT]);
	fprintf(stderr, "      tags   :   %10ju (%10ju duplicates %10ju deltas)\n", object_count_by_type[OBJ_TAG], duplicate_count_by_type[OBJ_TAG], delta_count_by_type[OBJ_TAG]);
	fprintf(stderr, "Total branches:  %10lu (%10lu loads     )\n", branch_count, branch_load_count);
	fprintf(stderr, "      marks:     %10ju (%10ju unique    )\n", (((uintmax_t)1) << marks->shift) * 1024, marks_set_count);
	fprintf(stderr, "      atoms:     %10u\n", atom_cnt);
	fprintf(stderr, "Memory total:    %10ju KiB\n", (total_allocd + alloc_count*sizeof(struct object_entry))/1024);
	fprintf(stderr, "       pools:    %10lu KiB\n", total_allocd/1024);
	fprintf(stderr, "     objects:    %10ju KiB\n", (alloc_count*sizeof(struct object_entry))/1024);
	fprintf(stderr, "---------------------------------------------------------------------\n");
	pack_report();
	fprintf(stderr, "---------------------------------------------------------------------\n");
	fprintf(stderr, "\n");

	return 0;
}
