/*
 * Copyright (c) 2011, Google Inc.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "convert.h"
#include "environment.h"
#include "streaming.h"
#include "repository.h"
#include "object-file.h"
#include "odb.h"
#include "replace-object.h"
#include "packfile.h"

typedef int (*open_istream_fn)(struct git_istream *,
			       struct repository *,
			       const struct object_id *,
			       enum object_type *);
typedef int (*close_istream_fn)(struct git_istream *);
typedef ssize_t (*read_istream_fn)(struct git_istream *, char *, size_t);

#define FILTER_BUFFER (1024*16)

struct filtered_istream {
	struct git_istream *upstream;
	struct stream_filter *filter;
	char ibuf[FILTER_BUFFER];
	char obuf[FILTER_BUFFER];
	int i_end, i_ptr;
	int o_end, o_ptr;
	int input_finished;
};

struct git_istream {
	open_istream_fn open;
	close_istream_fn close;
	read_istream_fn read;

	unsigned long size; /* inflated size of full object */
	git_zstream z;
	enum { z_unused, z_used, z_done, z_error } z_state;

	union {
		struct {
			char *buf; /* from odb_read_object_info_extended() */
			unsigned long read_ptr;
		} incore;

		struct {
			void *mapped;
			unsigned long mapsize;
			char hdr[32];
			int hdr_avail;
			int hdr_used;
		} loose;

		struct {
			struct packed_git *pack;
			off_t pos;
		} in_pack;

		struct filtered_istream filtered;
	} u;
};

/*****************************************************************
 *
 * Common helpers
 *
 *****************************************************************/

static void close_deflated_stream(struct git_istream *st)
{
	if (st->z_state == z_used)
		git_inflate_end(&st->z);
}


/*****************************************************************
 *
 * Filtered stream
 *
 *****************************************************************/

static int close_istream_filtered(struct git_istream *st)
{
	free_stream_filter(st->u.filtered.filter);
	return close_istream(st->u.filtered.upstream);
}

static ssize_t read_istream_filtered(struct git_istream *st, char *buf,
				     size_t sz)
{
	struct filtered_istream *fs = &(st->u.filtered);
	size_t filled = 0;

	while (sz) {
		/* do we already have filtered output? */
		if (fs->o_ptr < fs->o_end) {
			size_t to_move = fs->o_end - fs->o_ptr;
			if (sz < to_move)
				to_move = sz;
			memcpy(buf + filled, fs->obuf + fs->o_ptr, to_move);
			fs->o_ptr += to_move;
			sz -= to_move;
			filled += to_move;
			continue;
		}
		fs->o_end = fs->o_ptr = 0;

		/* do we have anything to feed the filter with? */
		if (fs->i_ptr < fs->i_end) {
			size_t to_feed = fs->i_end - fs->i_ptr;
			size_t to_receive = FILTER_BUFFER;
			if (stream_filter(fs->filter,
					  fs->ibuf + fs->i_ptr, &to_feed,
					  fs->obuf, &to_receive))
				return -1;
			fs->i_ptr = fs->i_end - to_feed;
			fs->o_end = FILTER_BUFFER - to_receive;
			continue;
		}

		/* tell the filter to drain upon no more input */
		if (fs->input_finished) {
			size_t to_receive = FILTER_BUFFER;
			if (stream_filter(fs->filter,
					  NULL, NULL,
					  fs->obuf, &to_receive))
				return -1;
			fs->o_end = FILTER_BUFFER - to_receive;
			if (!fs->o_end)
				break;
			continue;
		}
		fs->i_end = fs->i_ptr = 0;

		/* refill the input from the upstream */
		if (!fs->input_finished) {
			fs->i_end = read_istream(fs->upstream, fs->ibuf, FILTER_BUFFER);
			if (fs->i_end < 0)
				return -1;
			if (fs->i_end)
				continue;
		}
		fs->input_finished = 1;
	}
	return filled;
}

static struct git_istream *attach_stream_filter(struct git_istream *st,
						struct stream_filter *filter)
{
	struct git_istream *ifs = xmalloc(sizeof(*ifs));
	struct filtered_istream *fs = &(ifs->u.filtered);

	ifs->close = close_istream_filtered;
	ifs->read = read_istream_filtered;
	fs->upstream = st;
	fs->filter = filter;
	fs->i_end = fs->i_ptr = 0;
	fs->o_end = fs->o_ptr = 0;
	fs->input_finished = 0;
	ifs->size = -1; /* unknown */
	return ifs;
}

/*****************************************************************
 *
 * Loose object stream
 *
 *****************************************************************/

static ssize_t read_istream_loose(struct git_istream *st, char *buf, size_t sz)
{
	size_t total_read = 0;

	switch (st->z_state) {
	case z_done:
		return 0;
	case z_error:
		return -1;
	default:
		break;
	}

	if (st->u.loose.hdr_used < st->u.loose.hdr_avail) {
		size_t to_copy = st->u.loose.hdr_avail - st->u.loose.hdr_used;
		if (sz < to_copy)
			to_copy = sz;
		memcpy(buf, st->u.loose.hdr + st->u.loose.hdr_used, to_copy);
		st->u.loose.hdr_used += to_copy;
		total_read += to_copy;
	}

	while (total_read < sz) {
		int status;

		st->z.next_out = (unsigned char *)buf + total_read;
		st->z.avail_out = sz - total_read;
		status = git_inflate(&st->z, Z_FINISH);

		total_read = st->z.next_out - (unsigned char *)buf;

		if (status == Z_STREAM_END) {
			git_inflate_end(&st->z);
			st->z_state = z_done;
			break;
		}
		if (status != Z_OK && (status != Z_BUF_ERROR || total_read < sz)) {
			git_inflate_end(&st->z);
			st->z_state = z_error;
			return -1;
		}
	}
	return total_read;
}

static int close_istream_loose(struct git_istream *st)
{
	close_deflated_stream(st);
	munmap(st->u.loose.mapped, st->u.loose.mapsize);
	return 0;
}

static int open_istream_loose(struct git_istream *st, struct repository *r,
			      const struct object_id *oid,
			      enum object_type *type)
{
	struct object_info oi = OBJECT_INFO_INIT;
	oi.sizep = &st->size;
	oi.typep = type;

	st->u.loose.mapped = map_loose_object(r, oid, &st->u.loose.mapsize);
	if (!st->u.loose.mapped)
		return -1;
	switch (unpack_loose_header(&st->z, st->u.loose.mapped,
				    st->u.loose.mapsize, st->u.loose.hdr,
				    sizeof(st->u.loose.hdr))) {
	case ULHR_OK:
		break;
	case ULHR_BAD:
	case ULHR_TOO_LONG:
		goto error;
	}
	if (parse_loose_header(st->u.loose.hdr, &oi) < 0 || *type < 0)
		goto error;

	st->u.loose.hdr_used = strlen(st->u.loose.hdr) + 1;
	st->u.loose.hdr_avail = st->z.total_out;
	st->z_state = z_used;
	st->close = close_istream_loose;
	st->read = read_istream_loose;

	return 0;
error:
	git_inflate_end(&st->z);
	munmap(st->u.loose.mapped, st->u.loose.mapsize);
	return -1;
}


/*****************************************************************
 *
 * Non-delta packed object stream
 *
 *****************************************************************/

static ssize_t read_istream_pack_non_delta(struct git_istream *st, char *buf,
					   size_t sz)
{
	size_t total_read = 0;

	switch (st->z_state) {
	case z_unused:
		memset(&st->z, 0, sizeof(st->z));
		git_inflate_init(&st->z);
		st->z_state = z_used;
		break;
	case z_done:
		return 0;
	case z_error:
		return -1;
	case z_used:
		break;
	}

	while (total_read < sz) {
		int status;
		struct pack_window *window = NULL;
		unsigned char *mapped;

		mapped = use_pack(st->u.in_pack.pack, &window,
				  st->u.in_pack.pos, &st->z.avail_in);

		st->z.next_out = (unsigned char *)buf + total_read;
		st->z.avail_out = sz - total_read;
		st->z.next_in = mapped;
		status = git_inflate(&st->z, Z_FINISH);

		st->u.in_pack.pos += st->z.next_in - mapped;
		total_read = st->z.next_out - (unsigned char *)buf;
		unuse_pack(&window);

		if (status == Z_STREAM_END) {
			git_inflate_end(&st->z);
			st->z_state = z_done;
			break;
		}

		/*
		 * Unlike the loose object case, we do not have to worry here
		 * about running out of input bytes and spinning infinitely. If
		 * we get Z_BUF_ERROR due to too few input bytes, then we'll
		 * replenish them in the next use_pack() call when we loop. If
		 * we truly hit the end of the pack (i.e., because it's corrupt
		 * or truncated), then use_pack() catches that and will die().
		 */
		if (status != Z_OK && status != Z_BUF_ERROR) {
			git_inflate_end(&st->z);
			st->z_state = z_error;
			return -1;
		}
	}
	return total_read;
}

static int close_istream_pack_non_delta(struct git_istream *st)
{
	close_deflated_stream(st);
	return 0;
}

static int open_istream_pack_non_delta(struct git_istream *st,
				       struct repository *r UNUSED,
				       const struct object_id *oid UNUSED,
				       enum object_type *type UNUSED)
{
	struct pack_window *window;
	enum object_type in_pack_type;

	window = NULL;

	in_pack_type = unpack_object_header(st->u.in_pack.pack,
					    &window,
					    &st->u.in_pack.pos,
					    &st->size);
	unuse_pack(&window);
	switch (in_pack_type) {
	default:
		return -1; /* we do not do deltas for now */
	case OBJ_COMMIT:
	case OBJ_TREE:
	case OBJ_BLOB:
	case OBJ_TAG:
		break;
	}
	st->z_state = z_unused;
	st->close = close_istream_pack_non_delta;
	st->read = read_istream_pack_non_delta;

	return 0;
}


/*****************************************************************
 *
 * In-core stream
 *
 *****************************************************************/

static int close_istream_incore(struct git_istream *st)
{
	free(st->u.incore.buf);
	return 0;
}

static ssize_t read_istream_incore(struct git_istream *st, char *buf, size_t sz)
{
	size_t read_size = sz;
	size_t remainder = st->size - st->u.incore.read_ptr;

	if (remainder <= read_size)
		read_size = remainder;
	if (read_size) {
		memcpy(buf, st->u.incore.buf + st->u.incore.read_ptr, read_size);
		st->u.incore.read_ptr += read_size;
	}
	return read_size;
}

static int open_istream_incore(struct git_istream *st, struct repository *r,
			       const struct object_id *oid, enum object_type *type)
{
	struct object_info oi = OBJECT_INFO_INIT;

	st->u.incore.read_ptr = 0;
	st->close = close_istream_incore;
	st->read = read_istream_incore;

	oi.typep = type;
	oi.sizep = &st->size;
	oi.contentp = (void **)&st->u.incore.buf;
	return odb_read_object_info_extended(r->objects, oid, &oi,
					     OBJECT_INFO_DIE_IF_CORRUPT);
}

/*****************************************************************************
 * static helpers variables and functions for users of streaming interface
 *****************************************************************************/

static int istream_source(struct git_istream *st,
			  struct repository *r,
			  const struct object_id *oid,
			  enum object_type *type)
{
	unsigned long size;
	int status;
	struct object_info oi = OBJECT_INFO_INIT;

	oi.typep = type;
	oi.sizep = &size;
	status = odb_read_object_info_extended(r->objects, oid, &oi, 0);
	if (status < 0)
		return status;

	switch (oi.whence) {
	case OI_LOOSE:
		st->open = open_istream_loose;
		return 0;
	case OI_PACKED:
		if (!oi.u.packed.is_delta &&
		    repo_settings_get_big_file_threshold(the_repository) < size) {
			st->u.in_pack.pack = oi.u.packed.pack;
			st->u.in_pack.pos = oi.u.packed.offset;
			st->open = open_istream_pack_non_delta;
			return 0;
		}
		/* fallthru */
	default:
		st->open = open_istream_incore;
		return 0;
	}
}

/****************************************************************
 * Users of streaming interface
 ****************************************************************/

int close_istream(struct git_istream *st)
{
	int r = st->close(st);
	free(st);
	return r;
}

ssize_t read_istream(struct git_istream *st, void *buf, size_t sz)
{
	return st->read(st, buf, sz);
}

struct git_istream *open_istream(struct repository *r,
				 const struct object_id *oid,
				 enum object_type *type,
				 unsigned long *size,
				 struct stream_filter *filter)
{
	struct git_istream *st = xmalloc(sizeof(*st));
	const struct object_id *real = lookup_replace_object(r, oid);
	int ret = istream_source(st, r, real, type);

	if (ret) {
		free(st);
		return NULL;
	}

	if (st->open(st, r, real, type)) {
		if (open_istream_incore(st, r, real, type)) {
			free(st);
			return NULL;
		}
	}
	if (filter) {
		/* Add "&& !is_null_stream_filter(filter)" for performance */
		struct git_istream *nst = attach_stream_filter(st, filter);
		if (!nst) {
			close_istream(st);
			return NULL;
		}
		st = nst;
	}

	*size = st->size;
	return st;
}

int stream_blob_to_fd(int fd, const struct object_id *oid, struct stream_filter *filter,
		      int can_seek)
{
	struct git_istream *st;
	enum object_type type;
	unsigned long sz;
	ssize_t kept = 0;
	int result = -1;

	st = open_istream(the_repository, oid, &type, &sz, filter);
	if (!st) {
		if (filter)
			free_stream_filter(filter);
		return result;
	}
	if (type != OBJ_BLOB)
		goto close_and_exit;
	for (;;) {
		char buf[1024 * 16];
		ssize_t wrote, holeto;
		ssize_t readlen = read_istream(st, buf, sizeof(buf));

		if (readlen < 0)
			goto close_and_exit;
		if (!readlen)
			break;
		if (can_seek && sizeof(buf) == readlen) {
			for (holeto = 0; holeto < readlen; holeto++)
				if (buf[holeto])
					break;
			if (readlen == holeto) {
				kept += holeto;
				continue;
			}
		}

		if (kept && lseek(fd, kept, SEEK_CUR) == (off_t) -1)
			goto close_and_exit;
		else
			kept = 0;
		wrote = write_in_full(fd, buf, readlen);

		if (wrote < 0)
			goto close_and_exit;
	}
	if (kept && (lseek(fd, kept - 1, SEEK_CUR) == (off_t) -1 ||
		     xwrite(fd, "", 1) != 1))
		goto close_and_exit;
	result = 0;

 close_and_exit:
	close_istream(st);
	return result;
}
