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

typedef int (*close_istream_fn)(struct odb_read_stream *);
typedef ssize_t (*read_istream_fn)(struct odb_read_stream *, char *, size_t);

#define FILTER_BUFFER (1024*16)

struct odb_read_stream {
	close_istream_fn close;
	read_istream_fn read;

	enum object_type type;
	unsigned long size; /* inflated size of full object */
	git_zstream z;
	enum { z_unused, z_used, z_done, z_error } z_state;
};

/*****************************************************************
 *
 * Common helpers
 *
 *****************************************************************/

static void close_deflated_stream(struct odb_read_stream *st)
{
	if (st->z_state == z_used)
		git_inflate_end(&st->z);
}


/*****************************************************************
 *
 * Filtered stream
 *
 *****************************************************************/

struct odb_filtered_read_stream {
	struct odb_read_stream base;
	struct odb_read_stream *upstream;
	struct stream_filter *filter;
	char ibuf[FILTER_BUFFER];
	char obuf[FILTER_BUFFER];
	int i_end, i_ptr;
	int o_end, o_ptr;
	int input_finished;
};

static int close_istream_filtered(struct odb_read_stream *_fs)
{
	struct odb_filtered_read_stream *fs = (struct odb_filtered_read_stream *)_fs;
	free_stream_filter(fs->filter);
	return close_istream(fs->upstream);
}

static ssize_t read_istream_filtered(struct odb_read_stream *_fs, char *buf,
				     size_t sz)
{
	struct odb_filtered_read_stream *fs = (struct odb_filtered_read_stream *)_fs;
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

static struct odb_read_stream *attach_stream_filter(struct odb_read_stream *st,
						    struct stream_filter *filter)
{
	struct odb_filtered_read_stream *fs;

	CALLOC_ARRAY(fs, 1);
	fs->base.close = close_istream_filtered;
	fs->base.read = read_istream_filtered;
	fs->upstream = st;
	fs->filter = filter;
	fs->base.size = -1; /* unknown */
	fs->base.type = st->type;

	return &fs->base;
}

/*****************************************************************
 *
 * Loose object stream
 *
 *****************************************************************/

struct odb_loose_read_stream {
	struct odb_read_stream base;
	void *mapped;
	unsigned long mapsize;
	char hdr[32];
	int hdr_avail;
	int hdr_used;
};

static ssize_t read_istream_loose(struct odb_read_stream *_st, char *buf, size_t sz)
{
	struct odb_loose_read_stream *st = (struct odb_loose_read_stream *)_st;
	size_t total_read = 0;

	switch (st->base.z_state) {
	case z_done:
		return 0;
	case z_error:
		return -1;
	default:
		break;
	}

	if (st->hdr_used < st->hdr_avail) {
		size_t to_copy = st->hdr_avail - st->hdr_used;
		if (sz < to_copy)
			to_copy = sz;
		memcpy(buf, st->hdr + st->hdr_used, to_copy);
		st->hdr_used += to_copy;
		total_read += to_copy;
	}

	while (total_read < sz) {
		int status;

		st->base.z.next_out = (unsigned char *)buf + total_read;
		st->base.z.avail_out = sz - total_read;
		status = git_inflate(&st->base.z, Z_FINISH);

		total_read = st->base.z.next_out - (unsigned char *)buf;

		if (status == Z_STREAM_END) {
			git_inflate_end(&st->base.z);
			st->base.z_state = z_done;
			break;
		}
		if (status != Z_OK && (status != Z_BUF_ERROR || total_read < sz)) {
			git_inflate_end(&st->base.z);
			st->base.z_state = z_error;
			return -1;
		}
	}
	return total_read;
}

static int close_istream_loose(struct odb_read_stream *_st)
{
	struct odb_loose_read_stream *st = (struct odb_loose_read_stream *)_st;
	close_deflated_stream(&st->base);
	munmap(st->mapped, st->mapsize);
	return 0;
}

static int open_istream_loose(struct odb_read_stream **out,
			      struct repository *r,
			      const struct object_id *oid)
{
	struct object_info oi = OBJECT_INFO_INIT;
	struct odb_loose_read_stream *st;
	struct odb_source *source;
	unsigned long mapsize;
	void *mapped;

	odb_prepare_alternates(r->objects);
	for (source = r->objects->sources; source; source = source->next) {
		mapped = odb_source_loose_map_object(source, oid, &mapsize);
		if (mapped)
			break;
	}
	if (!mapped)
		return -1;

	/*
	 * Note: we must allocate this structure early even though we may still
	 * fail. This is because we need to initialize the zlib stream, and it
	 * is not possible to copy the stream around after the fact because it
	 * has self-referencing pointers.
	 */
	CALLOC_ARRAY(st, 1);

	switch (unpack_loose_header(&st->base.z, mapped, mapsize, st->hdr,
				    sizeof(st->hdr))) {
	case ULHR_OK:
		break;
	case ULHR_BAD:
	case ULHR_TOO_LONG:
		goto error;
	}

	oi.sizep = &st->base.size;
	oi.typep = &st->base.type;

	if (parse_loose_header(st->hdr, &oi) < 0 || st->base.type < 0)
		goto error;

	st->mapped = mapped;
	st->mapsize = mapsize;
	st->hdr_used = strlen(st->hdr) + 1;
	st->hdr_avail = st->base.z.total_out;
	st->base.z_state = z_used;
	st->base.close = close_istream_loose;
	st->base.read = read_istream_loose;

	*out = &st->base;

	return 0;
error:
	git_inflate_end(&st->base.z);
	munmap(st->mapped, st->mapsize);
	free(st);
	return -1;
}


/*****************************************************************
 *
 * Non-delta packed object stream
 *
 *****************************************************************/

struct odb_packed_read_stream {
	struct odb_read_stream base;
	struct packed_git *pack;
	off_t pos;
};

static ssize_t read_istream_pack_non_delta(struct odb_read_stream *_st, char *buf,
					   size_t sz)
{
	struct odb_packed_read_stream *st = (struct odb_packed_read_stream *)_st;
	size_t total_read = 0;

	switch (st->base.z_state) {
	case z_unused:
		memset(&st->base.z, 0, sizeof(st->base.z));
		git_inflate_init(&st->base.z);
		st->base.z_state = z_used;
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

		mapped = use_pack(st->pack, &window,
				  st->pos, &st->base.z.avail_in);

		st->base.z.next_out = (unsigned char *)buf + total_read;
		st->base.z.avail_out = sz - total_read;
		st->base.z.next_in = mapped;
		status = git_inflate(&st->base.z, Z_FINISH);

		st->pos += st->base.z.next_in - mapped;
		total_read = st->base.z.next_out - (unsigned char *)buf;
		unuse_pack(&window);

		if (status == Z_STREAM_END) {
			git_inflate_end(&st->base.z);
			st->base.z_state = z_done;
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
			git_inflate_end(&st->base.z);
			st->base.z_state = z_error;
			return -1;
		}
	}
	return total_read;
}

static int close_istream_pack_non_delta(struct odb_read_stream *_st)
{
	struct odb_packed_read_stream *st = (struct odb_packed_read_stream *)_st;
	close_deflated_stream(&st->base);
	return 0;
}

static int open_istream_pack_non_delta(struct odb_read_stream **out,
				       struct repository *r UNUSED,
				       const struct object_id *oid UNUSED,
				       struct packed_git *pack,
				       off_t offset)
{
	struct odb_packed_read_stream *stream;
	struct pack_window *window;
	enum object_type in_pack_type;
	size_t size;

	window = NULL;

	in_pack_type = unpack_object_header(pack,
					    &window,
					    &offset,
					    &size);
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

	CALLOC_ARRAY(stream, 1);
	stream->base.close = close_istream_pack_non_delta;
	stream->base.read = read_istream_pack_non_delta;
	stream->base.type = in_pack_type;
	stream->base.size = size;
	stream->base.z_state = z_unused;
	stream->pack = pack;
	stream->pos = offset;

	*out = &stream->base;

	return 0;
}


/*****************************************************************
 *
 * In-core stream
 *
 *****************************************************************/

struct odb_incore_read_stream {
	struct odb_read_stream base;
	char *buf; /* from odb_read_object_info_extended() */
	unsigned long read_ptr;
};

static int close_istream_incore(struct odb_read_stream *_st)
{
	struct odb_incore_read_stream *st = (struct odb_incore_read_stream *)_st;
	free(st->buf);
	return 0;
}

static ssize_t read_istream_incore(struct odb_read_stream *_st, char *buf, size_t sz)
{
	struct odb_incore_read_stream *st = (struct odb_incore_read_stream *)_st;
	size_t read_size = sz;
	size_t remainder = st->base.size - st->read_ptr;

	if (remainder <= read_size)
		read_size = remainder;
	if (read_size) {
		memcpy(buf, st->buf + st->read_ptr, read_size);
		st->read_ptr += read_size;
	}
	return read_size;
}

static int open_istream_incore(struct odb_read_stream **out,
			       struct repository *r,
			       const struct object_id *oid)
{
	struct object_info oi = OBJECT_INFO_INIT;
	struct odb_incore_read_stream stream = {
		.base.close = close_istream_incore,
		.base.read = read_istream_incore,
	};
	struct odb_incore_read_stream *st;
	int ret;

	oi.typep = &stream.base.type;
	oi.sizep = &stream.base.size;
	oi.contentp = (void **)&stream.buf;
	ret = odb_read_object_info_extended(r->objects, oid, &oi,
					    OBJECT_INFO_DIE_IF_CORRUPT);
	if (ret)
		return ret;

	CALLOC_ARRAY(st, 1);
	*st = stream;
	*out = &st->base;

	return 0;
}

/*****************************************************************************
 * static helpers variables and functions for users of streaming interface
 *****************************************************************************/

static int istream_source(struct odb_read_stream **out,
			  struct repository *r,
			  const struct object_id *oid)
{
	unsigned long size;
	int status;
	struct object_info oi = OBJECT_INFO_INIT;

	oi.sizep = &size;
	status = odb_read_object_info_extended(r->objects, oid, &oi, 0);
	if (status < 0)
		return status;

	switch (oi.whence) {
	case OI_LOOSE:
		if (open_istream_loose(out, r, oid) < 0)
			break;
		return 0;
	case OI_PACKED:
		if (oi.u.packed.is_delta ||
		    repo_settings_get_big_file_threshold(the_repository) >= size ||
		    open_istream_pack_non_delta(out, r, oid, oi.u.packed.pack,
						oi.u.packed.offset) < 0)
			break;
		return 0;
	default:
		break;
	}

	return open_istream_incore(out, r, oid);
}

/****************************************************************
 * Users of streaming interface
 ****************************************************************/

int close_istream(struct odb_read_stream *st)
{
	int r = st->close(st);
	free(st);
	return r;
}

ssize_t read_istream(struct odb_read_stream *st, void *buf, size_t sz)
{
	return st->read(st, buf, sz);
}

struct odb_read_stream *open_istream(struct repository *r,
				     const struct object_id *oid,
				     enum object_type *type,
				     unsigned long *size,
				     struct stream_filter *filter)
{
	struct odb_read_stream *st;
	const struct object_id *real = lookup_replace_object(r, oid);
	int ret = istream_source(&st, r, real);

	if (ret)
		return NULL;

	if (filter) {
		/* Add "&& !is_null_stream_filter(filter)" for performance */
		struct odb_read_stream *nst = attach_stream_filter(st, filter);
		if (!nst) {
			close_istream(st);
			return NULL;
		}
		st = nst;
	}

	*size = st->size;
	*type = st->type;
	return st;
}

int stream_blob_to_fd(int fd, const struct object_id *oid, struct stream_filter *filter,
		      int can_seek)
{
	struct odb_read_stream *st;
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
