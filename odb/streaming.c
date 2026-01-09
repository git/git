/*
 * Copyright (c) 2011, Google Inc.
 */

#include "git-compat-util.h"
#include "convert.h"
#include "environment.h"
#include "repository.h"
#include "object-file.h"
#include "odb.h"
#include "odb/streaming.h"
#include "replace-object.h"
#include "packfile.h"

#define FILTER_BUFFER (1024*16)

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
	return odb_read_stream_close(fs->upstream);
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
			fs->i_end = odb_read_stream_read(fs->upstream, fs->ibuf, FILTER_BUFFER);
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
			       struct object_database *odb,
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
	ret = odb_read_object_info_extended(odb, oid, &oi,
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
			  struct object_database *odb,
			  const struct object_id *oid)
{
	struct odb_source *source;

	if (!packfile_store_read_object_stream(out, odb->packfiles, oid))
		return 0;

	odb_prepare_alternates(odb);
	for (source = odb->sources; source; source = source->next)
		if (!odb_source_loose_read_object_stream(out, source, oid))
			return 0;

	return open_istream_incore(out, odb, oid);
}

/****************************************************************
 * Users of streaming interface
 ****************************************************************/

int odb_read_stream_close(struct odb_read_stream *st)
{
	int r = st->close(st);
	free(st);
	return r;
}

ssize_t odb_read_stream_read(struct odb_read_stream *st, void *buf, size_t sz)
{
	return st->read(st, buf, sz);
}

struct odb_read_stream *odb_read_stream_open(struct object_database *odb,
					     const struct object_id *oid,
					     struct stream_filter *filter)
{
	struct odb_read_stream *st;
	const struct object_id *real = lookup_replace_object(odb->repo, oid);
	int ret = istream_source(&st, odb, real);

	if (ret)
		return NULL;

	if (filter) {
		/* Add "&& !is_null_stream_filter(filter)" for performance */
		struct odb_read_stream *nst = attach_stream_filter(st, filter);
		if (!nst) {
			odb_read_stream_close(st);
			return NULL;
		}
		st = nst;
	}

	return st;
}

int odb_stream_blob_to_fd(struct object_database *odb,
			  int fd,
			  const struct object_id *oid,
			  struct stream_filter *filter,
			  int can_seek)
{
	struct odb_read_stream *st;
	ssize_t kept = 0;
	int result = -1;

	st = odb_read_stream_open(odb, oid, filter);
	if (!st) {
		if (filter)
			free_stream_filter(filter);
		return result;
	}
	if (st->type != OBJ_BLOB)
		goto close_and_exit;
	for (;;) {
		char buf[1024 * 16];
		ssize_t wrote, holeto;
		ssize_t readlen = odb_read_stream_read(st, buf, sizeof(buf));

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
	odb_read_stream_close(st);
	return result;
}
