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
#include "csum-file.h"

static int sha1flush(struct sha1file *f, unsigned int count)
{
	void *buf = f->buffer;

	for (;;) {
		int ret = write(f->fd, buf, count);
		if (ret > 0) {
			buf += ret;
			count -= ret;
			if (count)
				continue;
			return 0;
		}
		if (!ret)
			die("sha1 file write error. Out of diskspace");
		if (errno == EAGAIN || errno == EINTR)
			continue;
		die("sha1 file write error (%s)", strerror(errno));
	}
}

int sha1close(struct sha1file *f)
{
	unsigned offset = f->offset;
	if (offset) {
		SHA1_Update(&f->ctx, f->buffer, offset);
		sha1flush(f, offset);
	}
	SHA1_Final(f->buffer, &f->ctx);
	sha1flush(f, 20);
	return 0;
}

int sha1write(struct sha1file *f, void *buf, unsigned int count)
{
	while (count) {
		unsigned offset = f->offset;
		unsigned left = sizeof(f->buffer) - offset;
		unsigned nr = count > left ? left : count;

		memcpy(f->buffer + offset, buf, nr);
		count -= nr;
		offset += nr;
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

struct sha1file *sha1create(const char *fmt, ...)
{
	static char filename[PATH_MAX];
	struct sha1file *f;
	unsigned len;
	va_list arg;
	int fd;

	va_start(arg, fmt);
	len = vsnprintf(filename, PATH_MAX, fmt, arg);
	va_end(arg);

	if (len >= PATH_MAX)
		die("you wascally wabbit, you");
	fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0644);
	if (fd < 0)
		die("unable to open %s (%s)", filename, strerror(errno));
	f = xmalloc(sizeof(*f));
	f->fd = fd;
	f->error = 0;
	f->offset = 0;
	SHA1_Init(&f->ctx);
	return f;
}

int sha1write_compressed(struct sha1file *f, void *in, unsigned int size)
{
	z_stream stream;
	unsigned long maxsize;
	void *out;

	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, Z_DEFAULT_COMPRESSION);
	maxsize = deflateBound(&stream, size);
	out = xmalloc(maxsize);

	/* Compress it */
	stream.next_in = in;
	stream.avail_in = size;

	stream.next_out = out;
	stream.avail_out = maxsize;

	while (deflate(&stream, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&stream);

	size = stream.total_out;
	sha1write(f, out, size);
	free(out);
	return size;
}


