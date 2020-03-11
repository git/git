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

static void flush(struct hashfile *f, const void *buf, unsigned int count)
{
	if (0 <= f->check_fd && count)  {
		unsigned char check_buffer[8192];
		ssize_t ret = read_in_full(f->check_fd, check_buffer, count);

		if (ret < 0)
			die_errno("%s: sha1 file read error", f->name);
		if (ret != count)
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

void hashflush(struct hashfile *f)
{
	unsigned offset = f->offset;

	if (offset) {
		the_hash_algo->update_fn(&f->ctx, f->buffer, offset);
		flush(f, f->buffer, offset);
		f->offset = 0;
	}
}

int finalize_hashfile(struct hashfile *f, unsigned char *result, unsigned int flags)
{
	int fd;

	hashflush(f);
	the_hash_algo->final_fn(f->buffer, &f->ctx);
	if (result)
		hashcpy(result, f->buffer);
	if (flags & CSUM_HASH_IN_STREAM)
		flush(f, f->buffer, the_hash_algo->rawsz);
	if (flags & CSUM_FSYNC)
		fsync_or_die(f->fd, f->name);
	if (flags & CSUM_CLOSE) {
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

void hashwrite(struct hashfile *f, const void *buf, unsigned int count)
{
	while (count) {
		unsigned offset = f->offset;
		unsigned left = sizeof(f->buffer) - offset;
		unsigned nr = count > left ? left : count;
		const void *data;

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
			the_hash_algo->update_fn(&f->ctx, data, offset);
			flush(f, data, offset);
			offset = 0;
		}
		f->offset = offset;
	}
}

struct hashfile *hashfd(int fd, const char *name)
{
	return hashfd_throughput(fd, name, NULL);
}

struct hashfile *hashfd_check(const char *name)
{
	int sink, check;
	struct hashfile *f;

	sink = open("/dev/null", O_WRONLY);
	if (sink < 0)
		die_errno("unable to open /dev/null");
	check = open(name, O_RDONLY);
	if (check < 0)
		die_errno("unable to open '%s'", name);
	f = hashfd(sink, name);
	f->check_fd = check;
	return f;
}

struct hashfile *hashfd_throughput(int fd, const char *name, struct progress *tp)
{
	struct hashfile *f = xmalloc(sizeof(*f));
	f->fd = fd;
	f->check_fd = -1;
	f->offset = 0;
	f->total = 0;
	f->tp = tp;
	f->name = name;
	f->do_crc = 0;
	the_hash_algo->init_fn(&f->ctx);
	return f;
}

void hashfile_checkpoint(struct hashfile *f, struct hashfile_checkpoint *checkpoint)
{
	hashflush(f);
	checkpoint->offset = f->total;
	the_hash_algo->clone_fn(&checkpoint->ctx, &f->ctx);
}

int hashfile_truncate(struct hashfile *f, struct hashfile_checkpoint *checkpoint)
{
	off_t offset = checkpoint->offset;

	if (ftruncate(f->fd, offset) ||
	    lseek(f->fd, offset, SEEK_SET) != offset)
		return -1;
	f->total = offset;
	f->ctx = checkpoint->ctx;
	f->offset = 0; /* hashflush() was called in checkpoint */
	return 0;
}

void crc32_begin(struct hashfile *f)
{
	f->crc32 = crc32(0, NULL, 0);
	f->do_crc = 1;
}

uint32_t crc32_end(struct hashfile *f)
{
	f->do_crc = 0;
	return f->crc32;
}
