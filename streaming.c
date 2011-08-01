/*
 * Copyright (c) 2011, Google Inc.
 */
#include "cache.h"
#include "streaming.h"

enum input_source {
	stream_error = -1,
	incore = 0,
	loose = 1,
	pack_non_delta = 2
};

typedef int (*open_istream_fn)(struct git_istream *,
			       struct object_info *,
			       const unsigned char *,
			       enum object_type *);
typedef int (*close_istream_fn)(struct git_istream *);
typedef ssize_t (*read_istream_fn)(struct git_istream *, char *, size_t);

struct stream_vtbl {
	close_istream_fn close;
	read_istream_fn read;
};

#define open_method_decl(name) \
	int open_istream_ ##name \
	(struct git_istream *st, struct object_info *oi, \
	 const unsigned char *sha1, \
	 enum object_type *type)

#define close_method_decl(name) \
	int close_istream_ ##name \
	(struct git_istream *st)

#define read_method_decl(name) \
	ssize_t read_istream_ ##name \
	(struct git_istream *st, char *buf, size_t sz)

/* forward declaration */
static open_method_decl(incore);
static open_method_decl(loose);
static open_method_decl(pack_non_delta);
static struct git_istream *attach_stream_filter(struct git_istream *st,
						struct stream_filter *filter);


static open_istream_fn open_istream_tbl[] = {
	open_istream_incore,
	open_istream_loose,
	open_istream_pack_non_delta,
};

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
	const struct stream_vtbl *vtbl;
	unsigned long size; /* inflated size of full object */
	git_zstream z;
	enum { z_unused, z_used, z_done, z_error } z_state;

	union {
		struct {
			char *buf; /* from read_object() */
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

int close_istream(struct git_istream *st)
{
	int r = st->vtbl->close(st);
	free(st);
	return r;
}

ssize_t read_istream(struct git_istream *st, char *buf, size_t sz)
{
	return st->vtbl->read(st, buf, sz);
}

static enum input_source istream_source(const unsigned char *sha1,
					enum object_type *type,
					struct object_info *oi)
{
	unsigned long size;
	int status;

	oi->sizep = &size;
	status = sha1_object_info_extended(sha1, oi);
	if (status < 0)
		return stream_error;
	*type = status;

	switch (oi->whence) {
	case OI_LOOSE:
		return loose;
	case OI_PACKED:
		if (!oi->u.packed.is_delta && big_file_threshold <= size)
			return pack_non_delta;
		/* fallthru */
	default:
		return incore;
	}
}

struct git_istream *open_istream(const unsigned char *sha1,
				 enum object_type *type,
				 unsigned long *size,
				 struct stream_filter *filter)
{
	struct git_istream *st;
	struct object_info oi;
	const unsigned char *real = lookup_replace_object(sha1);
	enum input_source src = istream_source(real, type, &oi);

	if (src < 0)
		return NULL;

	st = xmalloc(sizeof(*st));
	if (open_istream_tbl[src](st, &oi, real, type)) {
		if (open_istream_incore(st, &oi, real, type)) {
			free(st);
			return NULL;
		}
	}
	if (st && filter) {
		/* Add "&& !is_null_stream_filter(filter)" for performance */
		struct git_istream *nst = attach_stream_filter(st, filter);
		if (!nst)
			close_istream(st);
		st = nst;
	}

	*size = st->size;
	return st;
}


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

static close_method_decl(filtered)
{
	free_stream_filter(st->u.filtered.filter);
	return close_istream(st->u.filtered.upstream);
}

static read_method_decl(filtered)
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
				break;
			if (fs->i_end)
				continue;
		}
		fs->input_finished = 1;
	}
	return filled;
}

static struct stream_vtbl filtered_vtbl = {
	close_istream_filtered,
	read_istream_filtered,
};

static struct git_istream *attach_stream_filter(struct git_istream *st,
						struct stream_filter *filter)
{
	struct git_istream *ifs = xmalloc(sizeof(*ifs));
	struct filtered_istream *fs = &(ifs->u.filtered);

	ifs->vtbl = &filtered_vtbl;
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

static read_method_decl(loose)
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
		if (status != Z_OK && status != Z_BUF_ERROR) {
			git_inflate_end(&st->z);
			st->z_state = z_error;
			return -1;
		}
	}
	return total_read;
}

static close_method_decl(loose)
{
	close_deflated_stream(st);
	munmap(st->u.loose.mapped, st->u.loose.mapsize);
	return 0;
}

static struct stream_vtbl loose_vtbl = {
	close_istream_loose,
	read_istream_loose,
};

static open_method_decl(loose)
{
	st->u.loose.mapped = map_sha1_file(sha1, &st->u.loose.mapsize);
	if (!st->u.loose.mapped)
		return -1;
	if (unpack_sha1_header(&st->z,
			       st->u.loose.mapped,
			       st->u.loose.mapsize,
			       st->u.loose.hdr,
			       sizeof(st->u.loose.hdr)) < 0) {
		git_inflate_end(&st->z);
		munmap(st->u.loose.mapped, st->u.loose.mapsize);
		return -1;
	}

	parse_sha1_header(st->u.loose.hdr, &st->size);
	st->u.loose.hdr_used = strlen(st->u.loose.hdr) + 1;
	st->u.loose.hdr_avail = st->z.total_out;
	st->z_state = z_used;

	st->vtbl = &loose_vtbl;
	return 0;
}


/*****************************************************************
 *
 * Non-delta packed object stream
 *
 *****************************************************************/

static read_method_decl(pack_non_delta)
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
		if (status != Z_OK && status != Z_BUF_ERROR) {
			git_inflate_end(&st->z);
			st->z_state = z_error;
			return -1;
		}
	}
	return total_read;
}

static close_method_decl(pack_non_delta)
{
	close_deflated_stream(st);
	return 0;
}

static struct stream_vtbl pack_non_delta_vtbl = {
	close_istream_pack_non_delta,
	read_istream_pack_non_delta,
};

static open_method_decl(pack_non_delta)
{
	struct pack_window *window;
	enum object_type in_pack_type;

	st->u.in_pack.pack = oi->u.packed.pack;
	st->u.in_pack.pos = oi->u.packed.offset;
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
	st->vtbl = &pack_non_delta_vtbl;
	return 0;
}


/*****************************************************************
 *
 * In-core stream
 *
 *****************************************************************/

static close_method_decl(incore)
{
	free(st->u.incore.buf);
	return 0;
}

static read_method_decl(incore)
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

static struct stream_vtbl incore_vtbl = {
	close_istream_incore,
	read_istream_incore,
};

static open_method_decl(incore)
{
	st->u.incore.buf = read_sha1_file_extended(sha1, type, &st->size, 0);
	st->u.incore.read_ptr = 0;
	st->vtbl = &incore_vtbl;

	return st->u.incore.buf ? 0 : -1;
}
