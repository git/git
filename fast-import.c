/*
Format of STDIN stream:

  stream ::= cmd*;

  cmd ::= new_blob
        | new_commit
        | new_branch
        | new_tag
        ;

  new_blob ::= 'blob' blob_data;

  new_commit ::= 'comt' ref_name author_committer_msg
    file_change*
    '0';

  new_branch ::= 'brch' dst_ref_name src_ref_name;
  dst_ref_name ::= ref_name;
  src_ref_name ::= ref_name | sha1_exp;

  new_tag ::= 'tagg' ref_name tag_name tagger_msg;

  file_change ::= 'M' path_name hexsha1
                | 'D' path_name
                ;

  author_committer_msg ::= len32
    'author' sp name '<' email '>' ts tz lf
    'committer' sp name '<' email '>' ts tz lf
    lf
    binary_data;

  tagger_msg ::= len32
    'tagger' sp name '<' email '>' ts tz lf
    lf
    binary_data;

  blob_data ::= len32 binary_data; # max len is 2^32-1
  path_name ::= len32 path;        # max len is PATH_MAX-1
  ref_name  ::= len32 ref;         # max len is PATH_MAX-1
  tag_name  ::= len32 tag;         # max len is PATH_MAX-1
  sha1_exp  ::= len32 sha1exp;     # max len is PATH_MAX-1

  len32 ::= # unsigned 32 bit value, native format;
  binary_data ::= # file content, not interpreted;
  sp ::= # ASCII space character;
  lf ::= # ASCII newline (LF) character;
  path ::= # GIT style file path, e.g. "a/b/c";
  ref ::= # GIT ref name, e.g. "refs/heads/MOZ_GECKO_EXPERIMENT";
  tag ::= # GIT tag name, e.g. "FIREFOX_1_5";
  sha1exp ::= # Any valid GIT SHA1 expression;
  hexsha1 ::= # SHA1 in hexadecimal format;
  name ::= # valid GIT author/committer name;
  email ::= # valid GIT author/committer email;
  ts ::= # time since the epoch in seconds, ascii decimal;
  tz ::= # GIT style timezone;
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

struct object_entry
{
	struct object_entry *next;
	unsigned long offset;
	unsigned char sha1[20];
};

struct object_entry_pool
{
	struct object_entry_pool *next_pool;
	struct object_entry *next_free;
	struct object_entry *end;
	struct object_entry entries[FLEX_ARRAY]; /* more */
};

struct last_object
{
	void *data;
	unsigned int len;
	unsigned int depth;
	unsigned char sha1[20];
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
	int str_len;
	char str_dat[FLEX_ARRAY]; /* more */
};

struct tree_content;
struct tree_entry
{
	struct tree_content *tree;
	struct atom_str* name;
	unsigned int mode;
	unsigned char sha1[20];
};

struct tree_content
{
	unsigned int entry_capacity; /* must match avail_tree_content */
	unsigned int entry_count;
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
	unsigned long last_commit;
	struct tree_entry branch_tree;
	unsigned char sha1[20];
};


/* Stats and misc. counters */
static int max_depth = 10;
static unsigned long alloc_count;
static unsigned long branch_count;
static unsigned long object_count;
static unsigned long duplicate_count;
static unsigned long object_count_by_type[9];
static unsigned long duplicate_count_by_type[9];

/* Memory pools */
static size_t mem_pool_alloc = 2*1024*1024 - sizeof(struct mem_pool);
static size_t total_allocd;
static struct mem_pool *mem_pool;

/* atom management */
static unsigned int atom_table_sz = 4451;
static unsigned int atom_cnt;
static struct atom_str **atom_table;

/* The .pack file being generated */
static int pack_fd;
static unsigned long pack_offset;
static unsigned char pack_sha1[20];

/* Table of objects we've written. */
static unsigned int object_entry_alloc = 1000;
static struct object_entry_pool *blocks;
static struct object_entry *object_table[1 << 16];

/* Our last blob */
static struct last_object last_blob;

/* Tree management */
static unsigned int tree_entry_alloc = 1000;
static void *avail_tree_entry;
static unsigned int avail_tree_table_sz = 100;
static struct avail_tree_content **avail_tree_table;

/* Branch data */
static unsigned int max_active_branches = 5;
static unsigned int cur_active_branches;
static unsigned int branch_table_sz = 1039;
static struct branch **branch_table;
static struct branch *active_branches;


static void alloc_objects(int cnt)
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
	memcpy(e->sha1, sha1, sizeof(e->sha1));
	return e;
}

static struct object_entry* find_object(unsigned char *sha1)
{
	unsigned int h = sha1[0] << 8 | sha1[1];
	struct object_entry *e;
	for (e = object_table[h]; e; e = e->next)
		if (!memcmp(sha1, e->sha1, sizeof(e->sha1)))
			return e;
	return NULL;
}

static struct object_entry* insert_object(unsigned char *sha1)
{
	unsigned int h = sha1[0] << 8 | sha1[1];
	struct object_entry *e = object_table[h];
	struct object_entry *p = NULL;

	while (e) {
		if (!memcmp(sha1, e->sha1, sizeof(e->sha1)))
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

	b = pool_calloc(1, sizeof(struct branch));
	b->name = pool_strdup(name);
	b->table_next_branch = branch_table[hc];
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
	return t;
}

static void release_tree_entry(struct tree_entry *e);
static void release_tree_content(struct tree_content *t)
{
	struct avail_tree_content *f = (struct avail_tree_content*)t;
	unsigned int hc = hc_entries(f->entry_capacity);
	unsigned int i;
	for (i = 0; i < t->entry_count; i++)
		release_tree_entry(t->entries[i]);
	f->next_avail = avail_tree_table[hc];
	avail_tree_table[hc] = f;
}

static struct tree_content* grow_tree_content(
	struct tree_content *t,
	int amt)
{
	struct tree_content *r = new_tree_content(t->entry_count + amt);
	r->entry_count = t->entry_count;
	memcpy(r->entries,t->entries,t->entry_count*sizeof(t->entries[0]));
	release_tree_content(t);
	return r;
}

static struct tree_entry* new_tree_entry()
{
	struct tree_entry *e;

	if (!avail_tree_entry) {
		unsigned int n = tree_entry_alloc;
		avail_tree_entry = e = xmalloc(n * sizeof(struct tree_entry));
		while (n--) {
			*((void**)e) = e + 1;
			e++;
		}
	}

	e = avail_tree_entry;
	avail_tree_entry = *((void**)e);
	return e;
}

static void release_tree_entry(struct tree_entry *e)
{
	if (e->tree)
		release_tree_content(e->tree);
	*((void**)e) = avail_tree_entry;
	avail_tree_entry = e;
}

static void yread(int fd, void *buffer, size_t length)
{
	ssize_t ret = 0;
	while (ret < length) {
		ssize_t size = xread(fd, (char *) buffer + ret, length - ret);
		if (!size)
			die("Read from descriptor %i: end of stream", fd);
		if (size < 0)
			die("Read from descriptor %i: %s", fd, strerror(errno));
		ret += size;
	}
}

static int optional_read(int fd, void *buffer, size_t length)
{
	ssize_t ret = 0;
	while (ret < length) {
		ssize_t size = xread(fd, (char *) buffer + ret, length - ret);
		if (!size && !ret)
			return 1;
		if (!size)
			die("Read from descriptor %i: end of stream", fd);
		if (size < 0)
			die("Read from descriptor %i: %s", fd, strerror(errno));
		ret += size;
	}
	return 0;
}

static void ywrite(int fd, void *buffer, size_t length)
{
	ssize_t ret = 0;
	while (ret < length) {
		ssize_t size = xwrite(fd, (char *) buffer + ret, length - ret);
		if (!size)
			die("Write to descriptor %i: end of file", fd);
		if (size < 0)
			die("Write to descriptor %i: %s", fd, strerror(errno));
		ret += size;
	}
}

static const char* read_path()
{
	static char sn[PATH_MAX];
	unsigned long slen;

	yread(0, &slen, 4);
	if (!slen)
		die("Expected string command parameter, didn't find one");
	if (slen > (PATH_MAX - 1))
		die("Can't handle excessive string length %lu", slen);
	yread(0, sn, slen);
	sn[slen] = 0;
	return sn;
}

static unsigned long encode_header(
	enum object_type type,
	unsigned long size,
	unsigned char *hdr)
{
	int n = 1;
	unsigned char c;

	if (type < OBJ_COMMIT || type > OBJ_DELTA)
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
	unsigned long datlen,
	struct last_object *last,
	unsigned char *sha1out)
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
		memcpy(sha1out, sha1, sizeof(sha1));

	e = insert_object(sha1);
	if (e->offset) {
		duplicate_count++;
		duplicate_count_by_type[type]++;
		return 1;
	}
	e->offset = pack_offset;
	object_count++;
	object_count_by_type[type]++;

	if (last && last->data && last->depth < max_depth)
		delta = diff_delta(last->data, last->len,
			dat, datlen,
			&deltalen, 0);
	else
		delta = 0;

	memset(&s, 0, sizeof(s));
	deflateInit(&s, zlib_compression_level);

	if (delta) {
		last->depth++;
		s.next_in = delta;
		s.avail_in = deltalen;
		hdrlen = encode_header(OBJ_DELTA, deltalen, hdr);
		ywrite(pack_fd, hdr, hdrlen);
		ywrite(pack_fd, last->sha1, sizeof(sha1));
		pack_offset += hdrlen + sizeof(sha1);
	} else {
		if (last)
			last->depth = 0;
		s.next_in = dat;
		s.avail_in = datlen;
		hdrlen = encode_header(type, datlen, hdr);
		ywrite(pack_fd, hdr, hdrlen);
		pack_offset += hdrlen;
	}

	s.avail_out = deflateBound(&s, s.avail_in);
	s.next_out = out = xmalloc(s.avail_out);
	while (deflate(&s, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&s);

	ywrite(pack_fd, out, s.total_out);
	pack_offset += s.total_out;

	free(out);
	if (delta)
		free(delta);
	if (last) {
		if (last->data)
			free(last->data);
		last->data = dat;
		last->len = datlen;
		memcpy(last->sha1, sha1, sizeof(sha1));
	}
	return 0;
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
	struct object_entry *myoe;
	struct tree_content *t;
	unsigned long size;
	char *buf;
	const char *c;
	char type[20];

	root->tree = t = new_tree_content(8);
	if (!memcmp(root->sha1, null_sha1, 20))
		return;

	myoe = find_object(root->sha1);
	if (myoe) {
		die("FIXME");
	} else {
		buf = read_sha1_file(root->sha1, type, &size);
		if (!buf || strcmp(type, tree_type))
			die("Can't load existing tree %s", sha1_to_hex(root->sha1));
	}

	c = buf;
	while (c != (buf + size)) {
		struct tree_entry *e = new_tree_entry();

		if (t->entry_count == t->entry_capacity)
			root->tree = t = grow_tree_content(t, 8);
		t->entries[t->entry_count++] = e;

		e->tree = NULL;
		c = get_mode(c, &e->mode);
		if (!c)
			die("Corrupt mode in %s", sha1_to_hex(root->sha1));
		e->name = to_atom(c, strlen(c));
		c += e->name->str_len + 1;
		memcpy(e->sha1, c, sizeof(e->sha1));
		c += 20;
	}
	free(buf);
}

static int tecmp (const void *_a, const void *_b)
{
	struct tree_entry *a = *((struct tree_entry**)_a);
	struct tree_entry *b = *((struct tree_entry**)_b);
	return base_name_compare(
		a->name->str_dat, a->name->str_len, a->mode,
		b->name->str_dat, b->name->str_len, b->mode);
}

static void store_tree(struct tree_entry *root)
{
	struct tree_content *t = root->tree;
	unsigned int i;
	size_t maxlen;
	char *buf, *c;

	if (memcmp(root->sha1, null_sha1, 20))
		return;

	maxlen = 0;
	for (i = 0; i < t->entry_count; i++) {
		maxlen += t->entries[i]->name->str_len + 34;
		if (t->entries[i]->tree)
			store_tree(t->entries[i]);
	}

	qsort(t->entries, t->entry_count, sizeof(t->entries[0]), tecmp);
	buf = c = xmalloc(maxlen);
	for (i = 0; i < t->entry_count; i++) {
		struct tree_entry *e = t->entries[i];
		c += sprintf(c, "%o", e->mode);
		*c++ = ' ';
		strcpy(c, e->name->str_dat);
		c += e->name->str_len + 1;
		memcpy(c, e->sha1, 20);
		c += 20;
	}
	store_object(OBJ_TREE, buf, c - buf, NULL, root->sha1);
	free(buf);
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
				if (e->mode == mode && !memcmp(e->sha1, sha1, 20))
					return 0;
				e->mode = mode;
				memcpy(e->sha1, sha1, 20);
				if (e->tree) {
					release_tree_content(e->tree);
					e->tree = NULL;
				}
				memcpy(root->sha1, null_sha1, 20);
				return 1;
			}
			if (!S_ISDIR(e->mode)) {
				e->tree = new_tree_content(8);
				e->mode = 040000;
			}
			if (!e->tree)
				load_tree(e);
			if (tree_content_set(e, slash1 + 1, sha1, mode)) {
				memcpy(root->sha1, null_sha1, 20);
				return 1;
			}
			return 0;
		}
	}

	if (t->entry_count == t->entry_capacity)
		root->tree = t = grow_tree_content(t, 8);
	e = new_tree_entry();
	e->name = to_atom(p, n);
	t->entries[t->entry_count++] = e;
	if (slash1) {
		e->tree = new_tree_content(8);
		e->mode = 040000;
		tree_content_set(e, slash1 + 1, sha1, mode);
	} else {
		e->tree = NULL;
		e->mode = mode;
		memcpy(e->sha1, sha1, 20);
	}
	memcpy(root->sha1, null_sha1, 20);
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
			if (!slash1 || !S_ISDIR(e->mode))
				goto del_entry;
			if (!e->tree)
				load_tree(e);
			if (tree_content_remove(e, slash1 + 1)) {
				if (!e->tree->entry_count)
					goto del_entry;
				memcpy(root->sha1, null_sha1, 20);
				return 1;
			}
			return 0;
		}
	}
	return 0;

del_entry:
	for (i++; i < t->entry_count; i++)
		t->entries[i-1] = t->entries[i];
	t->entry_count--;
	release_tree_entry(e);
	memcpy(root->sha1, null_sha1, 20);
	return 1;
}

static void init_pack_header()
{
	const char* magic = "PACK";
	unsigned long version = 3;
	unsigned long zero = 0;

	version = htonl(version);
	ywrite(pack_fd, (char*)magic, 4);
	ywrite(pack_fd, &version, 4);
	ywrite(pack_fd, &zero, 4);
	pack_offset = 4 * 3;
}

static void fixup_header_footer()
{
	SHA_CTX c;
	char hdr[8];
	unsigned long cnt;
	char *buf;
	size_t n;

	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die("Failed seeking to start: %s", strerror(errno));

	SHA1_Init(&c);
	yread(pack_fd, hdr, 8);
	SHA1_Update(&c, hdr, 8);

	cnt = htonl(object_count);
	SHA1_Update(&c, &cnt, 4);
	ywrite(pack_fd, &cnt, 4);

	buf = xmalloc(128 * 1024);
	for (;;) {
		n = xread(pack_fd, buf, 128 * 1024);
		if (n <= 0)
			break;
		SHA1_Update(&c, buf, n);
	}
	free(buf);

	SHA1_Final(pack_sha1, &c);
	ywrite(pack_fd, pack_sha1, sizeof(pack_sha1));
}

static int oecmp (const void *_a, const void *_b)
{
	struct object_entry *a = *((struct object_entry**)_a);
	struct object_entry *b = *((struct object_entry**)_b);
	return memcmp(a->sha1, b->sha1, sizeof(a->sha1));
}

static void write_index(const char *idx_name)
{
	struct sha1file *f;
	struct object_entry **idx, **c, **last;
	struct object_entry *e;
	struct object_entry_pool *o;
	unsigned int array[256];
	int i;

	/* Build the sorted table of object IDs. */
	idx = xmalloc(object_count * sizeof(struct object_entry*));
	c = idx;
	for (o = blocks; o; o = o->next_pool)
		for (e = o->entries; e != o->next_free; e++)
			*c++ = e;
	last = idx + object_count;
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

	f = sha1create("%s", idx_name);
	sha1write(f, array, 256 * sizeof(int));
	for (c = idx; c != last; c++) {
		unsigned int offset = htonl((*c)->offset);
		sha1write(f, &offset, 4);
		sha1write(f, (*c)->sha1, sizeof((*c)->sha1));
	}
	sha1write(f, pack_sha1, sizeof(pack_sha1));
	sha1close(f, NULL, 1);
	free(idx);
}

static void dump_branches()
{
	static const char *msg = "fast-import";
	unsigned int i;
	struct branch *b;
	struct ref_lock *lock;

	for (i = 0; i < branch_table_sz; i++) {
		for (b = branch_table[i]; b; b = b->table_next_branch) {
			lock = lock_any_ref_for_update(b->name, NULL, 0);
			if (!lock || write_ref_sha1(lock, b->sha1, msg) < 0)
				die("Can't write %s", b->name);
		}
	}
}

static void cmd_new_blob()
{
	unsigned long datlen;
	unsigned char sha1[20];
	void *dat;

	yread(0, &datlen, 4);
	dat = xmalloc(datlen);
	yread(0, dat, datlen);
	if (store_object(OBJ_BLOB, dat, datlen, &last_blob, sha1))
		free(dat);
}

static void unload_one_branch()
{
	while (cur_active_branches >= max_active_branches) {
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
			release_tree_content(e->branch_tree.tree);
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
}

static void file_change_m(struct branch *b)
{
	const char *path = read_path();
	char hexsha1[41];
	unsigned char sha1[20];

	yread(0, hexsha1, 40);
	hexsha1[40] = 0;

	if (get_sha1_hex(hexsha1, sha1))
		die("Invalid sha1 %s for %s", hexsha1, path);

	tree_content_set(&b->branch_tree, path, sha1, 0100644);
}

static void file_change_d(struct branch *b)
{
	tree_content_remove(&b->branch_tree, read_path());
}

static void cmd_new_commit()
{
	static const unsigned int max_hdr_len = 94;
	const char *name = read_path();
	struct branch *b = lookup_branch(name);
	unsigned int acmsglen;
	char *body, *c;

	if (!b)
		die("Branch not declared: %s", name);
	if (!b->branch_tree.tree) {
		unload_one_branch();
		load_branch(b);
	}

	/* author_committer_msg */
	yread(0, &acmsglen, 4);
	body = xmalloc(acmsglen + max_hdr_len);
	c = body + max_hdr_len;
	yread(0, c, acmsglen);

	/* file_change* */
	for (;;) {
		unsigned char cmd;
		yread(0, &cmd, 1);
		if (cmd == '0')
			break;
		else if (cmd == 'M')
			file_change_m(b);
		else if (cmd == 'D')
			file_change_d(b);
		else
			die("Unsupported file_change: %c", cmd);
	}

	if (memcmp(b->sha1, null_sha1, 20)) {
		sprintf(c - 48, "parent %s", sha1_to_hex(b->sha1));
		*(c - 1) = '\n';
		c -= 48;
	}
	store_tree(&b->branch_tree);
	sprintf(c - 46, "tree %s", sha1_to_hex(b->branch_tree.sha1));
	*(c - 1) = '\n';
	c -= 46;

	store_object(OBJ_COMMIT,
		c, (body + max_hdr_len + acmsglen) - c,
		NULL, b->sha1);
	free(body);
	b->last_commit = object_count_by_type[OBJ_COMMIT];
}

static void cmd_new_branch()
{
	struct branch *b = new_branch(read_path());
	const char *base = read_path();
	struct branch *s = lookup_branch(base);

	if (!strcmp(b->name, base))
		die("Can't create a branch from itself: %s", base);
	else if (s) {
		memcpy(b->sha1, s->sha1, 20);
		memcpy(b->branch_tree.sha1, s->branch_tree.sha1, 20);
	}
	else if (!get_sha1(base, b->sha1)) {
		if (!memcmp(b->sha1, null_sha1, 20))
			memcpy(b->branch_tree.sha1, null_sha1, 20);
		else {
			unsigned long size;
			char *buf;

			buf = read_object_with_reference(b->sha1,
				type_names[OBJ_COMMIT], &size, b->sha1);
			if (!buf || size < 46)
				die("Not a valid commit: %s", base);
			if (memcmp("tree ", buf, 5)
				|| get_sha1_hex(buf + 5, b->branch_tree.sha1))
				die("The commit %s is corrupt", sha1_to_hex(b->sha1));
			free(buf);
		}
	} else
		die("Not a SHA1 or branch: %s", base);
}

int main(int argc, const char **argv)
{
	const char *base_name = argv[1];
	int est_obj_cnt = atoi(argv[2]);
	char *pack_name;
	char *idx_name;
	struct stat sb;

	setup_ident();
	git_config(git_default_config);

	pack_name = xmalloc(strlen(base_name) + 6);
	sprintf(pack_name, "%s.pack", base_name);
	idx_name = xmalloc(strlen(base_name) + 5);
	sprintf(idx_name, "%s.idx", base_name);

	pack_fd = open(pack_name, O_RDWR|O_CREAT|O_EXCL, 0666);
	if (pack_fd < 0)
		die("Can't create %s: %s", pack_name, strerror(errno));

	alloc_objects(est_obj_cnt);

	atom_table = xcalloc(atom_table_sz, sizeof(struct atom_str*));
	branch_table = xcalloc(branch_table_sz, sizeof(struct branch*));
	avail_tree_table = xcalloc(avail_tree_table_sz, sizeof(struct avail_tree_content*));

	init_pack_header();
	for (;;) {
		unsigned long cmd;
		if (optional_read(0, &cmd, 4))
			break;

		switch (ntohl(cmd)) {
		case 'blob': cmd_new_blob();   break;
		case 'comt': cmd_new_commit(); break;
		case 'brch': cmd_new_branch(); break;
		default:
			die("Invalid command %lu", cmd);
		}
	}
	fixup_header_footer();
	close(pack_fd);
	write_index(idx_name);
	dump_branches();

	fprintf(stderr, "%s statistics:\n", argv[0]);
	fprintf(stderr, "---------------------------------------------------\n");
	fprintf(stderr, "Alloc'd objects: %10lu (%10lu overflow  )\n", alloc_count, alloc_count - est_obj_cnt);
	fprintf(stderr, "Total objects:   %10lu (%10lu duplicates)\n", object_count, duplicate_count);
	fprintf(stderr, "      blobs  :   %10lu (%10lu duplicates)\n", object_count_by_type[OBJ_BLOB], duplicate_count_by_type[OBJ_BLOB]);
	fprintf(stderr, "      trees  :   %10lu (%10lu duplicates)\n", object_count_by_type[OBJ_TREE], duplicate_count_by_type[OBJ_TREE]);
	fprintf(stderr, "      commits:   %10lu (%10lu duplicates)\n", object_count_by_type[OBJ_COMMIT], duplicate_count_by_type[OBJ_COMMIT]);
	fprintf(stderr, "      tags   :   %10lu (%10lu duplicates)\n", object_count_by_type[OBJ_TAG], duplicate_count_by_type[OBJ_TAG]);
	fprintf(stderr, "Total branches:  %10lu\n", branch_count);
	fprintf(stderr, "Total atoms:     %10u\n", atom_cnt);
	fprintf(stderr, "Memory pools:    %10lu MiB\n", total_allocd/(1024*1024));
	fprintf(stderr, "---------------------------------------------------\n");

	stat(pack_name, &sb);
	fprintf(stderr, "Pack size:       %10lu KiB\n", (unsigned long)(sb.st_size/1024));
	stat(idx_name, &sb);
	fprintf(stderr, "Index size:      %10lu KiB\n", (unsigned long)(sb.st_size/1024));

	fprintf(stderr, "\n");

	return 0;
}
