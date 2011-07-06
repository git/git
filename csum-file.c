/*
 * csum-file.c
 *
 * Copyright (C) 2005 Linus Torvalds
 *
 * Simple file write infrastructure for writing SHA1-summed
 * files. Useful when you write a file that you want to be
 * able to verify hasn't been messed with afterwards.
 */
#include "cache.h"
#include "progress.h"
#include "csum-file.h"

static void sha1flush(struct sha1file *f, unsigned int count)
{
	void *buf = f->buffer;

	for (;;) {
		int ret = xwrite(f->fd, buf, count);
		if (ret > 0) {
			f->total += ret;
			display_throughput(f->tp, f->total);
			buf = (char *) buf + ret;
			count -= ret;
			if (count)
				continue;
			return;
		}
		if (!ret)
			die("sha1 file '%s' write error. Out of diskspace", f->name);
		die("sha1 file '%s' write error (%s)", f->name, strerror(errno));
	}
}

int sha1close(struct sha1file *f, unsigned char *result, unsigned int flags)
{
	int fd;
	unsigned offset = f->offset;

	if (offset) {
		SHA1_Update(&f->ctx, f->buffer, offset);
		sha1flush(f, offset);
		f->offset = 0;
	}
	if (flags & (CSUM_CLOSE | CSUM_FSYNC)) {
		/* write checksum and close fd */
		SHA1_Final(f->buffer, &f->ctx);
		if (result)
			hashcpy(result, f->buffer);
		sha1flush(f, 20);
		if (flags & CSUM_FSYNC)
			fsync_or_die(f->fd, f->name);
		if (close(f->fd))
			die("%s: sha1 file error on close (%s)",
			    f->name, strerror(errno));
		fd = 0;
	} else
		fd = f->fd;
	free(f);
	return fd;
}

int sha1write(struct sha1file *f, void *buf, unsigned int count)
{
	if (f->do_crc)
		f->crc32 = crc32(f->crc32, buf, count);
	while (count) {
		unsigned offset = f->offset;
		unsigned left = sizeof(f->buffer) - offset;
		unsigned nr = count > left ? left : count;

		memcpy(f->buffer + offset, buf, nr);
		count -= nr;
		offset += nr;
		buf = (char *) buf + nr;
		left -= nr;
		if (!left) {
			SHA1_Update(&f->ctx, f->buffer, offset);
			sha1flush(f, offset);
			offset = 0;
		}
		f->offset = offset;
	}
	return 0;
}

struct sha1file *sha1fd(int fd, const char *name)
{
	return sha1fd_throughput(fd, name, NULL);
}

struct sha1file *sha1fd_throughput(int fd, const char *name, struct progress *tp)
{
	struct sha1file *f = xmalloc(sizeof(*f));
	f->fd = fd;
	f->offset = 0;
	f->total = 0;
	f->tp = tp;
	f->name = name;
	f->do_crc = 0;
	SHA1_Init(&f->ctx);
	return f;
}

void crc32_begin(struct sha1file *f)
{
	f->crc32 = crc32(0, Z_NULL, 0);
	f->do_crc = 1;
}

uint32_t crc32_end(struct sha1file *f)
{
	f->do_crc = 0;
	return f->crc32;
}
