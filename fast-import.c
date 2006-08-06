#include "builtin.h"
#include "cache.h"
#include "object.h"
#include "blob.h"
#include "delta.h"
#include "pack.h"
#include "csum-file.h"

static int max_depth = 10;
static unsigned long object_count;
static unsigned long duplicate_count;
static unsigned long packoff;
static unsigned long overflow_count;
static int packfd;
static int current_depth;
static void *lastdat;
static unsigned long lastdatlen;
static unsigned char lastsha1[20];
static unsigned char packsha1[20];

struct object_entry
{
	struct object_entry *next;
	unsigned long offset;
	unsigned char sha1[20];
};

struct overflow_object_entry
{
	struct overflow_object_entry *next;
	struct object_entry oe;
};

struct object_entry *pool_start;
struct object_entry *pool_next;
struct object_entry *pool_end;
struct overflow_object_entry *overflow;
struct object_entry *table[1 << 16];

static struct object_entry* new_object(unsigned char *sha1)
{
	if (pool_next != pool_end) {
		struct object_entry *e = pool_next++;
		memcpy(e->sha1, sha1, sizeof(e->sha1));
		return e;
	} else {
		struct overflow_object_entry *e;

		e = xmalloc(sizeof(struct overflow_object_entry));
		e->next = overflow;
		memcpy(e->oe.sha1, sha1, sizeof(e->oe.sha1));
		overflow = e;
		overflow_count++;
		return &e->oe;
	}
}

static struct object_entry* insert_object(unsigned char *sha1)
{
	unsigned int h = sha1[0] << 8 | sha1[1];
	struct object_entry *e = table[h];
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
		table[h] = e;
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

static unsigned long encode_header(enum object_type type, unsigned long size, unsigned char *hdr)
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

static void write_blob(void *dat, unsigned long datlen)
{
	z_stream s;
	void *out, *delta;
	unsigned char hdr[64];
	unsigned long hdrlen, deltalen;

	if (lastdat && current_depth < max_depth) {
		delta = diff_delta(lastdat, lastdatlen,
			dat, datlen,
			&deltalen, 0);
	} else
		delta = 0;

	memset(&s, 0, sizeof(s));
	deflateInit(&s, zlib_compression_level);

	if (delta) {
		current_depth++;
		s.next_in = delta;
		s.avail_in = deltalen;
		hdrlen = encode_header(OBJ_DELTA, deltalen, hdr);
		if (ywrite(packfd, hdr, hdrlen) != hdrlen)
			die("Can't write object header: %s", strerror(errno));
		if (ywrite(packfd, lastsha1, sizeof(lastsha1)) != sizeof(lastsha1))
			die("Can't write object base: %s", strerror(errno));
		packoff += hdrlen + sizeof(lastsha1);
	} else {
		current_depth = 0;
		s.next_in = dat;
		s.avail_in = datlen;
		hdrlen = encode_header(OBJ_BLOB, datlen, hdr);
		if (ywrite(packfd, hdr, hdrlen) != hdrlen)
			die("Can't write object header: %s", strerror(errno));
		packoff += hdrlen;
	}

	s.avail_out = deflateBound(&s, s.avail_in);
	s.next_out = out = xmalloc(s.avail_out);
	while (deflate(&s, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&s);

	if (ywrite(packfd, out, s.total_out) != s.total_out)
		die("Failed writing compressed data %s", strerror(errno));
	packoff += s.total_out;

	free(out);
	if (delta)
		free(delta);
}

static void init_pack_header()
{
	const char* magic = "PACK";
	unsigned long version = 2;
	unsigned long zero = 0;

	version = htonl(version);

	if (ywrite(packfd, (char*)magic, 4) != 4)
		die("Can't write pack magic: %s", strerror(errno));
	if (ywrite(packfd, &version, 4) != 4)
		die("Can't write pack version: %s", strerror(errno));
	if (ywrite(packfd, &zero, 4) != 4)
		die("Can't write 0 object count: %s", strerror(errno));
	packoff = 4 * 3;
}

static void fixup_header_footer()
{
	SHA_CTX c;
	char hdr[8];
	unsigned long cnt;
	char *buf;
	size_t n;

	if (lseek(packfd, 0, SEEK_SET) != 0)
		die("Failed seeking to start: %s", strerror(errno));

	SHA1_Init(&c);
	if (yread(packfd, hdr, 8) != 8)
		die("Failed reading header: %s", strerror(errno));
	SHA1_Update(&c, hdr, 8);

	cnt = htonl(object_count);
	SHA1_Update(&c, &cnt, 4);
	if (ywrite(packfd, &cnt, 4) != 4)
		die("Failed writing object count: %s", strerror(errno));

	buf = xmalloc(128 * 1024);
	for (;;) {
		n = xread(packfd, buf, 128 * 1024);
		if (n <= 0)
			break;
		SHA1_Update(&c, buf, n);
	}
	free(buf);

	SHA1_Final(packsha1, &c);
	if (ywrite(packfd, packsha1, sizeof(packsha1)) != sizeof(packsha1))
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
	struct overflow_object_entry *o;
	unsigned int array[256];
	int i;

	/* Build the sorted table of object IDs. */
	idx = xmalloc(object_count * sizeof(struct object_entry*));
	c = idx;
	for (e = pool_start; e != pool_next; e++)
		*c++ = e;
	for (o = overflow; o; o = o->next)
		*c++ = &o->oe;
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
	sha1write(f, packsha1, sizeof(packsha1));
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

	packfd = open(pack_name, O_RDWR|O_CREAT|O_TRUNC, 0666);
	if (packfd < 0)
		die("Can't create pack file %s: %s", pack_name, strerror(errno));

	pool_start = xmalloc(est_obj_cnt * sizeof(struct object_entry));
	pool_next = pool_start;
	pool_end = pool_start + est_obj_cnt;

	init_pack_header();
	for (;;) {
		unsigned long datlen;
		int hdrlen;
		void *dat;
		char hdr[128];
		unsigned char sha1[20];
		SHA_CTX c;
		struct object_entry *e;

		if (yread(0, &datlen, 4) != 4)

			break;

		dat = xmalloc(datlen);
		if (yread(0, dat, datlen) != datlen)
			break;

		hdrlen = sprintf(hdr, "blob %lu", datlen) + 1;
		SHA1_Init(&c);
		SHA1_Update(&c, hdr, hdrlen);
		SHA1_Update(&c, dat, datlen);
		SHA1_Final(sha1, &c);

		e = insert_object(sha1);
		if (!e->offset) {
			e->offset = packoff;
			write_blob(dat, datlen);
			object_count++;
			printf("%s\n", sha1_to_hex(sha1));
			fflush(stdout);

			if (lastdat)
				free(lastdat);
			lastdat = dat;
			lastdatlen = datlen;
			memcpy(lastsha1, sha1, sizeof(sha1));
		} else {
			duplicate_count++;
			free(dat);
		}
	}
	fixup_header_footer();
	close(packfd);
	write_index(idx_name);

	fprintf(stderr, "%lu objects, %lu duplicates, %lu pool overflow\n",
		object_count, duplicate_count, overflow_count);

	return 0;
}
