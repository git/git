/*
 * csum-file.c
 *
 * Copyright (C) 2005 Linus Torvalds
 *
 * Simple file write infrastructure for writing SHA1-summed
 * files. Useful when you write a file that you want to be
 * able to verify hasn't been messed with afterwards.
 */
#include "git-compat-util.h"
#include "progress.h"
#include "csum-file.h"
#include "wrapper.h"

static void verify_buffer_or_die(struct hashfile *f,
				 const void *buf,
				 unsigned int count)
{
	ssize_t ret = read_in_full(f->check_fd, f->check_buffer, count);

	if (ret < 0)
		die_errno("%s: sha1 file read error", f->name);
	if (ret != count)
		die("%s: sha1 file truncated", f->name);
	if (memcmp(buf, f->check_buffer, count))
		die("sha1 file '%s' validation error", f->name);
}

static void flush(struct hashfile *f, const void *buf, unsigned int count)
{
	if (0 <= f->check_fd && count)
		verify_buffer_or_die(f, buf, count);

	if (write_in_full(f->fd, buf, count) < 0) {
		if (errno == ENOSPC)
			die("sha1 file '%s' write error. Out of diskspace", f->name);
		die_errno("sha1 file '%s' write error", f->name);
	}

	f->total += count;
	display_throughput(f->tp, f->total);
}

void hashflush(struct hashfile *f)
{
	unsigned offset = f->offset;

	if (offset) {
		if (!f->skip_hash)
			the_hash_algo->update_fn(&f->ctx, f->buffer, offset);
		flush(f, f->buffer, offset);
		f->offset = 0;
	}
}

static void free_hashfile(struct hashfile *f)
{
	free(f->buffer);
	free(f->check_buffer);
	free(f);
}

int finalize_hashfile(struct hashfile *f, unsigned char *result,
		      enum fsync_component component, unsigned int flags)
{
	int fd;

	hashflush(f);

	if (f->skip_hash)
		hashclr(f->buffer);
	else
		the_hash_algo->final_fn(f->buffer, &f->ctx);

	if (result)
		hashcpy(result, f->buffer);
	if (flags & CSUM_HASH_IN_STREAM)
		flush(f, f->buffer, the_hash_algo->rawsz);
	if (flags & CSUM_FSYNC)
		fsync_component_or_die(component, f->fd, f->name);
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
	free_hashfile(f);
	return fd;
}

void hashwrite(struct hashfile *f, const void *buf, unsigned int count)
{
	while (count) {
		unsigned left = f->buffer_len - f->offset;
		unsigned nr = count > left ? left : count;

		if (f->do_crc)
			f->crc32 = crc32(f->crc32, buf, nr);

		if (nr == f->buffer_len) {
			/*
			 * Flush a full batch worth of data directly
			 * from the input, skipping the memcpy() to
			 * the hashfile's buffer. In this block,
			 * f->offset is necessarily zero.
			 */
			if (!f->skip_hash)
				the_hash_algo->update_fn(&f->ctx, buf, nr);
			flush(f, buf, nr);
		} else {
			/*
			 * Copy to the hashfile's buffer, flushing only
			 * if it became full.
			 */
			memcpy(f->buffer + f->offset, buf, nr);
			f->offset += nr;
			left -= nr;
			if (!left)
				hashflush(f);
		}

		count -= nr;
		buf = (char *) buf + nr;
	}
}

struct hashfile *hashfd_check(const char *name)
{
	int sink, check;
	struct hashfile *f;

	sink = xopen("/dev/null", O_WRONLY);
	check = xopen(name, O_RDONLY);
	f = hashfd(sink, name);
	f->check_fd = check;
	f->check_buffer = xmalloc(f->buffer_len);

	return f;
}

static struct hashfile *hashfd_internal(int fd, const char *name,
					struct progress *tp,
					size_t buffer_len)
{
	struct hashfile *f = xmalloc(sizeof(*f));
	f->fd = fd;
	f->check_fd = -1;
	f->offset = 0;
	f->total = 0;
	f->tp = tp;
	f->name = name;
	f->do_crc = 0;
	f->skip_hash = 0;
	the_hash_algo->init_fn(&f->ctx);

	f->buffer_len = buffer_len;
	f->buffer = xmalloc(buffer_len);
	f->check_buffer = NULL;

	return f;
}

struct hashfile *hashfd(int fd, const char *name)
{
	/*
	 * Since we are not going to use a progress meter to
	 * measure the rate of data passing through this hashfile,
	 * use a larger buffer size to reduce fsync() calls.
	 */
	return hashfd_internal(fd, name, NULL, 128 * 1024);
}

struct hashfile *hashfd_throughput(int fd, const char *name, struct progress *tp)
{
	/*
	 * Since we are expecting to report progress of the
	 * write into this hashfile, use a smaller buffer
	 * size so the progress indicators arrive at a more
	 * frequent rate.
	 */
	return hashfd_internal(fd, name, tp, 8 * 1024);
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

int hashfile_checksum_valid(const unsigned char *data, size_t total_len)
{
	unsigned char got[GIT_MAX_RAWSZ];
	git_hash_ctx ctx;
	size_t data_len = total_len - the_hash_algo->rawsz;

	if (total_len < the_hash_algo->rawsz)
		return 0; /* say "too short"? */

	the_hash_algo->init_fn(&ctx);
	the_hash_algo->update_fn(&ctx, data, data_len);
	the_hash_algo->final_fn(got, &ctx);

	return hasheq(got, data + data_len);
}
