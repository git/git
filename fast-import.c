#include "builtin.h"
#include "cache.h"
#include "object.h"
#include "blob.h"
#include "delta.h"
#include "pack.h"
#include "csum-file.h"

struct object_entry
{
	struct object_entry *next;
	unsigned long offset;
	unsigned char sha1[20];
};

struct object_entry_block
{
	struct object_entry_block *next_block;
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

struct tree;
struct tree_entry
{
	struct tree *tree;
	mode_t mode;
	unsigned char sha1[20];
	char name[FLEX_ARRAY]; /* more */
};

struct tree
{
	struct last_object last_tree;
	unsigned long entry_count;
	struct tree_entry **entries;
};

struct branch
{
	struct branch *next_branch;
	struct tree_entry tree;
	unsigned char sha1[20];
	const char *name;
};

/* Stats and misc. counters. */
static int max_depth = 10;
static unsigned long alloc_count;
static unsigned long branch_count;
static unsigned long object_count;
static unsigned long duplicate_count;
static unsigned long object_count_by_type[9];
static unsigned long duplicate_count_by_type[9];

/* The .pack file */
static int pack_fd;
static unsigned long pack_offset;
static unsigned char pack_sha1[20];

/* Table of objects we've written. */
struct object_entry_block *blocks;
struct object_entry *object_table[1 << 16];

/* Our last blob */
struct last_object last_blob;

/* Branch data */
struct branch *branches;
struct branch *current_branch;

static void alloc_objects(int cnt)
{
	struct object_entry_block *b;

	b = xmalloc(sizeof(struct object_entry_block)
		+ cnt * sizeof(struct object_entry));
	b->next_block = blocks;
	b->next_free = b->entries;
	b->end = b->entries + cnt;
	blocks = b;
	alloc_count += cnt;
}

static struct object_entry* new_object(unsigned char *sha1)
{
	struct object_entry *e;

	if (blocks->next_free == blocks->end)
		alloc_objects(1000);

	e = blocks->next_free++;
	memcpy(e->sha1, sha1, sizeof(e->sha1));
	return e;
}

static struct object_entry* insert_object(unsigned char *sha1)
{
	unsigned int h = sha1[0] << 8 | sha1[1];
	struct object_entry *e = object_table[h];
	struct object_entry *p = 0;

	while (e) {
		if (!memcmp(sha1, e->sha1, sizeof(e->sha1)))
			return e;
		p = e;
		e = e->next;
	}

	e = new_object(sha1);
	e->next = 0;
	e->offset = 0;
	if (p)
		p->next = e;
	else
		object_table[h] = e;
	return e;
}

static ssize_t yread(int fd, void *buffer, size_t length)
{
	ssize_t ret = 0;
	while (ret < length) {
		ssize_t size = xread(fd, (char *) buffer + ret, length - ret);
		if (size < 0) {
			return size;
		}
		if (size == 0) {
			return ret;
		}
		ret += size;
	}
	return ret;
}

static ssize_t ywrite(int fd, void *buffer, size_t length)
{
	ssize_t ret = 0;
	while (ret < length) {
		ssize_t size = xwrite(fd, (char *) buffer + ret, length - ret);
		if (size < 0) {
			return size;
		}
		if (size == 0) {
			return ret;
		}
		ret += size;
	}
	return ret;
}

static const char* read_string()
{
	static char sn[PATH_MAX];
	unsigned long slen;

	if (yread(0, &slen, 4) != 4)
		die("Can't obtain string");
	if (!slen)
		return 0;
	if (slen > (PATH_MAX - 1))
		die("Can't handle excessive string length %lu", slen);

	if (yread(0, sn, slen) != slen)
		die("Can't obtain string of length %lu", slen);
	sn[slen] = 0;
	return sn;
}

static const char* read_required_string()
{
	const char *r = read_string();
	if (!r)
		die("Expected string command parameter, didn't find one");
	return r;
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
		return 0;
	}
	e->offset = pack_offset;
	object_count++;
	object_count_by_type[type]++;

	if (last->data && last->depth < max_depth)
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
		if (ywrite(pack_fd, hdr, hdrlen) != hdrlen)
			die("Can't write object header: %s", strerror(errno));
		if (ywrite(pack_fd, last->sha1, sizeof(sha1)) != sizeof(sha1))
			die("Can't write object base: %s", strerror(errno));
		pack_offset += hdrlen + sizeof(sha1);
	} else {
		last->depth = 0;
		s.next_in = dat;
		s.avail_in = datlen;
		hdrlen = encode_header(type, datlen, hdr);
		if (ywrite(pack_fd, hdr, hdrlen) != hdrlen)
			die("Can't write object header: %s", strerror(errno));
		pack_offset += hdrlen;
	}

	s.avail_out = deflateBound(&s, s.avail_in);
	s.next_out = out = xmalloc(s.avail_out);
	while (deflate(&s, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&s);

	if (ywrite(pack_fd, out, s.total_out) != s.total_out)
		die("Failed writing compressed data %s", strerror(errno));
	pack_offset += s.total_out;

	free(out);
	if (delta)
		free(delta);
	if (last->data)
		free(last->data);
	last->data = dat;
	last->len = datlen;
	memcpy(last->sha1, sha1, sizeof(sha1));
	return 1;
}

static void init_pack_header()
{
	const char* magic = "PACK";
	unsigned long version = 3;
	unsigned long zero = 0;

	version = htonl(version);

	if (ywrite(pack_fd, (char*)magic, 4) != 4)
		die("Can't write pack magic: %s", strerror(errno));
	if (ywrite(pack_fd, &version, 4) != 4)
		die("Can't write pack version: %s", strerror(errno));
	if (ywrite(pack_fd, &zero, 4) != 4)
		die("Can't write 0 object count: %s", strerror(errno));
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
	if (yread(pack_fd, hdr, 8) != 8)
		die("Failed reading header: %s", strerror(errno));
	SHA1_Update(&c, hdr, 8);

	cnt = htonl(object_count);
	SHA1_Update(&c, &cnt, 4);
	if (ywrite(pack_fd, &cnt, 4) != 4)
		die("Failed writing object count: %s", strerror(errno));

	buf = xmalloc(128 * 1024);
	for (;;) {
		n = xread(pack_fd, buf, 128 * 1024);
		if (n <= 0)
			break;
		SHA1_Update(&c, buf, n);
	}
	free(buf);

	SHA1_Final(pack_sha1, &c);
	if (ywrite(pack_fd, pack_sha1, sizeof(pack_sha1)) != sizeof(pack_sha1))
		die("Failed writing pack checksum: %s", strerror(errno));
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
	struct object_entry_block *o;
	unsigned int array[256];
	int i;

	/* Build the sorted table of object IDs. */
	idx = xmalloc(object_count * sizeof(struct object_entry*));
	c = idx;
	for (o = blocks; o; o = o->next_block)
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

static void new_blob()
{
	unsigned long datlen;
	void *dat;

	if (yread(0, &datlen, 4) != 4)
		die("Can't obtain blob length");

	dat = xmalloc(datlen);
	if (yread(0, dat, datlen) != datlen)
		die("Con't obtain %lu bytes of blob data", datlen);

	if (!store_object(OBJ_BLOB, dat, datlen, &last_blob, 0))
		free(dat);
}

static struct branch* lookup_branch(const char *name)
{
	struct branch *b;
	for (b = branches; b; b = b->next_branch) {
		if (!strcmp(name, b->name))
			return b;
	}
	die("No branch named '%s' has been declared", name);
}

static struct tree* deep_copy_tree (struct tree *t)
{
	struct tree *r = xmalloc(sizeof(struct tree));
	unsigned long i;

	if (t->last_tree.data) {
		r->last_tree.data = xmalloc(t->last_tree.len);
		r->last_tree.len = t->last_tree.len;
		r->last_tree.depth = t->last_tree.depth;
		memcpy(r->last_tree.data, t->last_tree.data, t->last_tree.len);
		memcpy(r->last_tree.sha1, t->last_tree.sha1, sizeof(t->last_tree.sha1));
	}

	r->entry_count = t->entry_count;
	r->entries = xmalloc(t->entry_count * sizeof(struct tree_entry*));
	for (i = 0; i < t->entry_count; i++) {
		struct tree_entry *a = t->entries[i];
		struct tree_entry *b;

		b = xmalloc(sizeof(struct tree_entry) + strlen(a->name) + 1);
		b->tree = a->tree ? deep_copy_tree(a->tree) : 0;
		b->mode = a->mode;
		memcpy(b->sha1, a->sha1, sizeof(a->sha1));
		strcpy(b->name, a->name);
		r->entries[i] = b;
	}

	return r;
}

static void store_tree (struct tree_entry *e)
{
	struct tree *t = e->tree;
	unsigned long maxlen, i;
	char *buf, *c;

	if (memcmp(null_sha1, e->sha1, sizeof(e->sha1)))
		return;

	maxlen = t->entry_count * 32;
	for (i = 0; i < t->entry_count; i++)
		maxlen += strlen(t->entries[i]->name);

	buf = c = xmalloc(maxlen);
	for (i = 0; i < t->entry_count; i++) {
		struct tree_entry *e = t->entries[i];
		c += sprintf(c, "%o %s", e->mode, e->name) + 1;
		if (e->tree)
			store_tree(e);
		memcpy(c, e->sha1, sizeof(e->sha1));
		c += sizeof(e->sha1);
	}

	if (!store_object(OBJ_TREE, buf, c - buf, &t->last_tree, e->sha1))
		free(buf);
}

static void new_branch()
{
	struct branch *nb = xcalloc(1, sizeof(struct branch));
	const char *source_name;

	nb->name = strdup(read_required_string());
	source_name = read_string();
	if (source_name) {
		struct branch *sb = lookup_branch(source_name);
		nb->tree.tree = deep_copy_tree(sb->tree.tree);
		memcpy(nb->tree.sha1, sb->tree.sha1, sizeof(sb->tree.sha1));
		memcpy(nb->sha1, sb->sha1, sizeof(sb->sha1));
	} else {
		nb->tree.tree = xcalloc(1, sizeof(struct tree));
		nb->tree.tree->entries = xmalloc(8*sizeof(struct tree_entry*));
	}
	nb->next_branch = branches;
	branches = nb;
	branch_count++;
}

static void set_branch()
{
	current_branch = lookup_branch(read_required_string());
}

static void commit()
{
	store_tree(&current_branch->tree);
}

int main(int argc, const char **argv)
{
	const char *base_name = argv[1];
	int est_obj_cnt = atoi(argv[2]);
	char *pack_name;
	char *idx_name;
	struct stat sb;

	pack_name = xmalloc(strlen(base_name) + 6);
	sprintf(pack_name, "%s.pack", base_name);
	idx_name = xmalloc(strlen(base_name) + 5);
	sprintf(idx_name, "%s.idx", base_name);

	pack_fd = open(pack_name, O_RDWR|O_CREAT|O_EXCL, 0666);
	if (pack_fd < 0)
		die("Can't create %s: %s", pack_name, strerror(errno));

	alloc_objects(est_obj_cnt);
	init_pack_header();
	for (;;) {
		unsigned long cmd;
		if (yread(0, &cmd, 4) != 4)
			break;

		switch (cmd) {
		case 'blob': new_blob();   break;
		case 'newb': new_branch(); break;
		case 'setb': set_branch(); break;
		case 'comt': commit();     break;
		default:
			die("Invalid command %lu", cmd);
		}
	}
	fixup_header_footer();
	close(pack_fd);
	write_index(idx_name);

	fprintf(stderr, "%s statistics:\n", argv[0]);
	fprintf(stderr, "---------------------------------------------------\n");
	fprintf(stderr, "Alloc'd objects: %10lu (%10lu overflow  )\n", alloc_count, alloc_count - est_obj_cnt);
	fprintf(stderr, "Total objects:   %10lu (%10lu duplicates)\n", object_count, duplicate_count);
	fprintf(stderr, "      blobs  :   %10lu (%10lu duplicates)\n", object_count_by_type[OBJ_BLOB], duplicate_count_by_type[OBJ_BLOB]);
	fprintf(stderr, "      trees  :   %10lu (%10lu duplicates)\n", object_count_by_type[OBJ_TREE], duplicate_count_by_type[OBJ_TREE]);
	fprintf(stderr, "      commits:   %10lu (%10lu duplicates)\n", object_count_by_type[OBJ_COMMIT], duplicate_count_by_type[OBJ_COMMIT]);
	fprintf(stderr, "      tags   :   %10lu (%10lu duplicates)\n", object_count_by_type[OBJ_TAG], duplicate_count_by_type[OBJ_TAG]);
	fprintf(stderr, "Total branches:  %10lu\n", branch_count);
	fprintf(stderr, "---------------------------------------------------\n");

	stat(pack_name, &sb);
	fprintf(stderr, "Pack size:       %10lu KiB\n", (unsigned long)(sb.st_size/1024));
	stat(idx_name, &sb);
	fprintf(stderr, "Index size:      %10lu KiB\n", (unsigned long)(sb.st_size/1024));

	fprintf(stderr, "\n");

	return 0;
}
