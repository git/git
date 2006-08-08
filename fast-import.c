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
	unsigned long len;
	int depth;
	unsigned char sha1[20];
};

/* Stats and misc. counters. */
static int max_depth = 10;
static unsigned long alloc_count;
static unsigned long object_count;
static unsigned long duplicate_count;

/* The .pack file */
static int pack_fd;
static unsigned long pack_offset;
static unsigned char pack_sha1[20];

/* Table of objects we've written. */
struct object_entry_block *blocks;
struct object_entry *object_table[1 << 16];

/* Our last blob */
struct last_object last_blob;

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
	struct last_object *last)
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

	e = insert_object(sha1);
	if (e->offset) {
		duplicate_count++;
		return 0;
	}
	e->offset = pack_offset;
	object_count++;

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
	unsigned long version = 2;
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

int main(int argc, const char **argv)
{
	const char *base_name = argv[1];
	int est_obj_cnt = atoi(argv[2]);
	char *pack_name;
	char *idx_name;

	pack_name = xmalloc(strlen(base_name) + 6);
	sprintf(pack_name, "%s.pack", base_name);
	idx_name = xmalloc(strlen(base_name) + 5);
	sprintf(idx_name, "%s.idx", base_name);

	pack_fd = open(pack_name, O_RDWR|O_CREAT|O_EXCL, 0666);
	if (pack_fd < 0)
		die("Can't create pack file %s: %s", pack_name, strerror(errno));

	alloc_objects(est_obj_cnt);
	init_pack_header();
	for (;;) {
		unsigned long datlen;
		void *dat;

		if (yread(0, &datlen, 4) != 4)
			break;

		dat = xmalloc(datlen);
		if (yread(0, dat, datlen) != datlen)
			break;

		if (!store_object(OBJ_BLOB, dat, datlen, &last_blob))
			free(dat);
	}
	fixup_header_footer();
	close(pack_fd);
	write_index(idx_name);

	fprintf(stderr, "%lu objects, %lu duplicates, %lu allocated (%lu overflow)\n",
		object_count, duplicate_count, alloc_count, alloc_count - est_obj_cnt);

	return 0;
}
