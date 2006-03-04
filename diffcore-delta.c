#include "cache.h"
#include "diff.h"
#include "diffcore.h"

struct linehash {
	unsigned long bytes;
	unsigned long hash;
};

static unsigned long hash_extended_line(const unsigned char **buf_p,
					unsigned long left)
{
	/* An extended line is zero or more whitespace letters (including LF)
	 * followed by one non whitespace letter followed by zero or more
	 * non LF, and terminated with by a LF (or EOF).
	 */
	const unsigned char *bol = *buf_p;
	const unsigned char *buf = bol;
	unsigned long hashval = 0;
	while (left) {
		unsigned c = *buf++;
		if (!c)
			goto binary;
		left--;
		if (' ' < c) {
			hashval = c;
			break;
		}
	}
	while (left) {
		unsigned c = *buf++;
		if (!c)
			goto binary;
		left--;
		if (c == '\n')
			break;
		if (' ' < c)
			hashval = hashval * 11 + c;
	}
	*buf_p = buf;
	return hashval;

 binary:
	*buf_p = NULL;
	return 0;
}

static int linehash_compare(const void *a_, const void *b_)
{
	struct linehash *a = (struct linehash *) a_;
	struct linehash *b = (struct linehash *) b_;
	if (a->hash < b->hash) return -1;
	if (a->hash > b->hash) return 1;
	return 0;
}

static struct linehash *hash_lines(const unsigned char *buf,
				   unsigned long size)
{
	const unsigned char *eobuf = buf + size;
	struct linehash *line = NULL;
	int alloc = 0, used = 0;

	while (buf < eobuf) {
		const unsigned char *ptr = buf;
		unsigned long hash = hash_extended_line(&buf, eobuf-ptr);
		if (!buf) {
			free(line);
			return NULL;
		}
		if (alloc <= used) {
			alloc = alloc_nr(alloc);
			line = xrealloc(line, sizeof(*line) * alloc);
		}
		line[used].bytes = buf - ptr;
		line[used].hash = hash;
		used++;
	}
	qsort(line, used, sizeof(*line), linehash_compare);

	/* Terminate the list */
	if (alloc <= used)
		line = xrealloc(line, sizeof(*line) * (used+1));
	line[used].bytes = line[used].hash = 0;
	return line;
}

int diffcore_count_changes(void *src, unsigned long src_size,
			   void *dst, unsigned long dst_size,
			   unsigned long delta_limit,
			   unsigned long *src_copied,
			   unsigned long *literal_added)
{
	struct linehash *src_lines, *dst_lines;
	unsigned long sc, la;

	src_lines = hash_lines(src, src_size);
	if (!src_lines)
		return -1;
	dst_lines = hash_lines(dst, dst_size);
	if (!dst_lines) {
		free(src_lines);
		return -1;
	}
	sc = la = 0;
	while (src_lines->bytes && dst_lines->bytes) {
		int cmp = linehash_compare(src_lines, dst_lines);
		if (!cmp) {
			sc += src_lines->bytes;
			src_lines++;
			dst_lines++;
			continue;
		}
		if (cmp < 0) {
			src_lines++;
			continue;
		}
		la += dst_lines->bytes;
		dst_lines++;
	}
	while (dst_lines->bytes) {
		la += dst_lines->bytes;
		dst_lines++;
	}
	*src_copied = sc;
	*literal_added = la;
	return 0;
}
