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

static void flush(struct sha1file *f, void *buf, unsigned int count)
{
	if (0 <= f->check_fd && count)  {
		unsigned char check_buffer[8192];
		ssize_t ret = read_in_full(f->check_fd, check_buffer, count);

		if (ret < 0)
			die_errno("%s: sha1 file read error", f->name);
		if (ret < count)
			die("%s: sha1 file truncated", f->name);
		if (memcmp(buf, check_buffer, count))
			die("sha1 file '%s' validation error", f->name);
	}

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
		die_errno("sha1 file '%s' write error", f->name);
	}
}

void sha1flush(struct sha1file *f)
{
	unsigned offset = f->offset;

	if (offset) {
		git_SHA1_Update(&f->ctx, f->buffer, offset);
		flush(f, f->buffer, offset);
		f->offset = 0;
	}
}

int sha1close(struct sha1file *f, unsigned char *result, unsigned int flags)
{
	int fd;

	sha1flush(f);
	git_SHA1_Final(f->buffer, &f->ctx);
	if (result)
		hashcpy(result, f->buffer);
	if (flags & (CSUM_CLOSE | CSUM_FSYNC)) {
		/* write checksum and close fd */
		flush(f, f->buffer, 20);
		if (flags & CSUM_FSYNC)
			fsync_or_die(f->fd, f->name);
		if (close(f->fd))
			die_errno("%s: sha1 file error on close", f->name);
		fd = 0;
	} else
		fd = f->fd;
	if (0 <= f->check_fd) {
		char discard;
		int cnt = read_in_full(f->check_fd, &discard, 1);
		if (cnt < 0)
			die_errno("%s: error when reading the tail of sha1 file",
				  f->name);
		if (cnt)
			die("%s: sha1 file has trailing garbage", f->name);
		if (close(f->check_fd))
			die_errno("%s: sha1 file error on close", f->name);
	}
	free(f);
	return fd;
}

int sha1write(struct sha1file *f, void *buf, unsigned int count)
{
	while (count) {
		unsigned offset = f->offset;
		unsigned left = sizeof(f->buffer) - offset;
		unsigned nr = count > left ? left : count;
		void *data;

		if (f->do_crc)
			f->crc32 = crc32(f->crc32, buf, nr);

		if (nr == sizeof(f->buffer)) {
			/* process full buffer directly without copy */
			data = buf;
		} else {
			memcpy(f->buffer + offset, buf, nr);
			data = f->buffer;
		}

		count -= nr;
		offset += nr;
		buf = (char *) buf + nr;
		left -= nr;
		if (!left) {
			git_SHA1_Update(&f->ctx, data, offset);
			flush(f, data, offset);
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

struct sha1file *sha1fd_check(const char *name)
{
	int sink, check;
	struct sha1file *f;

	sink = open("/dev/null", O_WRONLY);
	if (sink < 0)
		return NULL;
	check = open(name, O_RDONLY);
	if (check < 0) {
		int saved_errno = errno;
		close(sink);
		errno = saved_errno;
		return NULL;
	}
	f = sha1fd(sink, name);
	f->check_fd = check;
	return f;
}

struct sha1file *sha1fd_throughput(int fd, const char *name, struct progress *tp)
{
	struct sha1file *f = xmalloc(sizeof(*f));
	f->fd = fd;
	f->check_fd = -1;
	f->offset = 0;
	f->total = 0;
	f->tp = tp;
	f->name = name;
	f->do_crc = 0;
	git_SHA1_Init(&f->ctx);
	return f;
}

void crc32_begin(struct sha1file *f)
{
	f->crc32 = crc32(0, NULL, 0);
	f->do_crc = 1;
}

uint32_t crc32_end(struct sha1file *f)
{
	f->do_crc = 0;
	return f->crc32;
}
