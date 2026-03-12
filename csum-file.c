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
#include "csum-file.h"
#include "git-zlib.h"
#include "hash.h"
#include "progress.h"

static void verify_buffer_or_die(struct hashfile *f,
				 const void *buf,
				 unsigned int count)
{
	ssize_t ret = read_in_full(f->check_fd, f->check_buffer, count);

	if (ret < 0)
		die_errno("%s: sha1 file read error", f->name);
	if ((size_t)ret != count)
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
			git_hash_update(&f->ctx, f->buffer, offset);
		flush(f, f->buffer, offset);
		f->offset = 0;
	}
}

void free_hashfile(struct hashfile *f)
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
		hashclr(f->buffer, f->algop);
	else
		git_hash_final(f->buffer, &f->ctx);

	if (result)
		hashcpy(result, f->buffer, f->algop);
	if (flags & CSUM_HASH_IN_STREAM)
		flush(f, f->buffer, f->algop->rawsz);
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

void discard_hashfile(struct hashfile *f)
{
	if (0 <= f->check_fd)
		close(f->check_fd);
	if (0 <= f->fd)
		close(f->fd);
	free_hashfile(f);
}

void hashwrite(struct hashfile *f, const void *buf, uint32_t count)
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
				git_hash_update(&f->ctx, buf, nr);
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

struct hashfile *hashfd_check(const struct git_hash_algo *algop,
			      const char *name)
{
	int sink, check;
	struct hashfile *f;

	sink = xopen("/dev/null", O_WRONLY);
	check = xopen(name, O_RDONLY);
	f = hashfd(algop, sink, name);
	f->check_fd = check;
	f->check_buffer = xmalloc(f->buffer_len);

	return f;
}

static struct hashfile *hashfd_internal(const struct git_hash_algo *algop,
					int fd, const char *name,
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

	f->algop = unsafe_hash_algo(algop);
	f->algop->init_fn(&f->ctx);

	f->buffer_len = buffer_len;
	f->buffer = xmalloc(buffer_len);
	f->check_buffer = NULL;

	return f;
}

struct hashfile *hashfd(const struct git_hash_algo *algop,
			int fd, const char *name)
{
	/*
	 * Since we are not going to use a progress meter to
	 * measure the rate of data passing through this hashfile,
	 * use a larger buffer size to reduce fsync() calls.
	 */
	return hashfd_internal(algop, fd, name, NULL, 128 * 1024);
}

struct hashfile *hashfd_throughput(const struct git_hash_algo *algop,
				   int fd, const char *name, struct progress *tp)
{
	/*
	 * Since we are expecting to report progress of the
	 * write into this hashfile, use a smaller buffer
	 * size so the progress indicators arrive at a more
	 * frequent rate.
	 */
	return hashfd_internal(algop, fd, name, tp, 8 * 1024);
}

void hashfile_checkpoint_init(struct hashfile *f,
			      struct hashfile_checkpoint *checkpoint)
{
	memset(checkpoint, 0, sizeof(*checkpoint));
	f->algop->init_fn(&checkpoint->ctx);
}

void hashfile_checkpoint(struct hashfile *f, struct hashfile_checkpoint *checkpoint)
{
	hashflush(f);
	checkpoint->offset = f->total;
	git_hash_clone(&checkpoint->ctx, &f->ctx);
}

int hashfile_truncate(struct hashfile *f, struct hashfile_checkpoint *checkpoint)
{
	off_t offset = checkpoint->offset;

	if (ftruncate(f->fd, offset) ||
	    lseek(f->fd, offset, SEEK_SET) != offset)
		return -1;
	f->total = offset;
	git_hash_clone(&f->ctx, &checkpoint->ctx);
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

int hashfile_checksum_valid(const struct git_hash_algo *algop,
			    const unsigned char *data, size_t total_len)
{
	unsigned char got[GIT_MAX_RAWSZ];
	struct git_hash_ctx ctx;
	size_t data_len = total_len - algop->rawsz;

	algop = unsafe_hash_algo(algop);

	if (total_len < algop->rawsz)
		return 0; /* say "too short"? */

	algop->init_fn(&ctx);
	git_hash_update(&ctx, data, data_len);
	git_hash_final(got, &ctx);

	return hasheq(got, data + data_len, algop);
}
