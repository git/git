#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "config.h"
#include "attr.h"
#include "object.h"
#include "commit.h"
#include "tag.h"
#include "delta.h"
#include "pack.h"
#include "pack-revindex.h"
#include "csum-file.h"
#include "tree-walk.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "list-objects-filter-options.h"
#include "pack-objects.h"
#include "progress.h"
#include "refs.h"
#include "streaming.h"
#include "thread-utils.h"
#include "pack-bitmap.h"
#include "delta-islands.h"
#include "reachable.h"
#include "oid-array.h"
#include "strvec.h"
#include "list.h"
#include "packfile.h"
#include "object-file.h"
#include "object-store-ll.h"
#include "replace-object.h"
#include "dir.h"
#include "midx.h"
#include "trace2.h"
#include "shallow.h"
#include "promisor-remote.h"
#include "pack-mtimes.h"
#include "parse-options.h"
#include "blob.h"
#include "tree.h"
#include "path-walk.h"

/*
 * Objects we are going to pack are collected in the `to_pack` structure.
 * It contains an array (dynamically expanded) of the object data, and a map
 * that can resolve SHA1s to their position in the array.
 */
static struct packing_data to_pack;

static inline struct object_entry *oe_delta(
		const struct packing_data *pack,
		const struct object_entry *e)
{
	if (!e->delta_idx)
		return NULL;
	if (e->ext_base)
		return &pack->ext_bases[e->delta_idx - 1];
	else
		return &pack->objects[e->delta_idx - 1];
}

static inline unsigned long oe_delta_size(struct packing_data *pack,
					  const struct object_entry *e)
{
	if (e->delta_size_valid)
		return e->delta_size_;

	/*
	 * pack->delta_size[] can't be NULL because oe_set_delta_size()
	 * must have been called when a new delta is saved with
	 * oe_set_delta().
	 * If oe_delta() returns NULL (i.e. default state, which means
	 * delta_size_valid is also false), then the caller must never
	 * call oe_delta_size().
	 */
	return pack->delta_size[e - pack->objects];
}

unsigned long oe_get_size_slow(struct packing_data *pack,
			       const struct object_entry *e);

static inline unsigned long oe_size(struct packing_data *pack,
				    const struct object_entry *e)
{
	if (e->size_valid)
		return e->size_;

	return oe_get_size_slow(pack, e);
}

static inline void oe_set_delta(struct packing_data *pack,
				struct object_entry *e,
				struct object_entry *delta)
{
	if (delta)
		e->delta_idx = (delta - pack->objects) + 1;
	else
		e->delta_idx = 0;
}

static inline struct object_entry *oe_delta_sibling(
		const struct packing_data *pack,
		const struct object_entry *e)
{
	if (e->delta_sibling_idx)
		return &pack->objects[e->delta_sibling_idx - 1];
	return NULL;
}

static inline struct object_entry *oe_delta_child(
		const struct packing_data *pack,
		const struct object_entry *e)
{
	if (e->delta_child_idx)
		return &pack->objects[e->delta_child_idx - 1];
	return NULL;
}

static inline void oe_set_delta_child(struct packing_data *pack,
				      struct object_entry *e,
				      struct object_entry *delta)
{
	if (delta)
		e->delta_child_idx = (delta - pack->objects) + 1;
	else
		e->delta_child_idx = 0;
}

static inline void oe_set_delta_sibling(struct packing_data *pack,
					struct object_entry *e,
					struct object_entry *delta)
{
	if (delta)
		e->delta_sibling_idx = (delta - pack->objects) + 1;
	else
		e->delta_sibling_idx = 0;
}

static inline void oe_set_size(struct packing_data *pack,
			       struct object_entry *e,
			       unsigned long size)
{
	if (size < pack->oe_size_limit) {
		e->size_ = size;
		e->size_valid = 1;
	} else {
		e->size_valid = 0;
		if (oe_get_size_slow(pack, e) != size)
			BUG("'size' is supposed to be the object size!");
	}
}

static inline void oe_set_delta_size(struct packing_data *pack,
				     struct object_entry *e,
				     unsigned long size)
{
	if (size < pack->oe_delta_size_limit) {
		e->delta_size_ = size;
		e->delta_size_valid = 1;
	} else {
		packing_data_lock(pack);
		if (!pack->delta_size)
			ALLOC_ARRAY(pack->delta_size, pack->nr_alloc);
		packing_data_unlock(pack);

		pack->delta_size[e - pack->objects] = size;
		e->delta_size_valid = 0;
	}
}

#define IN_PACK(obj) oe_in_pack(&to_pack, obj)
#define SIZE(obj) oe_size(&to_pack, obj)
#define SET_SIZE(obj,size) oe_set_size(&to_pack, obj, size)
#define DELTA_SIZE(obj) oe_delta_size(&to_pack, obj)
#define DELTA(obj) oe_delta(&to_pack, obj)
#define DELTA_CHILD(obj) oe_delta_child(&to_pack, obj)
#define DELTA_SIBLING(obj) oe_delta_sibling(&to_pack, obj)
#define SET_DELTA(obj, val) oe_set_delta(&to_pack, obj, val)
#define SET_DELTA_EXT(obj, oid) oe_set_delta_ext(&to_pack, obj, oid)
#define SET_DELTA_SIZE(obj, val) oe_set_delta_size(&to_pack, obj, val)
#define SET_DELTA_CHILD(obj, val) oe_set_delta_child(&to_pack, obj, val)
#define SET_DELTA_SIBLING(obj, val) oe_set_delta_sibling(&to_pack, obj, val)

static const char *pack_usage[] = {
	N_("git pack-objects --stdout [<options>] [< <ref-list> | < <object-list>]"),
	N_("git pack-objects [<options>] <base-name> [< <ref-list> | < <object-list>]"),
	NULL
};

static struct pack_idx_entry **written_list;
static uint32_t nr_result, nr_written, nr_seen;
static struct bitmap_index *bitmap_git;
static uint32_t write_layer;

static int non_empty;
static int reuse_delta = 1, reuse_object = 1;
static int keep_unreachable, unpack_unreachable, include_tag;
static timestamp_t unpack_unreachable_expiration;
static int pack_loose_unreachable;
static int cruft;
static timestamp_t cruft_expiration;
static int local;
static int have_non_local_packs;
static int incremental;
static int ignore_packed_keep_on_disk;
static int ignore_packed_keep_in_core;
static int allow_ofs_delta;
static struct pack_idx_option pack_idx_opts;
static const char *base_name;
static int progress = 1;
static int window = 10;
static unsigned long pack_size_limit;
static int depth = 50;
static int delta_search_threads;
static int pack_to_stdout;
static int sparse;
static int thin;
static int path_walk = -1;
static int num_preferred_base;
static struct progress *progress_state;

static struct bitmapped_pack *reuse_packfiles;
static size_t reuse_packfiles_nr;
static size_t reuse_packfiles_used_nr;
static uint32_t reuse_packfile_objects;
static struct bitmap *reuse_packfile_bitmap;

static int use_bitmap_index_default = 1;
static int use_bitmap_index = -1;
static enum {
	NO_PACK_REUSE = 0,
	SINGLE_PACK_REUSE,
	MULTI_PACK_REUSE,
} allow_pack_reuse = SINGLE_PACK_REUSE;
static enum {
	WRITE_BITMAP_FALSE = 0,
	WRITE_BITMAP_QUIET,
	WRITE_BITMAP_TRUE,
} write_bitmap_index;
static uint16_t write_bitmap_options = BITMAP_OPT_HASH_CACHE;

static int exclude_promisor_objects;

static int use_delta_islands;

static unsigned long delta_cache_size = 0;
static unsigned long max_delta_cache_size = DEFAULT_DELTA_CACHE_SIZE;
static unsigned long cache_max_small_delta_size = 1000;

static unsigned long window_memory_limit = 0;

static struct string_list uri_protocols = STRING_LIST_INIT_NODUP;

enum missing_action {
	MA_ERROR = 0,      /* fail if any missing objects are encountered */
	MA_ALLOW_ANY,      /* silently allow ALL missing objects */
	MA_ALLOW_PROMISOR, /* silently allow all missing PROMISOR objects */
};
static enum missing_action arg_missing_action;
static show_object_fn fn_show_object;

struct configured_exclusion {
	struct oidmap_entry e;
	char *pack_hash_hex;
	char *uri;
};
static struct oidmap configured_exclusions;

static struct oidset excluded_by_config;
static int use_full_name_hash = -1;

static inline uint32_t pack_name_hash_fn(const char *name)
{
	if (use_full_name_hash)
		return pack_full_name_hash(name);
	return pack_name_hash(name);
}

/*
 * stats
 */
static uint32_t written, written_delta;
static uint32_t reused, reused_delta;

/*
 * Indexed commits
 */
static struct commit **indexed_commits;
static unsigned int indexed_commits_nr;
static unsigned int indexed_commits_alloc;

static void index_commit_for_bitmap(struct commit *commit)
{
	if (indexed_commits_nr >= indexed_commits_alloc) {
		indexed_commits_alloc = (indexed_commits_alloc + 32) * 2;
		REALLOC_ARRAY(indexed_commits, indexed_commits_alloc);
	}

	indexed_commits[indexed_commits_nr++] = commit;
}

static void *get_delta(struct object_entry *entry)
{
	unsigned long size, base_size, delta_size;
	void *buf, *base_buf, *delta_buf;
	enum object_type type;

	buf = repo_read_object_file(the_repository, &entry->idx.oid, &type,
				    &size);
	if (!buf)
		die(_("unable to read %s"), oid_to_hex(&entry->idx.oid));
	base_buf = repo_read_object_file(the_repository,
					 &DELTA(entry)->idx.oid, &type,
					 &base_size);
	if (!base_buf)
		die("unable to read %s",
		    oid_to_hex(&DELTA(entry)->idx.oid));
	delta_buf = diff_delta(base_buf, base_size,
			       buf, size, &delta_size, 0);
	/*
	 * We successfully computed this delta once but dropped it for
	 * memory reasons. Something is very wrong if this time we
	 * recompute and create a different delta.
	 */
	if (!delta_buf || delta_size != DELTA_SIZE(entry))
		BUG("delta size changed");
	free(buf);
	free(base_buf);
	return delta_buf;
}

static unsigned long do_compress(void **pptr, unsigned long size)
{
	git_zstream stream;
	void *in, *out;
	unsigned long maxsize;

	git_deflate_init(&stream, pack_compression_level);
	maxsize = git_deflate_bound(&stream, size);

	in = *pptr;
	out = xmalloc(maxsize);
	*pptr = out;

	stream.next_in = in;
	stream.avail_in = size;
	stream.next_out = out;
	stream.avail_out = maxsize;
	while (git_deflate(&stream, Z_FINISH) == Z_OK)
		; /* nothing */
	git_deflate_end(&stream);

	free(in);
	return stream.total_out;
}

static unsigned long write_large_blob_data(struct git_istream *st, struct hashfile *f,
					   const struct object_id *oid)
{
	git_zstream stream;
	unsigned char ibuf[1024 * 16];
	unsigned char obuf[1024 * 16];
	unsigned long olen = 0;

	git_deflate_init(&stream, pack_compression_level);

	for (;;) {
		ssize_t readlen;
		int zret = Z_OK;
		readlen = read_istream(st, ibuf, sizeof(ibuf));
		if (readlen == -1)
			die(_("unable to read %s"), oid_to_hex(oid));

		stream.next_in = ibuf;
		stream.avail_in = readlen;
		while ((stream.avail_in || readlen == 0) &&
		       (zret == Z_OK || zret == Z_BUF_ERROR)) {
			stream.next_out = obuf;
			stream.avail_out = sizeof(obuf);
			zret = git_deflate(&stream, readlen ? 0 : Z_FINISH);
			hashwrite(f, obuf, stream.next_out - obuf);
			olen += stream.next_out - obuf;
		}
		if (stream.avail_in)
			die(_("deflate error (%d)"), zret);
		if (readlen == 0) {
			if (zret != Z_STREAM_END)
				die(_("deflate error (%d)"), zret);
			break;
		}
	}
	git_deflate_end(&stream);
	return olen;
}

/*
 * we are going to reuse the existing object data as is.  make
 * sure it is not corrupt.
 */
static int check_pack_inflate(struct packed_git *p,
		struct pack_window **w_curs,
		off_t offset,
		off_t len,
		unsigned long expect)
{
	git_zstream stream;
	unsigned char fakebuf[4096], *in;
	int st;

	memset(&stream, 0, sizeof(stream));
	git_inflate_init(&stream);
	do {
		in = use_pack(p, w_curs, offset, &stream.avail_in);
		stream.next_in = in;
		stream.next_out = fakebuf;
		stream.avail_out = sizeof(fakebuf);
		st = git_inflate(&stream, Z_FINISH);
		offset += stream.next_in - in;
	} while (st == Z_OK || st == Z_BUF_ERROR);
	git_inflate_end(&stream);
	return (st == Z_STREAM_END &&
		stream.total_out == expect &&
		stream.total_in == len) ? 0 : -1;
}

static void copy_pack_data(struct hashfile *f,
		struct packed_git *p,
		struct pack_window **w_curs,
		off_t offset,
		off_t len)
{
	unsigned char *in;
	unsigned long avail;

	while (len) {
		in = use_pack(p, w_curs, offset, &avail);
		if (avail > len)
			avail = (unsigned long)len;
		hashwrite(f, in, avail);
		offset += avail;
		len -= avail;
	}
}

static inline int oe_size_greater_than(struct packing_data *pack,
				       const struct object_entry *lhs,
				       unsigned long rhs)
{
	if (lhs->size_valid)
		return lhs->size_ > rhs;
	if (rhs < pack->oe_size_limit) /* rhs < 2^x <= lhs ? */
		return 1;
	return oe_get_size_slow(pack, lhs) > rhs;
}

/* Return 0 if we will bust the pack-size limit */
static unsigned long write_no_reuse_object(struct hashfile *f, struct object_entry *entry,
					   unsigned long limit, int usable_delta)
{
	unsigned long size, datalen;
	unsigned char header[MAX_PACK_OBJECT_HEADER],
		      dheader[MAX_PACK_OBJECT_HEADER];
	unsigned hdrlen;
	enum object_type type;
	void *buf;
	struct git_istream *st = NULL;
	const unsigned hashsz = the_hash_algo->rawsz;

	if (!usable_delta) {
		if (oe_type(entry) == OBJ_BLOB &&
		    oe_size_greater_than(&to_pack, entry, big_file_threshold) &&
		    (st = open_istream(the_repository, &entry->idx.oid, &type,
				       &size, NULL)) != NULL)
			buf = NULL;
		else {
			buf = repo_read_object_file(the_repository,
						    &entry->idx.oid, &type,
						    &size);
			if (!buf)
				die(_("unable to read %s"),
				    oid_to_hex(&entry->idx.oid));
		}
		/*
		 * make sure no cached delta data remains from a
		 * previous attempt before a pack split occurred.
		 */
		FREE_AND_NULL(entry->delta_data);
		entry->z_delta_size = 0;
	} else if (entry->delta_data) {
		size = DELTA_SIZE(entry);
		buf = entry->delta_data;
		entry->delta_data = NULL;
		type = (allow_ofs_delta && DELTA(entry)->idx.offset) ?
			OBJ_OFS_DELTA : OBJ_REF_DELTA;
	} else {
		buf = get_delta(entry);
		size = DELTA_SIZE(entry);
		type = (allow_ofs_delta && DELTA(entry)->idx.offset) ?
			OBJ_OFS_DELTA : OBJ_REF_DELTA;
	}

	if (st)	/* large blob case, just assume we don't compress well */
		datalen = size;
	else if (entry->z_delta_size)
		datalen = entry->z_delta_size;
	else
		datalen = do_compress(&buf, size);

	/*
	 * The object header is a byte of 'type' followed by zero or
	 * more bytes of length.
	 */
	hdrlen = encode_in_pack_object_header(header, sizeof(header),
					      type, size);

	if (type == OBJ_OFS_DELTA) {
		/*
		 * Deltas with relative base contain an additional
		 * encoding of the relative offset for the delta
		 * base from this object's position in the pack.
		 */
		off_t ofs = entry->idx.offset - DELTA(entry)->idx.offset;
		unsigned pos = sizeof(dheader) - 1;
		dheader[pos] = ofs & 127;
		while (ofs >>= 7)
			dheader[--pos] = 128 | (--ofs & 127);
		if (limit && hdrlen + sizeof(dheader) - pos + datalen + hashsz >= limit) {
			if (st)
				close_istream(st);
			free(buf);
			return 0;
		}
		hashwrite(f, header, hdrlen);
		hashwrite(f, dheader + pos, sizeof(dheader) - pos);
		hdrlen += sizeof(dheader) - pos;
	} else if (type == OBJ_REF_DELTA) {
		/*
		 * Deltas with a base reference contain
		 * additional bytes for the base object ID.
		 */
		if (limit && hdrlen + hashsz + datalen + hashsz >= limit) {
			if (st)
				close_istream(st);
			free(buf);
			return 0;
		}
		hashwrite(f, header, hdrlen);
		hashwrite(f, DELTA(entry)->idx.oid.hash, hashsz);
		hdrlen += hashsz;
	} else {
		if (limit && hdrlen + datalen + hashsz >= limit) {
			if (st)
				close_istream(st);
			free(buf);
			return 0;
		}
		hashwrite(f, header, hdrlen);
	}
	if (st) {
		datalen = write_large_blob_data(st, f, &entry->idx.oid);
		close_istream(st);
	} else {
		hashwrite(f, buf, datalen);
		free(buf);
	}

	return hdrlen + datalen;
}

/* Return 0 if we will bust the pack-size limit */
static off_t write_reuse_object(struct hashfile *f, struct object_entry *entry,
				unsigned long limit, int usable_delta)
{
	struct packed_git *p = IN_PACK(entry);
	struct pack_window *w_curs = NULL;
	uint32_t pos;
	off_t offset;
	enum object_type type = oe_type(entry);
	off_t datalen;
	unsigned char header[MAX_PACK_OBJECT_HEADER],
		      dheader[MAX_PACK_OBJECT_HEADER];
	unsigned hdrlen;
	const unsigned hashsz = the_hash_algo->rawsz;
	unsigned long entry_size = SIZE(entry);

	if (DELTA(entry))
		type = (allow_ofs_delta && DELTA(entry)->idx.offset) ?
			OBJ_OFS_DELTA : OBJ_REF_DELTA;
	hdrlen = encode_in_pack_object_header(header, sizeof(header),
					      type, entry_size);

	offset = entry->in_pack_offset;
	if (offset_to_pack_pos(p, offset, &pos) < 0)
		die(_("write_reuse_object: could not locate %s, expected at "
		      "offset %"PRIuMAX" in pack %s"),
		    oid_to_hex(&entry->idx.oid), (uintmax_t)offset,
		    p->pack_name);
	datalen = pack_pos_to_offset(p, pos + 1) - offset;
	if (!pack_to_stdout && p->index_version > 1 &&
	    check_pack_crc(p, &w_curs, offset, datalen,
			   pack_pos_to_index(p, pos))) {
		error(_("bad packed object CRC for %s"),
		      oid_to_hex(&entry->idx.oid));
		unuse_pack(&w_curs);
		return write_no_reuse_object(f, entry, limit, usable_delta);
	}

	offset += entry->in_pack_header_size;
	datalen -= entry->in_pack_header_size;

	if (!pack_to_stdout && p->index_version == 1 &&
	    check_pack_inflate(p, &w_curs, offset, datalen, entry_size)) {
		error(_("corrupt packed object for %s"),
		      oid_to_hex(&entry->idx.oid));
		unuse_pack(&w_curs);
		return write_no_reuse_object(f, entry, limit, usable_delta);
	}

	if (type == OBJ_OFS_DELTA) {
		off_t ofs = entry->idx.offset - DELTA(entry)->idx.offset;
		unsigned pos = sizeof(dheader) - 1;
		dheader[pos] = ofs & 127;
		while (ofs >>= 7)
			dheader[--pos] = 128 | (--ofs & 127);
		if (limit && hdrlen + sizeof(dheader) - pos + datalen + hashsz >= limit) {
			unuse_pack(&w_curs);
			return 0;
		}
		hashwrite(f, header, hdrlen);
		hashwrite(f, dheader + pos, sizeof(dheader) - pos);
		hdrlen += sizeof(dheader) - pos;
		reused_delta++;
	} else if (type == OBJ_REF_DELTA) {
		if (limit && hdrlen + hashsz + datalen + hashsz >= limit) {
			unuse_pack(&w_curs);
			return 0;
		}
		hashwrite(f, header, hdrlen);
		hashwrite(f, DELTA(entry)->idx.oid.hash, hashsz);
		hdrlen += hashsz;
		reused_delta++;
	} else {
		if (limit && hdrlen + datalen + hashsz >= limit) {
			unuse_pack(&w_curs);
			return 0;
		}
		hashwrite(f, header, hdrlen);
	}
	copy_pack_data(f, p, &w_curs, offset, datalen);
	unuse_pack(&w_curs);
	reused++;
	return hdrlen + datalen;
}

/* Return 0 if we will bust the pack-size limit */
static off_t write_object(struct hashfile *f,
			  struct object_entry *entry,
			  off_t write_offset)
{
	unsigned long limit;
	off_t len;
	int usable_delta, to_reuse;

	if (!pack_to_stdout)
		crc32_begin(f);

	/* apply size limit if limited packsize and not first object */
	if (!pack_size_limit || !nr_written)
		limit = 0;
	else if (pack_size_limit <= write_offset)
		/*
		 * the earlier object did not fit the limit; avoid
		 * mistaking this with unlimited (i.e. limit = 0).
		 */
		limit = 1;
	else
		limit = pack_size_limit - write_offset;

	if (!DELTA(entry))
		usable_delta = 0;	/* no delta */
	else if (!pack_size_limit)
	       usable_delta = 1;	/* unlimited packfile */
	else if (DELTA(entry)->idx.offset == (off_t)-1)
		usable_delta = 0;	/* base was written to another pack */
	else if (DELTA(entry)->idx.offset)
		usable_delta = 1;	/* base already exists in this pack */
	else
		usable_delta = 0;	/* base could end up in another pack */

	if (!reuse_object)
		to_reuse = 0;	/* explicit */
	else if (!IN_PACK(entry))
		to_reuse = 0;	/* can't reuse what we don't have */
	else if (oe_type(entry) == OBJ_REF_DELTA ||
		 oe_type(entry) == OBJ_OFS_DELTA)
				/* check_object() decided it for us ... */
		to_reuse = usable_delta;
				/* ... but pack split may override that */
	else if (oe_type(entry) != entry->in_pack_type)
		to_reuse = 0;	/* pack has delta which is unusable */
	else if (DELTA(entry))
		to_reuse = 0;	/* we want to pack afresh */
	else
		to_reuse = 1;	/* we have it in-pack undeltified,
				 * and we do not need to deltify it.
				 */

	if (!to_reuse)
		len = write_no_reuse_object(f, entry, limit, usable_delta);
	else
		len = write_reuse_object(f, entry, limit, usable_delta);
	if (!len)
		return 0;

	if (usable_delta)
		written_delta++;
	written++;
	if (!pack_to_stdout)
		entry->idx.crc32 = crc32_end(f);
	return len;
}

enum write_one_status {
	WRITE_ONE_SKIP = -1, /* already written */
	WRITE_ONE_BREAK = 0, /* writing this will bust the limit; not written */
	WRITE_ONE_WRITTEN = 1, /* normal */
	WRITE_ONE_RECURSIVE = 2 /* already scheduled to be written */
};

static enum write_one_status write_one(struct hashfile *f,
				       struct object_entry *e,
				       off_t *offset)
{
	off_t size;
	int recursing;

	/*
	 * we set offset to 1 (which is an impossible value) to mark
	 * the fact that this object is involved in "write its base
	 * first before writing a deltified object" recursion.
	 */
	recursing = (e->idx.offset == 1);
	if (recursing) {
		warning(_("recursive delta detected for object %s"),
			oid_to_hex(&e->idx.oid));
		return WRITE_ONE_RECURSIVE;
	} else if (e->idx.offset || e->preferred_base) {
		/* offset is non zero if object is written already. */
		return WRITE_ONE_SKIP;
	}

	/* if we are deltified, write out base object first. */
	if (DELTA(e)) {
		e->idx.offset = 1; /* now recurse */
		switch (write_one(f, DELTA(e), offset)) {
		case WRITE_ONE_RECURSIVE:
			/* we cannot depend on this one */
			SET_DELTA(e, NULL);
			break;
		default:
			break;
		case WRITE_ONE_BREAK:
			e->idx.offset = recursing;
			return WRITE_ONE_BREAK;
		}
	}

	e->idx.offset = *offset;
	size = write_object(f, e, *offset);
	if (!size) {
		e->idx.offset = recursing;
		return WRITE_ONE_BREAK;
	}
	written_list[nr_written++] = &e->idx;

	/* make sure off_t is sufficiently large not to wrap */
	if (signed_add_overflows(*offset, size))
		die(_("pack too large for current definition of off_t"));
	*offset += size;
	return WRITE_ONE_WRITTEN;
}

static int mark_tagged(const char *path UNUSED, const char *referent UNUSED, const struct object_id *oid,
		       int flag UNUSED, void *cb_data UNUSED)
{
	struct object_id peeled;
	struct object_entry *entry = packlist_find(&to_pack, oid);

	if (entry)
		entry->tagged = 1;
	if (!peel_iterated_oid(the_repository, oid, &peeled)) {
		entry = packlist_find(&to_pack, &peeled);
		if (entry)
			entry->tagged = 1;
	}
	return 0;
}

static inline unsigned char oe_layer(struct packing_data *pack,
				     struct object_entry *e)
{
	if (!pack->layer)
		return 0;
	return pack->layer[e - pack->objects];
}

static inline void add_to_write_order(struct object_entry **wo,
			       unsigned int *endp,
			       struct object_entry *e)
{
	if (e->filled || oe_layer(&to_pack, e) != write_layer)
		return;
	wo[(*endp)++] = e;
	e->filled = 1;
}

static void add_descendants_to_write_order(struct object_entry **wo,
					   unsigned int *endp,
					   struct object_entry *e)
{
	int add_to_order = 1;
	while (e) {
		if (add_to_order) {
			struct object_entry *s;
			/* add this node... */
			add_to_write_order(wo, endp, e);
			/* all its siblings... */
			for (s = DELTA_SIBLING(e); s; s = DELTA_SIBLING(s)) {
				add_to_write_order(wo, endp, s);
			}
		}
		/* drop down a level to add left subtree nodes if possible */
		if (DELTA_CHILD(e)) {
			add_to_order = 1;
			e = DELTA_CHILD(e);
		} else {
			add_to_order = 0;
			/* our sibling might have some children, it is next */
			if (DELTA_SIBLING(e)) {
				e = DELTA_SIBLING(e);
				continue;
			}
			/* go back to our parent node */
			e = DELTA(e);
			while (e && !DELTA_SIBLING(e)) {
				/* we're on the right side of a subtree, keep
				 * going up until we can go right again */
				e = DELTA(e);
			}
			if (!e) {
				/* done- we hit our original root node */
				return;
			}
			/* pass it off to sibling at this level */
			e = DELTA_SIBLING(e);
		}
	};
}

static void add_family_to_write_order(struct object_entry **wo,
				      unsigned int *endp,
				      struct object_entry *e)
{
	struct object_entry *root;

	for (root = e; DELTA(root); root = DELTA(root))
		; /* nothing */
	add_descendants_to_write_order(wo, endp, root);
}

static void compute_layer_order(struct object_entry **wo, unsigned int *wo_end)
{
	unsigned int i, last_untagged;
	struct object_entry *objects = to_pack.objects;

	for (i = 0; i < to_pack.nr_objects; i++) {
		if (objects[i].tagged)
			break;
		add_to_write_order(wo, wo_end, &objects[i]);
	}
	last_untagged = i;

	/*
	 * Then fill all the tagged tips.
	 */
	for (; i < to_pack.nr_objects; i++) {
		if (objects[i].tagged)
			add_to_write_order(wo, wo_end, &objects[i]);
	}

	/*
	 * And then all remaining commits and tags.
	 */
	for (i = last_untagged; i < to_pack.nr_objects; i++) {
		if (oe_type(&objects[i]) != OBJ_COMMIT &&
		    oe_type(&objects[i]) != OBJ_TAG)
			continue;
		add_to_write_order(wo, wo_end, &objects[i]);
	}

	/*
	 * And then all the trees.
	 */
	for (i = last_untagged; i < to_pack.nr_objects; i++) {
		if (oe_type(&objects[i]) != OBJ_TREE)
			continue;
		add_to_write_order(wo, wo_end, &objects[i]);
	}

	/*
	 * Finally all the rest in really tight order
	 */
	for (i = last_untagged; i < to_pack.nr_objects; i++) {
		if (!objects[i].filled && oe_layer(&to_pack, &objects[i]) == write_layer)
			add_family_to_write_order(wo, wo_end, &objects[i]);
	}
}

static struct object_entry **compute_write_order(void)
{
	uint32_t max_layers = 1;
	unsigned int i, wo_end;

	struct object_entry **wo;
	struct object_entry *objects = to_pack.objects;

	for (i = 0; i < to_pack.nr_objects; i++) {
		objects[i].tagged = 0;
		objects[i].filled = 0;
		SET_DELTA_CHILD(&objects[i], NULL);
		SET_DELTA_SIBLING(&objects[i], NULL);
	}

	/*
	 * Fully connect delta_child/delta_sibling network.
	 * Make sure delta_sibling is sorted in the original
	 * recency order.
	 */
	for (i = to_pack.nr_objects; i > 0;) {
		struct object_entry *e = &objects[--i];
		if (!DELTA(e))
			continue;
		/* Mark me as the first child */
		e->delta_sibling_idx = DELTA(e)->delta_child_idx;
		SET_DELTA_CHILD(DELTA(e), e);
	}

	/*
	 * Mark objects that are at the tip of tags.
	 */
	refs_for_each_tag_ref(get_main_ref_store(the_repository), mark_tagged,
			      NULL);

	if (use_delta_islands) {
		max_layers = compute_pack_layers(&to_pack);
		free_island_marks();
	}

	ALLOC_ARRAY(wo, to_pack.nr_objects);
	wo_end = 0;

	for (; write_layer < max_layers; ++write_layer)
		compute_layer_order(wo, &wo_end);

	if (wo_end != to_pack.nr_objects)
		die(_("ordered %u objects, expected %"PRIu32),
		    wo_end, to_pack.nr_objects);

	return wo;
}


/*
 * A reused set of objects. All objects in a chunk have the same
 * relative position in the original packfile and the generated
 * packfile.
 */

static struct reused_chunk {
	/* The offset of the first object of this chunk in the original
	 * packfile. */
	off_t original;
	/* The difference for "original" minus the offset of the first object of
	 * this chunk in the generated packfile. */
	off_t difference;
} *reused_chunks;
static int reused_chunks_nr;
static int reused_chunks_alloc;

static void record_reused_object(off_t where, off_t offset)
{
	if (reused_chunks_nr && reused_chunks[reused_chunks_nr-1].difference == offset)
		return;

	ALLOC_GROW(reused_chunks, reused_chunks_nr + 1,
		   reused_chunks_alloc);
	reused_chunks[reused_chunks_nr].original = where;
	reused_chunks[reused_chunks_nr].difference = offset;
	reused_chunks_nr++;
}

/*
 * Binary search to find the chunk that "where" is in. Note
 * that we're not looking for an exact match, just the first
 * chunk that contains it (which implicitly ends at the start
 * of the next chunk.
 */
static off_t find_reused_offset(off_t where)
{
	int lo = 0, hi = reused_chunks_nr;
	while (lo < hi) {
		int mi = lo + ((hi - lo) / 2);
		if (where == reused_chunks[mi].original)
			return reused_chunks[mi].difference;
		if (where < reused_chunks[mi].original)
			hi = mi;
		else
			lo = mi + 1;
	}

	/*
	 * The first chunk starts at zero, so we can't have gone below
	 * there.
	 */
	assert(lo);
	return reused_chunks[lo-1].difference;
}

static void write_reused_pack_one(struct packed_git *reuse_packfile,
				  size_t pos, struct hashfile *out,
				  off_t pack_start,
				  struct pack_window **w_curs)
{
	off_t offset, next, cur;
	enum object_type type;
	unsigned long size;

	offset = pack_pos_to_offset(reuse_packfile, pos);
	next = pack_pos_to_offset(reuse_packfile, pos + 1);

	record_reused_object(offset,
			     offset - (hashfile_total(out) - pack_start));

	cur = offset;
	type = unpack_object_header(reuse_packfile, w_curs, &cur, &size);
	assert(type >= 0);

	if (type == OBJ_OFS_DELTA) {
		off_t base_offset;
		off_t fixup;

		unsigned char header[MAX_PACK_OBJECT_HEADER];
		unsigned len;

		base_offset = get_delta_base(reuse_packfile, w_curs, &cur, type, offset);
		assert(base_offset != 0);

		/* Convert to REF_DELTA if we must... */
		if (!allow_ofs_delta) {
			uint32_t base_pos;
			struct object_id base_oid;

			if (offset_to_pack_pos(reuse_packfile, base_offset, &base_pos) < 0)
				die(_("expected object at offset %"PRIuMAX" "
				      "in pack %s"),
				    (uintmax_t)base_offset,
				    reuse_packfile->pack_name);

			nth_packed_object_id(&base_oid, reuse_packfile,
					     pack_pos_to_index(reuse_packfile, base_pos));

			len = encode_in_pack_object_header(header, sizeof(header),
							   OBJ_REF_DELTA, size);
			hashwrite(out, header, len);
			hashwrite(out, base_oid.hash, the_hash_algo->rawsz);
			copy_pack_data(out, reuse_packfile, w_curs, cur, next - cur);
			return;
		}

		/* Otherwise see if we need to rewrite the offset... */
		fixup = find_reused_offset(offset) -
			find_reused_offset(base_offset);
		if (fixup) {
			unsigned char ofs_header[MAX_PACK_OBJECT_HEADER];
			unsigned i, ofs_len;
			off_t ofs = offset - base_offset - fixup;

			len = encode_in_pack_object_header(header, sizeof(header),
							   OBJ_OFS_DELTA, size);

			i = sizeof(ofs_header) - 1;
			ofs_header[i] = ofs & 127;
			while (ofs >>= 7)
				ofs_header[--i] = 128 | (--ofs & 127);

			ofs_len = sizeof(ofs_header) - i;

			hashwrite(out, header, len);
			hashwrite(out, ofs_header + sizeof(ofs_header) - ofs_len, ofs_len);
			copy_pack_data(out, reuse_packfile, w_curs, cur, next - cur);
			return;
		}

		/* ...otherwise we have no fixup, and can write it verbatim */
	}

	copy_pack_data(out, reuse_packfile, w_curs, offset, next - offset);
}

static size_t write_reused_pack_verbatim(struct bitmapped_pack *reuse_packfile,
					 struct hashfile *out,
					 off_t pack_start,
					 struct pack_window **w_curs)
{
	size_t pos = reuse_packfile->bitmap_pos;
	size_t end;

	if (pos % BITS_IN_EWORD) {
		size_t word_pos = (pos / BITS_IN_EWORD);
		size_t offset = pos % BITS_IN_EWORD;
		size_t last;
		eword_t word = reuse_packfile_bitmap->words[word_pos];

		if (offset + reuse_packfile->bitmap_nr < BITS_IN_EWORD)
			last = offset + reuse_packfile->bitmap_nr;
		else
			last = BITS_IN_EWORD;

		for (; offset < last; offset++) {
			if (word >> offset == 0)
				return word_pos;
			if (!bitmap_get(reuse_packfile_bitmap,
					word_pos * BITS_IN_EWORD + offset))
				return word_pos;
		}

		pos += BITS_IN_EWORD - (pos % BITS_IN_EWORD);
	}

	/*
	 * Now we're going to copy as many whole eword_t's as possible.
	 * "end" is the index of the last whole eword_t we copy, but
	 * there may be additional bits to process. Those are handled
	 * individually by write_reused_pack().
	 *
	 * Begin by advancing to the first word boundary in range of the
	 * bit positions occupied by objects in "reuse_packfile". Then
	 * pick the last word boundary in the same range. If we have at
	 * least one word's worth of bits to process, continue on.
	 */
	end = reuse_packfile->bitmap_pos + reuse_packfile->bitmap_nr;
	if (end % BITS_IN_EWORD)
		end -= end % BITS_IN_EWORD;
	if (pos >= end)
		return reuse_packfile->bitmap_pos / BITS_IN_EWORD;

	while (pos < end &&
	       reuse_packfile_bitmap->words[pos / BITS_IN_EWORD] == (eword_t)~0)
		pos += BITS_IN_EWORD;

	if (pos > end)
		pos = end;

	if (reuse_packfile->bitmap_pos < pos) {
		off_t pack_start_off = pack_pos_to_offset(reuse_packfile->p, 0);
		off_t pack_end_off = pack_pos_to_offset(reuse_packfile->p,
							pos - reuse_packfile->bitmap_pos);

		written += pos - reuse_packfile->bitmap_pos;

		/* We're recording one chunk, not one object. */
		record_reused_object(pack_start_off,
				     pack_start_off - (hashfile_total(out) - pack_start));
		hashflush(out);
		copy_pack_data(out, reuse_packfile->p, w_curs,
			pack_start_off, pack_end_off - pack_start_off);

		display_progress(progress_state, written);
	}
	if (pos % BITS_IN_EWORD)
		BUG("attempted to jump past a word boundary to %"PRIuMAX,
		    (uintmax_t)pos);
	return pos / BITS_IN_EWORD;
}

static void write_reused_pack(struct bitmapped_pack *reuse_packfile,
			      struct hashfile *f)
{
	size_t i = reuse_packfile->bitmap_pos / BITS_IN_EWORD;
	uint32_t offset;
	off_t pack_start = hashfile_total(f) - sizeof(struct pack_header);
	struct pack_window *w_curs = NULL;

	if (allow_ofs_delta)
		i = write_reused_pack_verbatim(reuse_packfile, f, pack_start,
					       &w_curs);

	for (; i < reuse_packfile_bitmap->word_alloc; ++i) {
		eword_t word = reuse_packfile_bitmap->words[i];
		size_t pos = (i * BITS_IN_EWORD);

		for (offset = 0; offset < BITS_IN_EWORD; ++offset) {
			uint32_t pack_pos;
			if ((word >> offset) == 0)
				break;

			offset += ewah_bit_ctz64(word >> offset);
			if (pos + offset < reuse_packfile->bitmap_pos)
				continue;
			if (pos + offset >= reuse_packfile->bitmap_pos + reuse_packfile->bitmap_nr)
				goto done;

			if (reuse_packfile->bitmap_pos) {
				/*
				 * When doing multi-pack reuse on a
				 * non-preferred pack, translate bit positions
				 * from the MIDX pseudo-pack order back to their
				 * pack-relative positions before attempting
				 * reuse.
				 */
				struct multi_pack_index *m = reuse_packfile->from_midx;
				uint32_t midx_pos;
				off_t pack_ofs;

				if (!m)
					BUG("non-zero bitmap position without MIDX");

				midx_pos = pack_pos_to_midx(m, pos + offset);
				pack_ofs = nth_midxed_offset(m, midx_pos);

				if (offset_to_pack_pos(reuse_packfile->p,
						       pack_ofs, &pack_pos) < 0)
					BUG("could not find expected object at offset %"PRIuMAX" in pack %s",
					    (uintmax_t)pack_ofs,
					    pack_basename(reuse_packfile->p));
			} else {
				/*
				 * Can use bit positions directly, even for MIDX
				 * bitmaps. See comment in try_partial_reuse()
				 * for why.
				 */
				pack_pos = pos + offset;
			}

			write_reused_pack_one(reuse_packfile->p, pack_pos, f,
					      pack_start, &w_curs);
			display_progress(progress_state, ++written);
		}
	}

done:
	unuse_pack(&w_curs);
}

static void write_excluded_by_configs(void)
{
	struct oidset_iter iter;
	const struct object_id *oid;

	oidset_iter_init(&excluded_by_config, &iter);
	while ((oid = oidset_iter_next(&iter))) {
		struct configured_exclusion *ex =
			oidmap_get(&configured_exclusions, oid);

		if (!ex)
			BUG("configured exclusion wasn't configured");
		write_in_full(1, ex->pack_hash_hex, strlen(ex->pack_hash_hex));
		write_in_full(1, " ", 1);
		write_in_full(1, ex->uri, strlen(ex->uri));
		write_in_full(1, "\n", 1);
	}
}

static const char no_split_warning[] = N_(
"disabling bitmap writing, packs are split due to pack.packSizeLimit"
);

static void write_pack_file(void)
{
	uint32_t i = 0, j;
	struct hashfile *f;
	off_t offset;
	uint32_t nr_remaining = nr_result;
	time_t last_mtime = 0;
	struct object_entry **write_order;

	if (progress > pack_to_stdout)
		progress_state = start_progress(_("Writing objects"), nr_result);
	ALLOC_ARRAY(written_list, to_pack.nr_objects);
	write_order = compute_write_order();

	do {
		unsigned char hash[GIT_MAX_RAWSZ];
		char *pack_tmp_name = NULL;

		if (pack_to_stdout)
			f = hashfd_throughput(1, "<stdout>", progress_state);
		else
			f = create_tmp_packfile(&pack_tmp_name);

		offset = write_pack_header(f, nr_remaining);

		if (reuse_packfiles_nr) {
			assert(pack_to_stdout);
			for (j = 0; j < reuse_packfiles_nr; j++) {
				reused_chunks_nr = 0;
				write_reused_pack(&reuse_packfiles[j], f);
				if (reused_chunks_nr)
					reuse_packfiles_used_nr++;
			}
			offset = hashfile_total(f);
		}

		nr_written = 0;
		for (; i < to_pack.nr_objects; i++) {
			struct object_entry *e = write_order[i];
			if (write_one(f, e, &offset) == WRITE_ONE_BREAK)
				break;
			display_progress(progress_state, written);
		}

		if (pack_to_stdout) {
			/*
			 * We never fsync when writing to stdout since we may
			 * not be writing to an actual pack file. For instance,
			 * the upload-pack code passes a pipe here. Calling
			 * fsync on a pipe results in unnecessary
			 * synchronization with the reader on some platforms.
			 */
			finalize_hashfile(f, hash, FSYNC_COMPONENT_NONE,
					  CSUM_HASH_IN_STREAM | CSUM_CLOSE);
		} else if (nr_written == nr_remaining) {
			finalize_hashfile(f, hash, FSYNC_COMPONENT_PACK,
					  CSUM_HASH_IN_STREAM | CSUM_FSYNC | CSUM_CLOSE);
		} else {
			/*
			 * If we wrote the wrong number of entries in the
			 * header, rewrite it like in fast-import.
			 */

			int fd = finalize_hashfile(f, hash, FSYNC_COMPONENT_PACK, 0);
			fixup_pack_header_footer(fd, hash, pack_tmp_name,
						 nr_written, hash, offset);
			close(fd);
			if (write_bitmap_index) {
				if (write_bitmap_index != WRITE_BITMAP_QUIET)
					warning(_(no_split_warning));
				write_bitmap_index = 0;
			}
		}

		if (!pack_to_stdout) {
			struct stat st;
			struct strbuf tmpname = STRBUF_INIT;
			struct bitmap_writer bitmap_writer;
			char *idx_tmp_name = NULL;

			/*
			 * Packs are runtime accessed in their mtime
			 * order since newer packs are more likely to contain
			 * younger objects.  So if we are creating multiple
			 * packs then we should modify the mtime of later ones
			 * to preserve this property.
			 */
			if (stat(pack_tmp_name, &st) < 0) {
				warning_errno(_("failed to stat %s"), pack_tmp_name);
			} else if (!last_mtime) {
				last_mtime = st.st_mtime;
			} else {
				struct utimbuf utb;
				utb.actime = st.st_atime;
				utb.modtime = --last_mtime;
				if (utime(pack_tmp_name, &utb) < 0)
					warning_errno(_("failed utime() on %s"), pack_tmp_name);
			}

			strbuf_addf(&tmpname, "%s-%s.", base_name,
				    hash_to_hex(hash));

			if (write_bitmap_index) {
				bitmap_writer_init(&bitmap_writer,
						   the_repository, &to_pack);
				bitmap_writer_set_checksum(&bitmap_writer, hash);
				bitmap_writer_build_type_index(&bitmap_writer,
							       written_list);
			}

			if (cruft)
				pack_idx_opts.flags |= WRITE_MTIMES;

			stage_tmp_packfiles(&tmpname, pack_tmp_name,
					    written_list, nr_written,
					    &to_pack, &pack_idx_opts, hash,
					    &idx_tmp_name);

			if (write_bitmap_index) {
				size_t tmpname_len = tmpname.len;

				strbuf_addstr(&tmpname, "bitmap");
				stop_progress(&progress_state);

				bitmap_writer_show_progress(&bitmap_writer,
							    progress);
				bitmap_writer_select_commits(&bitmap_writer,
							     indexed_commits,
							     indexed_commits_nr);
				if (bitmap_writer_build(&bitmap_writer) < 0)
					die(_("failed to write bitmap index"));
				bitmap_writer_finish(&bitmap_writer,
						     written_list,
						     tmpname.buf, write_bitmap_options);
				bitmap_writer_free(&bitmap_writer);
				write_bitmap_index = 0;
				strbuf_setlen(&tmpname, tmpname_len);
			}

			rename_tmp_packfile_idx(&tmpname, &idx_tmp_name);

			free(idx_tmp_name);
			strbuf_release(&tmpname);
			free(pack_tmp_name);
			puts(hash_to_hex(hash));
		}

		/* mark written objects as written to previous pack */
		for (j = 0; j < nr_written; j++) {
			written_list[j]->offset = (off_t)-1;
		}
		nr_remaining -= nr_written;
	} while (nr_remaining && i < to_pack.nr_objects);

	free(written_list);
	free(write_order);
	stop_progress(&progress_state);
	if (written != nr_result)
		die(_("wrote %"PRIu32" objects while expecting %"PRIu32),
		    written, nr_result);
	trace2_data_intmax("pack-objects", the_repository,
			   "write_pack_file/wrote", nr_result);
}

static int no_try_delta(const char *path)
{
	static struct attr_check *check;

	if (!check)
		check = attr_check_initl("delta", NULL);
	git_check_attr(the_repository->index, path, check);
	if (ATTR_FALSE(check->items[0].value))
		return 1;
	return 0;
}

/*
 * When adding an object, check whether we have already added it
 * to our packing list. If so, we can skip. However, if we are
 * being asked to excludei t, but the previous mention was to include
 * it, make sure to adjust its flags and tweak our numbers accordingly.
 *
 * As an optimization, we pass out the index position where we would have
 * found the item, since that saves us from having to look it up again a
 * few lines later when we want to add the new entry.
 */
static int have_duplicate_entry(const struct object_id *oid,
				int exclude)
{
	struct object_entry *entry;

	if (reuse_packfile_bitmap &&
	    bitmap_walk_contains(bitmap_git, reuse_packfile_bitmap, oid))
		return 1;

	entry = packlist_find(&to_pack, oid);
	if (!entry)
		return 0;

	if (exclude) {
		if (!entry->preferred_base)
			nr_result--;
		entry->preferred_base = 1;
	}

	return 1;
}

static int want_found_object(const struct object_id *oid, int exclude,
			     struct packed_git *p)
{
	if (exclude)
		return 1;
	if (incremental)
		return 0;

	if (!is_pack_valid(p))
		return -1;

	/*
	 * When asked to do --local (do not include an object that appears in a
	 * pack we borrow from elsewhere) or --honor-pack-keep (do not include
	 * an object that appears in a pack marked with .keep), finding a pack
	 * that matches the criteria is sufficient for us to decide to omit it.
	 * However, even if this pack does not satisfy the criteria, we need to
	 * make sure no copy of this object appears in _any_ pack that makes us
	 * to omit the object, so we need to check all the packs.
	 *
	 * We can however first check whether these options can possibly matter;
	 * if they do not matter we know we want the object in generated pack.
	 * Otherwise, we signal "-1" at the end to tell the caller that we do
	 * not know either way, and it needs to check more packs.
	 */

	/*
	 * Objects in packs borrowed from elsewhere are discarded regardless of
	 * if they appear in other packs that weren't borrowed.
	 */
	if (local && !p->pack_local)
		return 0;

	/*
	 * Then handle .keep first, as we have a fast(er) path there.
	 */
	if (ignore_packed_keep_on_disk || ignore_packed_keep_in_core) {
		/*
		 * Set the flags for the kept-pack cache to be the ones we want
		 * to ignore.
		 *
		 * That is, if we are ignoring objects in on-disk keep packs,
		 * then we want to search through the on-disk keep and ignore
		 * the in-core ones.
		 */
		unsigned flags = 0;
		if (ignore_packed_keep_on_disk)
			flags |= ON_DISK_KEEP_PACKS;
		if (ignore_packed_keep_in_core)
			flags |= IN_CORE_KEEP_PACKS;

		if (ignore_packed_keep_on_disk && p->pack_keep)
			return 0;
		if (ignore_packed_keep_in_core && p->pack_keep_in_core)
			return 0;
		if (has_object_kept_pack(oid, flags))
			return 0;
	}

	/*
	 * At this point we know definitively that either we don't care about
	 * keep-packs, or the object is not in one. Keep checking other
	 * conditions...
	 */
	if (!local || !have_non_local_packs)
		return 1;

	/* we don't know yet; keep looking for more packs */
	return -1;
}

static int want_object_in_pack_one(struct packed_git *p,
				   const struct object_id *oid,
				   int exclude,
				   struct packed_git **found_pack,
				   off_t *found_offset)
{
	off_t offset;

	if (p == *found_pack)
		offset = *found_offset;
	else
		offset = find_pack_entry_one(oid->hash, p);

	if (offset) {
		if (!*found_pack) {
			if (!is_pack_valid(p))
				return -1;
			*found_offset = offset;
			*found_pack = p;
		}
		return want_found_object(oid, exclude, p);
	}
	return -1;
}

/*
 * Check whether we want the object in the pack (e.g., we do not want
 * objects found in non-local stores if the "--local" option was used).
 *
 * If the caller already knows an existing pack it wants to take the object
 * from, that is passed in *found_pack and *found_offset; otherwise this
 * function finds if there is any pack that has the object and returns the pack
 * and its offset in these variables.
 */
static int want_object_in_pack(const struct object_id *oid,
			       int exclude,
			       struct packed_git **found_pack,
			       off_t *found_offset)
{
	int want;
	struct list_head *pos;
	struct multi_pack_index *m;

	if (!exclude && local && has_loose_object_nonlocal(oid))
		return 0;

	/*
	 * If we already know the pack object lives in, start checks from that
	 * pack - in the usual case when neither --local was given nor .keep files
	 * are present we will determine the answer right now.
	 */
	if (*found_pack) {
		want = want_found_object(oid, exclude, *found_pack);
		if (want != -1)
			return want;

		*found_pack = NULL;
		*found_offset = 0;
	}

	for (m = get_multi_pack_index(the_repository); m; m = m->next) {
		struct pack_entry e;
		if (fill_midx_entry(the_repository, oid, &e, m)) {
			want = want_object_in_pack_one(e.p, oid, exclude, found_pack, found_offset);
			if (want != -1)
				return want;
		}
	}

	list_for_each(pos, get_packed_git_mru(the_repository)) {
		struct packed_git *p = list_entry(pos, struct packed_git, mru);
		want = want_object_in_pack_one(p, oid, exclude, found_pack, found_offset);
		if (!exclude && want > 0)
			list_move(&p->mru,
				  get_packed_git_mru(the_repository));
		if (want != -1)
			return want;
	}

	if (uri_protocols.nr) {
		struct configured_exclusion *ex =
			oidmap_get(&configured_exclusions, oid);
		int i;
		const char *p;

		if (ex) {
			for (i = 0; i < uri_protocols.nr; i++) {
				if (skip_prefix(ex->uri,
						uri_protocols.items[i].string,
						&p) &&
				    *p == ':') {
					oidset_insert(&excluded_by_config, oid);
					return 0;
				}
			}
		}
	}

	return 1;
}

static struct object_entry *create_object_entry(const struct object_id *oid,
						enum object_type type,
						uint32_t hash,
						int exclude,
						int no_try_delta,
						struct packed_git *found_pack,
						off_t found_offset)
{
	struct object_entry *entry;

	entry = packlist_alloc(&to_pack, oid);
	entry->hash = hash;
	oe_set_type(entry, type);
	if (exclude)
		entry->preferred_base = 1;
	else
		nr_result++;
	if (found_pack) {
		oe_set_in_pack(&to_pack, entry, found_pack);
		entry->in_pack_offset = found_offset;
	}

	entry->no_try_delta = no_try_delta;

	return entry;
}

static const char no_closure_warning[] = N_(
"disabling bitmap writing, as some objects are not being packed"
);

static int add_object_entry(const struct object_id *oid, enum object_type type,
			    const char *name, int exclude)
{
	struct packed_git *found_pack = NULL;
	off_t found_offset = 0;

	display_progress(progress_state, ++nr_seen);

	if (have_duplicate_entry(oid, exclude))
		return 0;

	if (!want_object_in_pack(oid, exclude, &found_pack, &found_offset)) {
		/* The pack is missing an object, so it will not have closure */
		if (write_bitmap_index) {
			if (write_bitmap_index != WRITE_BITMAP_QUIET)
				warning(_(no_closure_warning));
			write_bitmap_index = 0;
		}
		return 0;
	}

	create_object_entry(oid, type, pack_name_hash_fn(name),
			    exclude, name && no_try_delta(name),
			    found_pack, found_offset);
	return 1;
}

static int add_object_entry_from_bitmap(const struct object_id *oid,
					enum object_type type,
					int flags UNUSED, uint32_t name_hash,
					struct packed_git *pack, off_t offset)
{
	display_progress(progress_state, ++nr_seen);

	if (have_duplicate_entry(oid, 0))
		return 0;

	if (!want_object_in_pack(oid, 0, &pack, &offset))
		return 0;

	create_object_entry(oid, type, name_hash, 0, 0, pack, offset);
	return 1;
}

struct pbase_tree_cache {
	struct object_id oid;
	int ref;
	int temporary;
	void *tree_data;
	unsigned long tree_size;
};

static struct pbase_tree_cache *(pbase_tree_cache[256]);
static int pbase_tree_cache_ix(const struct object_id *oid)
{
	return oid->hash[0] % ARRAY_SIZE(pbase_tree_cache);
}
static int pbase_tree_cache_ix_incr(int ix)
{
	return (ix+1) % ARRAY_SIZE(pbase_tree_cache);
}

static struct pbase_tree {
	struct pbase_tree *next;
	/* This is a phony "cache" entry; we are not
	 * going to evict it or find it through _get()
	 * mechanism -- this is for the toplevel node that
	 * would almost always change with any commit.
	 */
	struct pbase_tree_cache pcache;
} *pbase_tree;

static struct pbase_tree_cache *pbase_tree_get(const struct object_id *oid)
{
	struct pbase_tree_cache *ent, *nent;
	void *data;
	unsigned long size;
	enum object_type type;
	int neigh;
	int my_ix = pbase_tree_cache_ix(oid);
	int available_ix = -1;

	/* pbase-tree-cache acts as a limited hashtable.
	 * your object will be found at your index or within a few
	 * slots after that slot if it is cached.
	 */
	for (neigh = 0; neigh < 8; neigh++) {
		ent = pbase_tree_cache[my_ix];
		if (ent && oideq(&ent->oid, oid)) {
			ent->ref++;
			return ent;
		}
		else if (((available_ix < 0) && (!ent || !ent->ref)) ||
			 ((0 <= available_ix) &&
			  (!ent && pbase_tree_cache[available_ix])))
			available_ix = my_ix;
		if (!ent)
			break;
		my_ix = pbase_tree_cache_ix_incr(my_ix);
	}

	/* Did not find one.  Either we got a bogus request or
	 * we need to read and perhaps cache.
	 */
	data = repo_read_object_file(the_repository, oid, &type, &size);
	if (!data)
		return NULL;
	if (type != OBJ_TREE) {
		free(data);
		return NULL;
	}

	/* We need to either cache or return a throwaway copy */

	if (available_ix < 0)
		ent = NULL;
	else {
		ent = pbase_tree_cache[available_ix];
		my_ix = available_ix;
	}

	if (!ent) {
		nent = xmalloc(sizeof(*nent));
		nent->temporary = (available_ix < 0);
	}
	else {
		/* evict and reuse */
		free(ent->tree_data);
		nent = ent;
	}
	oidcpy(&nent->oid, oid);
	nent->tree_data = data;
	nent->tree_size = size;
	nent->ref = 1;
	if (!nent->temporary)
		pbase_tree_cache[my_ix] = nent;
	return nent;
}

static void pbase_tree_put(struct pbase_tree_cache *cache)
{
	if (!cache->temporary) {
		cache->ref--;
		return;
	}
	free(cache->tree_data);
	free(cache);
}

static size_t name_cmp_len(const char *name)
{
	return strcspn(name, "\n/");
}

static void add_pbase_object(struct tree_desc *tree,
			     const char *name,
			     size_t cmplen,
			     const char *fullname)
{
	struct name_entry entry;
	int cmp;

	while (tree_entry(tree,&entry)) {
		if (S_ISGITLINK(entry.mode))
			continue;
		cmp = tree_entry_len(&entry) != cmplen ? 1 :
		      memcmp(name, entry.path, cmplen);
		if (cmp > 0)
			continue;
		if (cmp < 0)
			return;
		if (name[cmplen] != '/') {
			add_object_entry(&entry.oid,
					 object_type(entry.mode),
					 fullname, 1);
			return;
		}
		if (S_ISDIR(entry.mode)) {
			struct tree_desc sub;
			struct pbase_tree_cache *tree;
			const char *down = name+cmplen+1;
			size_t downlen = name_cmp_len(down);

			tree = pbase_tree_get(&entry.oid);
			if (!tree)
				return;
			init_tree_desc(&sub, &tree->oid,
				       tree->tree_data, tree->tree_size);

			add_pbase_object(&sub, down, downlen, fullname);
			pbase_tree_put(tree);
		}
	}
}

static unsigned *done_pbase_paths;
static int done_pbase_paths_num;
static int done_pbase_paths_alloc;
static int done_pbase_path_pos(unsigned hash)
{
	int lo = 0;
	int hi = done_pbase_paths_num;
	while (lo < hi) {
		int mi = lo + (hi - lo) / 2;
		if (done_pbase_paths[mi] == hash)
			return mi;
		if (done_pbase_paths[mi] < hash)
			hi = mi;
		else
			lo = mi + 1;
	}
	return -lo-1;
}

static int check_pbase_path(unsigned hash)
{
	int pos = done_pbase_path_pos(hash);
	if (0 <= pos)
		return 1;
	pos = -pos - 1;
	ALLOC_GROW(done_pbase_paths,
		   done_pbase_paths_num + 1,
		   done_pbase_paths_alloc);
	done_pbase_paths_num++;
	if (pos < done_pbase_paths_num)
		MOVE_ARRAY(done_pbase_paths + pos + 1, done_pbase_paths + pos,
			   done_pbase_paths_num - pos - 1);
	done_pbase_paths[pos] = hash;
	return 0;
}

static void add_preferred_base_object(const char *name)
{
	struct pbase_tree *it;
	size_t cmplen;
	unsigned hash = pack_name_hash_fn(name);

	if (!num_preferred_base || check_pbase_path(hash))
		return;

	cmplen = name_cmp_len(name);
	for (it = pbase_tree; it; it = it->next) {
		if (cmplen == 0) {
			add_object_entry(&it->pcache.oid, OBJ_TREE, NULL, 1);
		}
		else {
			struct tree_desc tree;
			init_tree_desc(&tree, &it->pcache.oid,
				       it->pcache.tree_data, it->pcache.tree_size);
			add_pbase_object(&tree, name, cmplen, name);
		}
	}
}

static void add_preferred_base(struct object_id *oid)
{
	struct pbase_tree *it;
	void *data;
	unsigned long size;
	struct object_id tree_oid;

	if (window <= num_preferred_base++)
		return;

	data = read_object_with_reference(the_repository, oid,
					  OBJ_TREE, &size, &tree_oid);
	if (!data)
		return;

	for (it = pbase_tree; it; it = it->next) {
		if (oideq(&it->pcache.oid, &tree_oid)) {
			free(data);
			return;
		}
	}

	CALLOC_ARRAY(it, 1);
	it->next = pbase_tree;
	pbase_tree = it;

	oidcpy(&it->pcache.oid, &tree_oid);
	it->pcache.tree_data = data;
	it->pcache.tree_size = size;
}

static void cleanup_preferred_base(void)
{
	struct pbase_tree *it;
	unsigned i;

	it = pbase_tree;
	pbase_tree = NULL;
	while (it) {
		struct pbase_tree *tmp = it;
		it = tmp->next;
		free(tmp->pcache.tree_data);
		free(tmp);
	}

	for (i = 0; i < ARRAY_SIZE(pbase_tree_cache); i++) {
		if (!pbase_tree_cache[i])
			continue;
		free(pbase_tree_cache[i]->tree_data);
		FREE_AND_NULL(pbase_tree_cache[i]);
	}

	FREE_AND_NULL(done_pbase_paths);
	done_pbase_paths_num = done_pbase_paths_alloc = 0;
}

/*
 * Return 1 iff the object specified by "delta" can be sent
 * literally as a delta against the base in "base_sha1". If
 * so, then *base_out will point to the entry in our packing
 * list, or NULL if we must use the external-base list.
 *
 * Depth value does not matter - find_deltas() will
 * never consider reused delta as the base object to
 * deltify other objects against, in order to avoid
 * circular deltas.
 */
static int can_reuse_delta(const struct object_id *base_oid,
			   struct object_entry *delta,
			   struct object_entry **base_out)
{
	struct object_entry *base;

	/*
	 * First see if we're already sending the base (or it's explicitly in
	 * our "excluded" list).
	 */
	base = packlist_find(&to_pack, base_oid);
	if (base) {
		if (!in_same_island(&delta->idx.oid, &base->idx.oid))
			return 0;
		*base_out = base;
		return 1;
	}

	/*
	 * Otherwise, reachability bitmaps may tell us if the receiver has it,
	 * even if it was buried too deep in history to make it into the
	 * packing list.
	 */
	if (thin && bitmap_has_oid_in_uninteresting(bitmap_git, base_oid)) {
		if (use_delta_islands) {
			if (!in_same_island(&delta->idx.oid, base_oid))
				return 0;
		}
		*base_out = NULL;
		return 1;
	}

	return 0;
}

static void prefetch_to_pack(uint32_t object_index_start) {
	struct oid_array to_fetch = OID_ARRAY_INIT;
	uint32_t i;

	for (i = object_index_start; i < to_pack.nr_objects; i++) {
		struct object_entry *entry = to_pack.objects + i;

		if (!oid_object_info_extended(the_repository,
					      &entry->idx.oid,
					      NULL,
					      OBJECT_INFO_FOR_PREFETCH))
			continue;
		oid_array_append(&to_fetch, &entry->idx.oid);
	}
	promisor_remote_get_direct(the_repository,
				   to_fetch.oid, to_fetch.nr);
	oid_array_clear(&to_fetch);
}

static void check_object(struct object_entry *entry, uint32_t object_index)
{
	unsigned long canonical_size;
	enum object_type type;
	struct object_info oi = {.typep = &type, .sizep = &canonical_size};

	if (IN_PACK(entry)) {
		struct packed_git *p = IN_PACK(entry);
		struct pack_window *w_curs = NULL;
		int have_base = 0;
		struct object_id base_ref;
		struct object_entry *base_entry;
		unsigned long used, used_0;
		unsigned long avail;
		off_t ofs;
		unsigned char *buf, c;
		enum object_type type;
		unsigned long in_pack_size;

		buf = use_pack(p, &w_curs, entry->in_pack_offset, &avail);

		/*
		 * We want in_pack_type even if we do not reuse delta
		 * since non-delta representations could still be reused.
		 */
		used = unpack_object_header_buffer(buf, avail,
						   &type,
						   &in_pack_size);
		if (used == 0)
			goto give_up;

		if (type < 0)
			BUG("invalid type %d", type);
		entry->in_pack_type = type;

		/*
		 * Determine if this is a delta and if so whether we can
		 * reuse it or not.  Otherwise let's find out as cheaply as
		 * possible what the actual type and size for this object is.
		 */
		switch (entry->in_pack_type) {
		default:
			/* Not a delta hence we've already got all we need. */
			oe_set_type(entry, entry->in_pack_type);
			SET_SIZE(entry, in_pack_size);
			entry->in_pack_header_size = used;
			if (oe_type(entry) < OBJ_COMMIT || oe_type(entry) > OBJ_BLOB)
				goto give_up;
			unuse_pack(&w_curs);
			return;
		case OBJ_REF_DELTA:
			if (reuse_delta && !entry->preferred_base) {
				oidread(&base_ref,
					use_pack(p, &w_curs,
						 entry->in_pack_offset + used,
						 NULL),
					the_repository->hash_algo);
				have_base = 1;
			}
			entry->in_pack_header_size = used + the_hash_algo->rawsz;
			break;
		case OBJ_OFS_DELTA:
			buf = use_pack(p, &w_curs,
				       entry->in_pack_offset + used, NULL);
			used_0 = 0;
			c = buf[used_0++];
			ofs = c & 127;
			while (c & 128) {
				ofs += 1;
				if (!ofs || MSB(ofs, 7)) {
					error(_("delta base offset overflow in pack for %s"),
					      oid_to_hex(&entry->idx.oid));
					goto give_up;
				}
				c = buf[used_0++];
				ofs = (ofs << 7) + (c & 127);
			}
			ofs = entry->in_pack_offset - ofs;
			if (ofs <= 0 || ofs >= entry->in_pack_offset) {
				error(_("delta base offset out of bound for %s"),
				      oid_to_hex(&entry->idx.oid));
				goto give_up;
			}
			if (reuse_delta && !entry->preferred_base) {
				uint32_t pos;
				if (offset_to_pack_pos(p, ofs, &pos) < 0)
					goto give_up;
				if (!nth_packed_object_id(&base_ref, p,
							  pack_pos_to_index(p, pos)))
					have_base = 1;
			}
			entry->in_pack_header_size = used + used_0;
			break;
		}

		if (have_base &&
		    can_reuse_delta(&base_ref, entry, &base_entry)) {
			oe_set_type(entry, entry->in_pack_type);
			SET_SIZE(entry, in_pack_size); /* delta size */
			SET_DELTA_SIZE(entry, in_pack_size);

			if (base_entry) {
				SET_DELTA(entry, base_entry);
				entry->delta_sibling_idx = base_entry->delta_child_idx;
				SET_DELTA_CHILD(base_entry, entry);
			} else {
				SET_DELTA_EXT(entry, &base_ref);
			}

			unuse_pack(&w_curs);
			return;
		}

		if (oe_type(entry)) {
			off_t delta_pos;

			/*
			 * This must be a delta and we already know what the
			 * final object type is.  Let's extract the actual
			 * object size from the delta header.
			 */
			delta_pos = entry->in_pack_offset + entry->in_pack_header_size;
			canonical_size = get_size_from_delta(p, &w_curs, delta_pos);
			if (canonical_size == 0)
				goto give_up;
			SET_SIZE(entry, canonical_size);
			unuse_pack(&w_curs);
			return;
		}

		/*
		 * No choice but to fall back to the recursive delta walk
		 * with oid_object_info() to find about the object type
		 * at this point...
		 */
		give_up:
		unuse_pack(&w_curs);
	}

	if (oid_object_info_extended(the_repository, &entry->idx.oid, &oi,
				     OBJECT_INFO_SKIP_FETCH_OBJECT | OBJECT_INFO_LOOKUP_REPLACE) < 0) {
		if (repo_has_promisor_remote(the_repository)) {
			prefetch_to_pack(object_index);
			if (oid_object_info_extended(the_repository, &entry->idx.oid, &oi,
						     OBJECT_INFO_SKIP_FETCH_OBJECT | OBJECT_INFO_LOOKUP_REPLACE) < 0)
				type = -1;
		} else {
			type = -1;
		}
	}
	oe_set_type(entry, type);
	if (entry->type_valid) {
		SET_SIZE(entry, canonical_size);
	} else {
		/*
		 * Bad object type is checked in prepare_pack().  This is
		 * to permit a missing preferred base object to be ignored
		 * as a preferred base.  Doing so can result in a larger
		 * pack file, but the transfer will still take place.
		 */
	}
}

static int pack_offset_sort(const void *_a, const void *_b)
{
	const struct object_entry *a = *(struct object_entry **)_a;
	const struct object_entry *b = *(struct object_entry **)_b;
	const struct packed_git *a_in_pack = IN_PACK(a);
	const struct packed_git *b_in_pack = IN_PACK(b);

	/* avoid filesystem trashing with loose objects */
	if (!a_in_pack && !b_in_pack)
		return oidcmp(&a->idx.oid, &b->idx.oid);

	if (a_in_pack < b_in_pack)
		return -1;
	if (a_in_pack > b_in_pack)
		return 1;
	return a->in_pack_offset < b->in_pack_offset ? -1 :
			(a->in_pack_offset > b->in_pack_offset);
}

/*
 * Drop an on-disk delta we were planning to reuse. Naively, this would
 * just involve blanking out the "delta" field, but we have to deal
 * with some extra book-keeping:
 *
 *   1. Removing ourselves from the delta_sibling linked list.
 *
 *   2. Updating our size/type to the non-delta representation. These were
 *      either not recorded initially (size) or overwritten with the delta type
 *      (type) when check_object() decided to reuse the delta.
 *
 *   3. Resetting our delta depth, as we are now a base object.
 */
static void drop_reused_delta(struct object_entry *entry)
{
	unsigned *idx = &to_pack.objects[entry->delta_idx - 1].delta_child_idx;
	struct object_info oi = OBJECT_INFO_INIT;
	enum object_type type;
	unsigned long size;

	while (*idx) {
		struct object_entry *oe = &to_pack.objects[*idx - 1];

		if (oe == entry)
			*idx = oe->delta_sibling_idx;
		else
			idx = &oe->delta_sibling_idx;
	}
	SET_DELTA(entry, NULL);
	entry->depth = 0;

	oi.sizep = &size;
	oi.typep = &type;
	if (packed_object_info(the_repository, IN_PACK(entry), entry->in_pack_offset, &oi) < 0) {
		/*
		 * We failed to get the info from this pack for some reason;
		 * fall back to oid_object_info, which may find another copy.
		 * And if that fails, the error will be recorded in oe_type(entry)
		 * and dealt with in prepare_pack().
		 */
		oe_set_type(entry,
			    oid_object_info(the_repository, &entry->idx.oid, &size));
	} else {
		oe_set_type(entry, type);
	}
	SET_SIZE(entry, size);
}

/*
 * Follow the chain of deltas from this entry onward, throwing away any links
 * that cause us to hit a cycle (as determined by the DFS state flags in
 * the entries).
 *
 * We also detect too-long reused chains that would violate our --depth
 * limit.
 */
static void break_delta_chains(struct object_entry *entry)
{
	/*
	 * The actual depth of each object we will write is stored as an int,
	 * as it cannot exceed our int "depth" limit. But before we break
	 * changes based no that limit, we may potentially go as deep as the
	 * number of objects, which is elsewhere bounded to a uint32_t.
	 */
	uint32_t total_depth;
	struct object_entry *cur, *next;

	for (cur = entry, total_depth = 0;
	     cur;
	     cur = DELTA(cur), total_depth++) {
		if (cur->dfs_state == DFS_DONE) {
			/*
			 * We've already seen this object and know it isn't
			 * part of a cycle. We do need to append its depth
			 * to our count.
			 */
			total_depth += cur->depth;
			break;
		}

		/*
		 * We break cycles before looping, so an ACTIVE state (or any
		 * other cruft which made its way into the state variable)
		 * is a bug.
		 */
		if (cur->dfs_state != DFS_NONE)
			BUG("confusing delta dfs state in first pass: %d",
			    cur->dfs_state);

		/*
		 * Now we know this is the first time we've seen the object. If
		 * it's not a delta, we're done traversing, but we'll mark it
		 * done to save time on future traversals.
		 */
		if (!DELTA(cur)) {
			cur->dfs_state = DFS_DONE;
			break;
		}

		/*
		 * Mark ourselves as active and see if the next step causes
		 * us to cycle to another active object. It's important to do
		 * this _before_ we loop, because it impacts where we make the
		 * cut, and thus how our total_depth counter works.
		 * E.g., We may see a partial loop like:
		 *
		 *   A -> B -> C -> D -> B
		 *
		 * Cutting B->C breaks the cycle. But now the depth of A is
		 * only 1, and our total_depth counter is at 3. The size of the
		 * error is always one less than the size of the cycle we
		 * broke. Commits C and D were "lost" from A's chain.
		 *
		 * If we instead cut D->B, then the depth of A is correct at 3.
		 * We keep all commits in the chain that we examined.
		 */
		cur->dfs_state = DFS_ACTIVE;
		if (DELTA(cur)->dfs_state == DFS_ACTIVE) {
			drop_reused_delta(cur);
			cur->dfs_state = DFS_DONE;
			break;
		}
	}

	/*
	 * And now that we've gone all the way to the bottom of the chain, we
	 * need to clear the active flags and set the depth fields as
	 * appropriate. Unlike the loop above, which can quit when it drops a
	 * delta, we need to keep going to look for more depth cuts. So we need
	 * an extra "next" pointer to keep going after we reset cur->delta.
	 */
	for (cur = entry; cur; cur = next) {
		next = DELTA(cur);

		/*
		 * We should have a chain of zero or more ACTIVE states down to
		 * a final DONE. We can quit after the DONE, because either it
		 * has no bases, or we've already handled them in a previous
		 * call.
		 */
		if (cur->dfs_state == DFS_DONE)
			break;
		else if (cur->dfs_state != DFS_ACTIVE)
			BUG("confusing delta dfs state in second pass: %d",
			    cur->dfs_state);

		/*
		 * If the total_depth is more than depth, then we need to snip
		 * the chain into two or more smaller chains that don't exceed
		 * the maximum depth. Most of the resulting chains will contain
		 * (depth + 1) entries (i.e., depth deltas plus one base), and
		 * the last chain (i.e., the one containing entry) will contain
		 * whatever entries are left over, namely
		 * (total_depth % (depth + 1)) of them.
		 *
		 * Since we are iterating towards decreasing depth, we need to
		 * decrement total_depth as we go, and we need to write to the
		 * entry what its final depth will be after all of the
		 * snipping. Since we're snipping into chains of length (depth
		 * + 1) entries, the final depth of an entry will be its
		 * original depth modulo (depth + 1). Any time we encounter an
		 * entry whose final depth is supposed to be zero, we snip it
		 * from its delta base, thereby making it so.
		 */
		cur->depth = (total_depth--) % (depth + 1);
		if (!cur->depth)
			drop_reused_delta(cur);

		cur->dfs_state = DFS_DONE;
	}
}

static void get_object_details(void)
{
	uint32_t i;
	struct object_entry **sorted_by_offset;

	if (progress)
		progress_state = start_progress(_("Counting objects"),
						to_pack.nr_objects);

	CALLOC_ARRAY(sorted_by_offset, to_pack.nr_objects);
	for (i = 0; i < to_pack.nr_objects; i++)
		sorted_by_offset[i] = to_pack.objects + i;
	QSORT(sorted_by_offset, to_pack.nr_objects, pack_offset_sort);

	for (i = 0; i < to_pack.nr_objects; i++) {
		struct object_entry *entry = sorted_by_offset[i];
		check_object(entry, i);
		if (entry->type_valid &&
		    oe_size_greater_than(&to_pack, entry, big_file_threshold))
			entry->no_try_delta = 1;
		display_progress(progress_state, i + 1);
	}
	stop_progress(&progress_state);

	/*
	 * This must happen in a second pass, since we rely on the delta
	 * information for the whole list being completed.
	 */
	for (i = 0; i < to_pack.nr_objects; i++)
		break_delta_chains(&to_pack.objects[i]);

	free(sorted_by_offset);
}

/*
 * We search for deltas in a list sorted by type, by filename hash, and then
 * by size, so that we see progressively smaller and smaller files.
 * That's because we prefer deltas to be from the bigger file
 * to the smaller -- deletes are potentially cheaper, but perhaps
 * more importantly, the bigger file is likely the more recent
 * one.  The deepest deltas are therefore the oldest objects which are
 * less susceptible to be accessed often.
 */
static int type_size_sort(const void *_a, const void *_b)
{
	const struct object_entry *a = *(struct object_entry **)_a;
	const struct object_entry *b = *(struct object_entry **)_b;
	const enum object_type a_type = oe_type(a);
	const enum object_type b_type = oe_type(b);
	const unsigned long a_size = SIZE(a);
	const unsigned long b_size = SIZE(b);

	if (a_type > b_type)
		return -1;
	if (a_type < b_type)
		return 1;
	if (a->hash > b->hash)
		return -1;
	if (a->hash < b->hash)
		return 1;
	if (a->preferred_base > b->preferred_base)
		return -1;
	if (a->preferred_base < b->preferred_base)
		return 1;
	if (use_delta_islands) {
		const int island_cmp = island_delta_cmp(&a->idx.oid, &b->idx.oid);
		if (island_cmp)
			return island_cmp;
	}
	if (a_size > b_size)
		return -1;
	if (a_size < b_size)
		return 1;
	return a < b ? -1 : (a > b);  /* newest first */
}

struct unpacked {
	struct object_entry *entry;
	void *data;
	struct delta_index *index;
	unsigned depth;
};

static int delta_cacheable(unsigned long src_size, unsigned long trg_size,
			   unsigned long delta_size)
{
	if (max_delta_cache_size && delta_cache_size + delta_size > max_delta_cache_size)
		return 0;

	if (delta_size < cache_max_small_delta_size)
		return 1;

	/* cache delta, if objects are large enough compared to delta size */
	if ((src_size >> 20) + (trg_size >> 21) > (delta_size >> 10))
		return 1;

	return 0;
}

/* Protect delta_cache_size */
static pthread_mutex_t cache_mutex;
#define cache_lock()		pthread_mutex_lock(&cache_mutex)
#define cache_unlock()		pthread_mutex_unlock(&cache_mutex)

/*
 * Protect object list partitioning (e.g. struct thread_param) and
 * progress_state
 */
static pthread_mutex_t progress_mutex;
#define progress_lock()		pthread_mutex_lock(&progress_mutex)
#define progress_unlock()	pthread_mutex_unlock(&progress_mutex)

/*
 * Access to struct object_entry is unprotected since each thread owns
 * a portion of the main object list. Just don't access object entries
 * ahead in the list because they can be stolen and would need
 * progress_mutex for protection.
 */

static inline int oe_size_less_than(struct packing_data *pack,
				    const struct object_entry *lhs,
				    unsigned long rhs)
{
	if (lhs->size_valid)
		return lhs->size_ < rhs;
	if (rhs < pack->oe_size_limit) /* rhs < 2^x <= lhs ? */
		return 0;
	return oe_get_size_slow(pack, lhs) < rhs;
}

static inline void oe_set_tree_depth(struct packing_data *pack,
				     struct object_entry *e,
				     unsigned int tree_depth)
{
	if (!pack->tree_depth)
		CALLOC_ARRAY(pack->tree_depth, pack->nr_alloc);
	pack->tree_depth[e - pack->objects] = tree_depth;
}

/*
 * Return the size of the object without doing any delta
 * reconstruction (so non-deltas are true object sizes, but deltas
 * return the size of the delta data).
 */
unsigned long oe_get_size_slow(struct packing_data *pack,
			       const struct object_entry *e)
{
	struct packed_git *p;
	struct pack_window *w_curs;
	unsigned char *buf;
	enum object_type type;
	unsigned long used, avail, size;

	if (e->type_ != OBJ_OFS_DELTA && e->type_ != OBJ_REF_DELTA) {
		packing_data_lock(&to_pack);
		if (oid_object_info(the_repository, &e->idx.oid, &size) < 0)
			die(_("unable to get size of %s"),
			    oid_to_hex(&e->idx.oid));
		packing_data_unlock(&to_pack);
		return size;
	}

	p = oe_in_pack(pack, e);
	if (!p)
		BUG("when e->type is a delta, it must belong to a pack");

	packing_data_lock(&to_pack);
	w_curs = NULL;
	buf = use_pack(p, &w_curs, e->in_pack_offset, &avail);
	used = unpack_object_header_buffer(buf, avail, &type, &size);
	if (used == 0)
		die(_("unable to parse object header of %s"),
		    oid_to_hex(&e->idx.oid));

	unuse_pack(&w_curs);
	packing_data_unlock(&to_pack);
	return size;
}

static int try_delta(struct unpacked *trg, struct unpacked *src,
		     unsigned max_depth, unsigned long *mem_usage)
{
	struct object_entry *trg_entry = trg->entry;
	struct object_entry *src_entry = src->entry;
	unsigned long trg_size, src_size, delta_size, sizediff, max_size, sz;
	unsigned ref_depth;
	enum object_type type;
	void *delta_buf;

	/* Don't bother doing diffs between different types */
	if (oe_type(trg_entry) != oe_type(src_entry))
		return -1;

	/*
	 * We do not bother to try a delta that we discarded on an
	 * earlier try, but only when reusing delta data.  Note that
	 * src_entry that is marked as the preferred_base should always
	 * be considered, as even if we produce a suboptimal delta against
	 * it, we will still save the transfer cost, as we already know
	 * the other side has it and we won't send src_entry at all.
	 */
	if (reuse_delta && IN_PACK(trg_entry) &&
	    IN_PACK(trg_entry) == IN_PACK(src_entry) &&
	    !src_entry->preferred_base &&
	    trg_entry->in_pack_type != OBJ_REF_DELTA &&
	    trg_entry->in_pack_type != OBJ_OFS_DELTA)
		return 0;

	/* Let's not bust the allowed depth. */
	if (src->depth >= max_depth)
		return 0;

	/* Now some size filtering heuristics. */
	trg_size = SIZE(trg_entry);
	if (!DELTA(trg_entry)) {
		max_size = trg_size/2 - the_hash_algo->rawsz;
		ref_depth = 1;
	} else {
		max_size = DELTA_SIZE(trg_entry);
		ref_depth = trg->depth;
	}
	max_size = (uint64_t)max_size * (max_depth - src->depth) /
						(max_depth - ref_depth + 1);
	if (max_size == 0)
		return 0;
	src_size = SIZE(src_entry);
	sizediff = src_size < trg_size ? trg_size - src_size : 0;
	if (sizediff >= max_size)
		return 0;
	if (trg_size < src_size / 32)
		return 0;

	if (!in_same_island(&trg->entry->idx.oid, &src->entry->idx.oid))
		return 0;

	/* Load data if not already done */
	if (!trg->data) {
		packing_data_lock(&to_pack);
		trg->data = repo_read_object_file(the_repository,
						  &trg_entry->idx.oid, &type,
						  &sz);
		packing_data_unlock(&to_pack);
		if (!trg->data)
			die(_("object %s cannot be read"),
			    oid_to_hex(&trg_entry->idx.oid));
		if (sz != trg_size)
			die(_("object %s inconsistent object length (%"PRIuMAX" vs %"PRIuMAX")"),
			    oid_to_hex(&trg_entry->idx.oid), (uintmax_t)sz,
			    (uintmax_t)trg_size);
		*mem_usage += sz;
	}
	if (!src->data) {
		packing_data_lock(&to_pack);
		src->data = repo_read_object_file(the_repository,
						  &src_entry->idx.oid, &type,
						  &sz);
		packing_data_unlock(&to_pack);
		if (!src->data) {
			if (src_entry->preferred_base) {
				static int warned = 0;
				if (!warned++)
					warning(_("object %s cannot be read"),
						oid_to_hex(&src_entry->idx.oid));
				/*
				 * Those objects are not included in the
				 * resulting pack.  Be resilient and ignore
				 * them if they can't be read, in case the
				 * pack could be created nevertheless.
				 */
				return 0;
			}
			die(_("object %s cannot be read"),
			    oid_to_hex(&src_entry->idx.oid));
		}
		if (sz != src_size)
			die(_("object %s inconsistent object length (%"PRIuMAX" vs %"PRIuMAX")"),
			    oid_to_hex(&src_entry->idx.oid), (uintmax_t)sz,
			    (uintmax_t)src_size);
		*mem_usage += sz;
	}
	if (!src->index) {
		src->index = create_delta_index(src->data, src_size);
		if (!src->index) {
			static int warned = 0;
			if (!warned++)
				warning(_("suboptimal pack - out of memory"));
			return 0;
		}
		*mem_usage += sizeof_delta_index(src->index);
	}

	delta_buf = create_delta(src->index, trg->data, trg_size, &delta_size, max_size);
	if (!delta_buf)
		return 0;

	if (DELTA(trg_entry)) {
		/* Prefer only shallower same-sized deltas. */
		if (delta_size == DELTA_SIZE(trg_entry) &&
		    src->depth + 1 >= trg->depth) {
			free(delta_buf);
			return 0;
		}
	}

	/*
	 * Handle memory allocation outside of the cache
	 * accounting lock.  Compiler will optimize the strangeness
	 * away when NO_PTHREADS is defined.
	 */
	free(trg_entry->delta_data);
	cache_lock();
	if (trg_entry->delta_data) {
		delta_cache_size -= DELTA_SIZE(trg_entry);
		trg_entry->delta_data = NULL;
	}
	if (delta_cacheable(src_size, trg_size, delta_size)) {
		delta_cache_size += delta_size;
		cache_unlock();
		trg_entry->delta_data = xrealloc(delta_buf, delta_size);
	} else {
		cache_unlock();
		free(delta_buf);
	}

	SET_DELTA(trg_entry, src_entry);
	SET_DELTA_SIZE(trg_entry, delta_size);
	trg->depth = src->depth + 1;

	return 1;
}

static unsigned int check_delta_limit(struct object_entry *me, unsigned int n)
{
	struct object_entry *child = DELTA_CHILD(me);
	unsigned int m = n;
	while (child) {
		const unsigned int c = check_delta_limit(child, n + 1);
		if (m < c)
			m = c;
		child = DELTA_SIBLING(child);
	}
	return m;
}

static unsigned long free_unpacked(struct unpacked *n)
{
	unsigned long freed_mem = sizeof_delta_index(n->index);
	free_delta_index(n->index);
	n->index = NULL;
	if (n->data) {
		freed_mem += SIZE(n->entry);
		FREE_AND_NULL(n->data);
	}
	n->entry = NULL;
	n->depth = 0;
	return freed_mem;
}

static void find_deltas(struct object_entry **list, unsigned *list_size,
			int window, int depth, unsigned *processed)
{
	uint32_t i, idx = 0, count = 0;
	struct unpacked *array;
	unsigned long mem_usage = 0;

	CALLOC_ARRAY(array, window);

	for (;;) {
		struct object_entry *entry;
		struct unpacked *n = array + idx;
		int j, max_depth, best_base = -1;

		progress_lock();
		if (!*list_size) {
			progress_unlock();
			break;
		}
		entry = *list++;
		(*list_size)--;
		if (!entry->preferred_base) {
			(*processed)++;
			display_progress(progress_state, *processed);
		}
		progress_unlock();

		mem_usage -= free_unpacked(n);
		n->entry = entry;

		while (window_memory_limit &&
		       mem_usage > window_memory_limit &&
		       count > 1) {
			const uint32_t tail = (idx + window - count) % window;
			mem_usage -= free_unpacked(array + tail);
			count--;
		}

		/* We do not compute delta to *create* objects we are not
		 * going to pack.
		 */
		if (entry->preferred_base)
			goto next;

		/*
		 * If the current object is at pack edge, take the depth the
		 * objects that depend on the current object into account
		 * otherwise they would become too deep.
		 */
		max_depth = depth;
		if (DELTA_CHILD(entry)) {
			max_depth -= check_delta_limit(entry, 0);
			if (max_depth <= 0)
				goto next;
		}

		j = window;
		while (--j > 0) {
			int ret;
			uint32_t other_idx = idx + j;
			struct unpacked *m;
			if (other_idx >= window)
				other_idx -= window;
			m = array + other_idx;
			if (!m->entry)
				break;
			ret = try_delta(n, m, max_depth, &mem_usage);
			if (ret < 0)
				break;
			else if (ret > 0)
				best_base = other_idx;
		}

		/*
		 * If we decided to cache the delta data, then it is best
		 * to compress it right away.  First because we have to do
		 * it anyway, and doing it here while we're threaded will
		 * save a lot of time in the non threaded write phase,
		 * as well as allow for caching more deltas within
		 * the same cache size limit.
		 * ...
		 * But only if not writing to stdout, since in that case
		 * the network is most likely throttling writes anyway,
		 * and therefore it is best to go to the write phase ASAP
		 * instead, as we can afford spending more time compressing
		 * between writes at that moment.
		 */
		if (entry->delta_data && !pack_to_stdout) {
			unsigned long size;

			size = do_compress(&entry->delta_data, DELTA_SIZE(entry));
			if (size < (1U << OE_Z_DELTA_BITS)) {
				entry->z_delta_size = size;
				cache_lock();
				delta_cache_size -= DELTA_SIZE(entry);
				delta_cache_size += entry->z_delta_size;
				cache_unlock();
			} else {
				FREE_AND_NULL(entry->delta_data);
				entry->z_delta_size = 0;
			}
		}

		/* if we made n a delta, and if n is already at max
		 * depth, leaving it in the window is pointless.  we
		 * should evict it first.
		 */
		if (DELTA(entry) && max_depth <= n->depth)
			continue;

		/*
		 * Move the best delta base up in the window, after the
		 * currently deltified object, to keep it longer.  It will
		 * be the first base object to be attempted next.
		 */
		if (DELTA(entry)) {
			struct unpacked swap = array[best_base];
			int dist = (window + idx - best_base) % window;
			int dst = best_base;
			while (dist--) {
				int src = (dst + 1) % window;
				array[dst] = array[src];
				dst = src;
			}
			array[dst] = swap;
		}

		next:
		idx++;
		if (count + 1 < window)
			count++;
		if (idx >= window)
			idx = 0;
	}

	for (i = 0; i < window; ++i) {
		free_delta_index(array[i].index);
		free(array[i].data);
	}
	free(array);
}

/*
 * The main object list is split into smaller lists, each is handed to
 * one worker.
 *
 * The main thread waits on the condition that (at least) one of the workers
 * has stopped working (which is indicated in the .working member of
 * struct thread_params).
 *
 * When a work thread has completed its work, it sets .working to 0 and
 * signals the main thread and waits on the condition that .data_ready
 * becomes 1.
 *
 * The main thread steals half of the work from the worker that has
 * most work left to hand it to the idle worker.
 */

struct thread_params {
	pthread_t thread;
	struct object_entry **list;
	unsigned list_size;
	unsigned remaining;
	int window;
	int depth;
	int working;
	int data_ready;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	unsigned *processed;
};

static pthread_cond_t progress_cond;

/*
 * Mutex and conditional variable can't be statically-initialized on Windows.
 */
static void init_threaded_search(void)
{
	pthread_mutex_init(&cache_mutex, NULL);
	pthread_mutex_init(&progress_mutex, NULL);
	pthread_cond_init(&progress_cond, NULL);
}

static void cleanup_threaded_search(void)
{
	pthread_cond_destroy(&progress_cond);
	pthread_mutex_destroy(&cache_mutex);
	pthread_mutex_destroy(&progress_mutex);
}

static void *threaded_find_deltas(void *arg)
{
	struct thread_params *me = arg;

	progress_lock();
	while (me->remaining) {
		progress_unlock();

		find_deltas(me->list, &me->remaining,
			    me->window, me->depth, me->processed);

		progress_lock();
		me->working = 0;
		pthread_cond_signal(&progress_cond);
		progress_unlock();

		/*
		 * We must not set ->data_ready before we wait on the
		 * condition because the main thread may have set it to 1
		 * before we get here. In order to be sure that new
		 * work is available if we see 1 in ->data_ready, it
		 * was initialized to 0 before this thread was spawned
		 * and we reset it to 0 right away.
		 */
		pthread_mutex_lock(&me->mutex);
		while (!me->data_ready)
			pthread_cond_wait(&me->cond, &me->mutex);
		me->data_ready = 0;
		pthread_mutex_unlock(&me->mutex);

		progress_lock();
	}
	progress_unlock();
	/* leave ->working 1 so that this doesn't get more work assigned */
	return NULL;
}

static void ll_find_deltas(struct object_entry **list, unsigned list_size,
			   int window, int depth, unsigned *processed)
{
	struct thread_params *p;
	int i, ret, active_threads = 0;

	init_threaded_search();

	if (delta_search_threads <= 1) {
		find_deltas(list, &list_size, window, depth, processed);
		cleanup_threaded_search();
		return;
	}
	if (progress > pack_to_stdout)
		fprintf_ln(stderr, _("Delta compression using up to %d threads"),
			   delta_search_threads);
	CALLOC_ARRAY(p, delta_search_threads);

	/* Partition the work amongst work threads. */
	for (i = 0; i < delta_search_threads; i++) {
		unsigned sub_size = list_size / (delta_search_threads - i);

		/* don't use too small segments or no deltas will be found */
		if (sub_size < 2*window && i+1 < delta_search_threads)
			sub_size = 0;

		p[i].window = window;
		p[i].depth = depth;
		p[i].processed = processed;
		p[i].working = 1;
		p[i].data_ready = 0;

		/* try to split chunks on "path" boundaries */
		while (sub_size && sub_size < list_size &&
		       list[sub_size]->hash &&
		       list[sub_size]->hash == list[sub_size-1]->hash)
			sub_size++;

		p[i].list = list;
		p[i].list_size = sub_size;
		p[i].remaining = sub_size;

		list += sub_size;
		list_size -= sub_size;
	}

	/* Start work threads. */
	for (i = 0; i < delta_search_threads; i++) {
		if (!p[i].list_size)
			continue;
		pthread_mutex_init(&p[i].mutex, NULL);
		pthread_cond_init(&p[i].cond, NULL);
		ret = pthread_create(&p[i].thread, NULL,
				     threaded_find_deltas, &p[i]);
		if (ret)
			die(_("unable to create thread: %s"), strerror(ret));
		active_threads++;
	}

	/*
	 * Now let's wait for work completion.  Each time a thread is done
	 * with its work, we steal half of the remaining work from the
	 * thread with the largest number of unprocessed objects and give
	 * it to that newly idle thread.  This ensure good load balancing
	 * until the remaining object list segments are simply too short
	 * to be worth splitting anymore.
	 */
	while (active_threads) {
		struct thread_params *target = NULL;
		struct thread_params *victim = NULL;
		unsigned sub_size = 0;

		progress_lock();
		for (;;) {
			for (i = 0; !target && i < delta_search_threads; i++)
				if (!p[i].working)
					target = &p[i];
			if (target)
				break;
			pthread_cond_wait(&progress_cond, &progress_mutex);
		}

		for (i = 0; i < delta_search_threads; i++)
			if (p[i].remaining > 2*window &&
			    (!victim || victim->remaining < p[i].remaining))
				victim = &p[i];
		if (victim) {
			sub_size = victim->remaining / 2;
			list = victim->list + victim->list_size - sub_size;
			while (sub_size && list[0]->hash &&
			       list[0]->hash == list[-1]->hash) {
				list++;
				sub_size--;
			}
			if (!sub_size) {
				/*
				 * It is possible for some "paths" to have
				 * so many objects that no hash boundary
				 * might be found.  Let's just steal the
				 * exact half in that case.
				 */
				sub_size = victim->remaining / 2;
				list -= sub_size;
			}
			target->list = list;
			victim->list_size -= sub_size;
			victim->remaining -= sub_size;
		}
		target->list_size = sub_size;
		target->remaining = sub_size;
		target->working = 1;
		progress_unlock();

		pthread_mutex_lock(&target->mutex);
		target->data_ready = 1;
		pthread_cond_signal(&target->cond);
		pthread_mutex_unlock(&target->mutex);

		if (!sub_size) {
			pthread_join(target->thread, NULL);
			pthread_cond_destroy(&target->cond);
			pthread_mutex_destroy(&target->mutex);
			active_threads--;
		}
	}
	cleanup_threaded_search();
	free(p);
}

static int obj_is_packed(const struct object_id *oid)
{
	return packlist_find(&to_pack, oid) ||
		(reuse_packfile_bitmap &&
		 bitmap_walk_contains(bitmap_git, reuse_packfile_bitmap, oid));
}

static void add_tag_chain(const struct object_id *oid)
{
	struct tag *tag;

	/*
	 * We catch duplicates already in add_object_entry(), but we'd
	 * prefer to do this extra check to avoid having to parse the
	 * tag at all if we already know that it's being packed (e.g., if
	 * it was included via bitmaps, we would not have parsed it
	 * previously).
	 */
	if (obj_is_packed(oid))
		return;

	tag = lookup_tag(the_repository, oid);
	while (1) {
		if (!tag || parse_tag(tag) || !tag->tagged)
			die(_("unable to pack objects reachable from tag %s"),
			    oid_to_hex(oid));

		add_object_entry(&tag->object.oid, OBJ_TAG, NULL, 0);

		if (tag->tagged->type != OBJ_TAG)
			return;

		tag = (struct tag *)tag->tagged;
	}
}

static int add_ref_tag(const char *tag UNUSED, const char *referent UNUSED, const struct object_id *oid,
		       int flag UNUSED, void *cb_data UNUSED)
{
	struct object_id peeled;

	if (!peel_iterated_oid(the_repository, oid, &peeled) && obj_is_packed(&peeled))
		add_tag_chain(oid);
	return 0;
}

static int should_attempt_deltas(struct object_entry *entry)
{
	if (DELTA(entry))
		return 0;

	if (!entry->type_valid ||
	    oe_size_less_than(&to_pack, entry, 50))
		return 0;

	if (entry->no_try_delta)
		return 0;

	if (!entry->preferred_base) {
		if (oe_type(entry) < 0)
			die(_("unable to get type of object %s"),
				oid_to_hex(&entry->idx.oid));
	} else if (oe_type(entry) < 0) {
		/*
		 * This object is not found, but we
		 * don't have to include it anyway.
		 */
		return 0;
	}

	return 1;
}

static void prepare_pack(int window, int depth)
{
	struct object_entry **delta_list;
	uint32_t i, nr_deltas;
	unsigned n;

	if (use_delta_islands)
		resolve_tree_islands(the_repository, progress, &to_pack);

	get_object_details();

	/*
	 * If we're locally repacking then we need to be doubly careful
	 * from now on in order to make sure no stealth corruption gets
	 * propagated to the new pack.  Clients receiving streamed packs
	 * should validate everything they get anyway so no need to incur
	 * the additional cost here in that case.
	 */
	if (!pack_to_stdout)
		do_check_packed_object_crc = 1;

	if (!to_pack.nr_objects || !window || !depth)
		return;

	ALLOC_ARRAY(delta_list, to_pack.nr_objects);
	nr_deltas = n = 0;

	for (i = 0; i < to_pack.nr_objects; i++) {
		struct object_entry *entry = to_pack.objects + i;

		if (!should_attempt_deltas(entry))
			continue;

		if (!entry->preferred_base)
			nr_deltas++;

		delta_list[n++] = entry;
	}

	if (nr_deltas && n > 1) {
		unsigned nr_done = 0;

		if (progress)
			progress_state = start_progress(_("Compressing objects"),
							nr_deltas);
		QSORT(delta_list, n, type_size_sort);
		ll_find_deltas(delta_list, n, window+1, depth, &nr_done);
		stop_progress(&progress_state);
		if (nr_done != nr_deltas)
			die(_("inconsistency with delta count"));
	}
	free(delta_list);
}

static int git_pack_config(const char *k, const char *v,
			   const struct config_context *ctx, void *cb)
{
	if (!strcmp(k, "pack.window")) {
		window = git_config_int(k, v, ctx->kvi);
		return 0;
	}
	if (!strcmp(k, "pack.windowmemory")) {
		window_memory_limit = git_config_ulong(k, v, ctx->kvi);
		return 0;
	}
	if (!strcmp(k, "pack.depth")) {
		depth = git_config_int(k, v, ctx->kvi);
		return 0;
	}
	if (!strcmp(k, "pack.deltacachesize")) {
		max_delta_cache_size = git_config_int(k, v, ctx->kvi);
		return 0;
	}
	if (!strcmp(k, "pack.deltacachelimit")) {
		cache_max_small_delta_size = git_config_int(k, v, ctx->kvi);
		return 0;
	}
	if (!strcmp(k, "pack.writebitmaphashcache")) {
		if (git_config_bool(k, v))
			write_bitmap_options |= BITMAP_OPT_HASH_CACHE;
		else
			write_bitmap_options &= ~BITMAP_OPT_HASH_CACHE;
	}

	if (!strcmp(k, "pack.writebitmaplookuptable")) {
		if (git_config_bool(k, v))
			write_bitmap_options |= BITMAP_OPT_LOOKUP_TABLE;
		else
			write_bitmap_options &= ~BITMAP_OPT_LOOKUP_TABLE;
	}

	if (!strcmp(k, "pack.usebitmaps")) {
		use_bitmap_index_default = git_config_bool(k, v);
		return 0;
	}
	if (!strcmp(k, "pack.allowpackreuse")) {
		int res = git_parse_maybe_bool_text(v);
		if (res < 0) {
			if (!strcasecmp(v, "single"))
				allow_pack_reuse = SINGLE_PACK_REUSE;
			else if (!strcasecmp(v, "multi"))
				allow_pack_reuse = MULTI_PACK_REUSE;
			else
				die(_("invalid pack.allowPackReuse value: '%s'"), v);
		} else if (res) {
			allow_pack_reuse = SINGLE_PACK_REUSE;
		} else {
			allow_pack_reuse = NO_PACK_REUSE;
		}
		return 0;
	}
	if (!strcmp(k, "pack.threads")) {
		delta_search_threads = git_config_int(k, v, ctx->kvi);
		if (delta_search_threads < 0)
			die(_("invalid number of threads specified (%d)"),
			    delta_search_threads);
		if (!HAVE_THREADS && delta_search_threads != 1) {
			warning(_("no threads support, ignoring %s"), k);
			delta_search_threads = 0;
		}
		return 0;
	}
	if (!strcmp(k, "pack.indexversion")) {
		pack_idx_opts.version = git_config_int(k, v, ctx->kvi);
		if (pack_idx_opts.version > 2)
			die(_("bad pack.indexVersion=%"PRIu32),
			    pack_idx_opts.version);
		return 0;
	}
	if (!strcmp(k, "pack.writereverseindex")) {
		if (git_config_bool(k, v))
			pack_idx_opts.flags |= WRITE_REV;
		else
			pack_idx_opts.flags &= ~WRITE_REV;
		return 0;
	}
	if (!strcmp(k, "uploadpack.blobpackfileuri")) {
		struct configured_exclusion *ex;
		const char *oid_end, *pack_end;
		/*
		 * Stores the pack hash. This is not a true object ID, but is
		 * of the same form.
		 */
		struct object_id pack_hash;

		if (!v)
			return config_error_nonbool(k);

		ex = xmalloc(sizeof(*ex));
		if (parse_oid_hex(v, &ex->e.oid, &oid_end) ||
		    *oid_end != ' ' ||
		    parse_oid_hex(oid_end + 1, &pack_hash, &pack_end) ||
		    *pack_end != ' ')
			die(_("value of uploadpack.blobpackfileuri must be "
			      "of the form '<object-hash> <pack-hash> <uri>' (got '%s')"), v);
		if (oidmap_get(&configured_exclusions, &ex->e.oid))
			die(_("object already configured in another "
			      "uploadpack.blobpackfileuri (got '%s')"), v);
		ex->pack_hash_hex = xcalloc(1, pack_end - oid_end);
		memcpy(ex->pack_hash_hex, oid_end + 1, pack_end - oid_end - 1);
		ex->uri = xstrdup(pack_end + 1);
		oidmap_put(&configured_exclusions, ex);
	}
	return git_default_config(k, v, ctx, cb);
}

/* Counters for trace2 output when in --stdin-packs mode. */
static int stdin_packs_found_nr;
static int stdin_packs_hints_nr;

static int add_object_entry_from_pack(const struct object_id *oid,
				      struct packed_git *p,
				      uint32_t pos,
				      void *_data)
{
	off_t ofs;
	enum object_type type = OBJ_NONE;

	display_progress(progress_state, ++nr_seen);

	if (have_duplicate_entry(oid, 0))
		return 0;

	ofs = nth_packed_object_offset(p, pos);
	if (!want_object_in_pack(oid, 0, &p, &ofs))
		return 0;

	if (p) {
		struct rev_info *revs = _data;
		struct object_info oi = OBJECT_INFO_INIT;

		oi.typep = &type;
		if (packed_object_info(the_repository, p, ofs, &oi) < 0) {
			die(_("could not get type of object %s in pack %s"),
			    oid_to_hex(oid), p->pack_name);
		} else if (type == OBJ_COMMIT) {
			/*
			 * commits in included packs are used as starting points for the
			 * subsequent revision walk
			 */
			add_pending_oid(revs, NULL, oid, 0);
		}

		stdin_packs_found_nr++;
	}

	create_object_entry(oid, type, 0, 0, 0, p, ofs);

	return 0;
}

static void show_commit_pack_hint(struct commit *commit UNUSED,
				  void *data UNUSED)
{
	/* nothing to do; commits don't have a namehash */
}

static void show_object_pack_hint(struct object *object, const char *name,
				  void *data UNUSED)
{
	struct object_entry *oe = packlist_find(&to_pack, &object->oid);
	if (!oe)
		return;

	/*
	 * Our 'to_pack' list was constructed by iterating all objects packed in
	 * included packs, and so doesn't have a non-zero hash field that you
	 * would typically pick up during a reachability traversal.
	 *
	 * Make a best-effort attempt to fill in the ->hash and ->no_try_delta
	 * here using a now in order to perhaps improve the delta selection
	 * process.
	 */
	oe->hash = pack_name_hash_fn(name);
	oe->no_try_delta = name && no_try_delta(name);

	stdin_packs_hints_nr++;
}

static int pack_mtime_cmp(const void *_a, const void *_b)
{
	struct packed_git *a = ((const struct string_list_item*)_a)->util;
	struct packed_git *b = ((const struct string_list_item*)_b)->util;

	/*
	 * order packs by descending mtime so that objects are laid out
	 * roughly as newest-to-oldest
	 */
	if (a->mtime < b->mtime)
		return 1;
	else if (b->mtime < a->mtime)
		return -1;
	else
		return 0;
}

static void read_packs_list_from_stdin(void)
{
	struct strbuf buf = STRBUF_INIT;
	struct string_list include_packs = STRING_LIST_INIT_DUP;
	struct string_list exclude_packs = STRING_LIST_INIT_DUP;
	struct string_list_item *item = NULL;

	struct packed_git *p;
	struct rev_info revs;

	repo_init_revisions(the_repository, &revs, NULL);
	/*
	 * Use a revision walk to fill in the namehash of objects in the include
	 * packs. To save time, we'll avoid traversing through objects that are
	 * in excluded packs.
	 *
	 * That may cause us to avoid populating all of the namehash fields of
	 * all included objects, but our goal is best-effort, since this is only
	 * an optimization during delta selection.
	 */
	revs.no_kept_objects = 1;
	revs.keep_pack_cache_flags |= IN_CORE_KEEP_PACKS;
	revs.blob_objects = 1;
	revs.tree_objects = 1;
	revs.tag_objects = 1;
	revs.ignore_missing_links = 1;

	while (strbuf_getline(&buf, stdin) != EOF) {
		if (!buf.len)
			continue;

		if (*buf.buf == '^')
			string_list_append(&exclude_packs, buf.buf + 1);
		else
			string_list_append(&include_packs, buf.buf);

		strbuf_reset(&buf);
	}

	string_list_sort(&include_packs);
	string_list_remove_duplicates(&include_packs, 0);
	string_list_sort(&exclude_packs);
	string_list_remove_duplicates(&exclude_packs, 0);

	for (p = get_all_packs(the_repository); p; p = p->next) {
		const char *pack_name = pack_basename(p);

		if ((item = string_list_lookup(&include_packs, pack_name)))
			item->util = p;
		if ((item = string_list_lookup(&exclude_packs, pack_name)))
			item->util = p;
	}

	/*
	 * Arguments we got on stdin may not even be packs. First
	 * check that to avoid segfaulting later on in
	 * e.g. pack_mtime_cmp(), excluded packs are handled below.
	 *
	 * Since we first parsed our STDIN and then sorted the input
	 * lines the pack we error on will be whatever line happens to
	 * sort first. This is lazy, it's enough that we report one
	 * bad case here, we don't need to report the first/last one,
	 * or all of them.
	 */
	for_each_string_list_item(item, &include_packs) {
		struct packed_git *p = item->util;
		if (!p)
			die(_("could not find pack '%s'"), item->string);
		if (!is_pack_valid(p))
			die(_("packfile %s cannot be accessed"), p->pack_name);
	}

	/*
	 * Then, handle all of the excluded packs, marking them as
	 * kept in-core so that later calls to add_object_entry()
	 * discards any objects that are also found in excluded packs.
	 */
	for_each_string_list_item(item, &exclude_packs) {
		struct packed_git *p = item->util;
		if (!p)
			die(_("could not find pack '%s'"), item->string);
		p->pack_keep_in_core = 1;
	}

	/*
	 * Order packs by ascending mtime; use QSORT directly to access the
	 * string_list_item's ->util pointer, which string_list_sort() does not
	 * provide.
	 */
	QSORT(include_packs.items, include_packs.nr, pack_mtime_cmp);

	for_each_string_list_item(item, &include_packs) {
		struct packed_git *p = item->util;
		for_each_object_in_pack(p,
					add_object_entry_from_pack,
					&revs,
					FOR_EACH_OBJECT_PACK_ORDER);
	}

	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));
	traverse_commit_list(&revs,
			     show_commit_pack_hint,
			     show_object_pack_hint,
			     NULL);

	trace2_data_intmax("pack-objects", the_repository, "stdin_packs_found",
			   stdin_packs_found_nr);
	trace2_data_intmax("pack-objects", the_repository, "stdin_packs_hints",
			   stdin_packs_hints_nr);

	strbuf_release(&buf);
	string_list_clear(&include_packs, 0);
	string_list_clear(&exclude_packs, 0);
}

static void add_cruft_object_entry(const struct object_id *oid, enum object_type type,
				   struct packed_git *pack, off_t offset,
				   const char *name, uint32_t mtime)
{
	struct object_entry *entry;

	display_progress(progress_state, ++nr_seen);

	entry = packlist_find(&to_pack, oid);
	if (entry) {
		if (name) {
			entry->hash = pack_name_hash_fn(name);
			entry->no_try_delta = no_try_delta(name);
		}
	} else {
		if (!want_object_in_pack(oid, 0, &pack, &offset))
			return;
		if (!pack && type == OBJ_BLOB && !has_loose_object(oid)) {
			/*
			 * If a traversed tree has a missing blob then we want
			 * to avoid adding that missing object to our pack.
			 *
			 * This only applies to missing blobs, not trees,
			 * because the traversal needs to parse sub-trees but
			 * not blobs.
			 *
			 * Note we only perform this check when we couldn't
			 * already find the object in a pack, so we're really
			 * limited to "ensure non-tip blobs which don't exist in
			 * packs do exist via loose objects". Confused?
			 */
			return;
		}

		entry = create_object_entry(oid, type, pack_name_hash_fn(name),
					    0, name && no_try_delta(name),
					    pack, offset);
	}

	if (mtime > oe_cruft_mtime(&to_pack, entry))
		oe_set_cruft_mtime(&to_pack, entry, mtime);
	return;
}

static void show_cruft_object(struct object *obj, const char *name, void *data UNUSED)
{
	/*
	 * if we did not record it earlier, it's at least as old as our
	 * expiration value. Rather than find it exactly, just use that
	 * value.  This may bump it forward from its real mtime, but it
	 * will still be "too old" next time we run with the same
	 * expiration.
	 *
	 * if obj does appear in the packing list, this call is a noop (or may
	 * set the namehash).
	 */
	add_cruft_object_entry(&obj->oid, obj->type, NULL, 0, name, cruft_expiration);
}

static void show_cruft_commit(struct commit *commit, void *data)
{
	show_cruft_object((struct object*)commit, NULL, data);
}

static int cruft_include_check_obj(struct object *obj, void *data UNUSED)
{
	return !has_object_kept_pack(&obj->oid, IN_CORE_KEEP_PACKS);
}

static int cruft_include_check(struct commit *commit, void *data)
{
	return cruft_include_check_obj((struct object*)commit, data);
}

static void set_cruft_mtime(const struct object *object,
			    struct packed_git *pack,
			    off_t offset, time_t mtime)
{
	add_cruft_object_entry(&object->oid, object->type, pack, offset, NULL,
			       mtime);
}

static void mark_pack_kept_in_core(struct string_list *packs, unsigned keep)
{
	struct string_list_item *item = NULL;
	for_each_string_list_item(item, packs) {
		struct packed_git *p = item->util;
		if (!p)
			die(_("could not find pack '%s'"), item->string);
		p->pack_keep_in_core = keep;
	}
}

static void add_unreachable_loose_objects(void);
static void add_objects_in_unpacked_packs(void);

static void enumerate_cruft_objects(void)
{
	if (progress)
		progress_state = start_progress(_("Enumerating cruft objects"), 0);

	add_objects_in_unpacked_packs();
	add_unreachable_loose_objects();

	stop_progress(&progress_state);
}

static void enumerate_and_traverse_cruft_objects(struct string_list *fresh_packs)
{
	struct packed_git *p;
	struct rev_info revs;
	int ret;

	repo_init_revisions(the_repository, &revs, NULL);

	revs.tag_objects = 1;
	revs.tree_objects = 1;
	revs.blob_objects = 1;

	revs.include_check = cruft_include_check;
	revs.include_check_obj = cruft_include_check_obj;

	revs.ignore_missing_links = 1;

	if (progress)
		progress_state = start_progress(_("Enumerating cruft objects"), 0);
	ret = add_unseen_recent_objects_to_traversal(&revs, cruft_expiration,
						     set_cruft_mtime, 1);
	stop_progress(&progress_state);

	if (ret)
		die(_("unable to add cruft objects"));

	/*
	 * Re-mark only the fresh packs as kept so that objects in
	 * unknown packs do not halt the reachability traversal early.
	 */
	for (p = get_all_packs(the_repository); p; p = p->next)
		p->pack_keep_in_core = 0;
	mark_pack_kept_in_core(fresh_packs, 1);

	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));
	if (progress)
		progress_state = start_progress(_("Traversing cruft objects"), 0);
	nr_seen = 0;
	traverse_commit_list(&revs, show_cruft_commit, show_cruft_object, NULL);

	stop_progress(&progress_state);
}

static void read_cruft_objects(void)
{
	struct strbuf buf = STRBUF_INIT;
	struct string_list discard_packs = STRING_LIST_INIT_DUP;
	struct string_list fresh_packs = STRING_LIST_INIT_DUP;
	struct packed_git *p;

	ignore_packed_keep_in_core = 1;

	while (strbuf_getline(&buf, stdin) != EOF) {
		if (!buf.len)
			continue;

		if (*buf.buf == '-')
			string_list_append(&discard_packs, buf.buf + 1);
		else
			string_list_append(&fresh_packs, buf.buf);
	}

	string_list_sort(&discard_packs);
	string_list_sort(&fresh_packs);

	for (p = get_all_packs(the_repository); p; p = p->next) {
		const char *pack_name = pack_basename(p);
		struct string_list_item *item;

		item = string_list_lookup(&fresh_packs, pack_name);
		if (!item)
			item = string_list_lookup(&discard_packs, pack_name);

		if (item) {
			item->util = p;
		} else {
			/*
			 * This pack wasn't mentioned in either the "fresh" or
			 * "discard" list, so the caller didn't know about it.
			 *
			 * Mark it as kept so that its objects are ignored by
			 * add_unseen_recent_objects_to_traversal(). We'll
			 * unmark it before starting the traversal so it doesn't
			 * halt the traversal early.
			 */
			p->pack_keep_in_core = 1;
		}
	}

	mark_pack_kept_in_core(&fresh_packs, 1);
	mark_pack_kept_in_core(&discard_packs, 0);

	if (cruft_expiration)
		enumerate_and_traverse_cruft_objects(&fresh_packs);
	else
		enumerate_cruft_objects();

	strbuf_release(&buf);
	string_list_clear(&discard_packs, 0);
	string_list_clear(&fresh_packs, 0);
}

static void read_object_list_from_stdin(void)
{
	char line[GIT_MAX_HEXSZ + 1 + PATH_MAX + 2];
	struct object_id oid;
	const char *p;

	for (;;) {
		if (!fgets(line, sizeof(line), stdin)) {
			if (feof(stdin))
				break;
			if (!ferror(stdin))
				BUG("fgets returned NULL, not EOF, not error!");
			if (errno != EINTR)
				die_errno("fgets");
			clearerr(stdin);
			continue;
		}
		if (line[0] == '-') {
			if (get_oid_hex(line+1, &oid))
				die(_("expected edge object ID, got garbage:\n %s"),
				    line);
			add_preferred_base(&oid);
			continue;
		}
		if (parse_oid_hex(line, &oid, &p))
			die(_("expected object ID, got garbage:\n %s"), line);

		add_preferred_base_object(p + 1);
		add_object_entry(&oid, OBJ_NONE, p + 1, 0);
	}
}

static void show_commit(struct commit *commit, void *data UNUSED)
{
	add_object_entry(&commit->object.oid, OBJ_COMMIT, NULL, 0);

	if (write_bitmap_index)
		index_commit_for_bitmap(commit);

	if (use_delta_islands)
		propagate_island_marks(commit);
}

static void show_object(struct object *obj, const char *name,
			void *data UNUSED)
{
	add_preferred_base_object(name);
	add_object_entry(&obj->oid, obj->type, name, 0);

	if (use_delta_islands) {
		const char *p;
		unsigned depth;
		struct object_entry *ent;

		/* the empty string is a root tree, which is depth 0 */
		depth = *name ? 1 : 0;
		for (p = strchr(name, '/'); p; p = strchr(p + 1, '/'))
			depth++;

		ent = packlist_find(&to_pack, &obj->oid);
		if (ent && depth > oe_tree_depth(&to_pack, ent))
			oe_set_tree_depth(&to_pack, ent, depth);
	}
}

static void show_object__ma_allow_any(struct object *obj, const char *name, void *data)
{
	assert(arg_missing_action == MA_ALLOW_ANY);

	/*
	 * Quietly ignore ALL missing objects.  This avoids problems with
	 * staging them now and getting an odd error later.
	 */
	if (!has_object(the_repository, &obj->oid, 0))
		return;

	show_object(obj, name, data);
}

static void show_object__ma_allow_promisor(struct object *obj, const char *name, void *data)
{
	assert(arg_missing_action == MA_ALLOW_PROMISOR);

	/*
	 * Quietly ignore EXPECTED missing objects.  This avoids problems with
	 * staging them now and getting an odd error later.
	 */
	if (!has_object(the_repository, &obj->oid, 0) && is_promisor_object(&obj->oid))
		return;

	show_object(obj, name, data);
}

static int option_parse_missing_action(const struct option *opt UNUSED,
				       const char *arg, int unset)
{
	assert(arg);
	assert(!unset);

	if (!strcmp(arg, "error")) {
		arg_missing_action = MA_ERROR;
		fn_show_object = show_object;
		return 0;
	}

	if (!strcmp(arg, "allow-any")) {
		arg_missing_action = MA_ALLOW_ANY;
		fetch_if_missing = 0;
		fn_show_object = show_object__ma_allow_any;
		return 0;
	}

	if (!strcmp(arg, "allow-promisor")) {
		arg_missing_action = MA_ALLOW_PROMISOR;
		fetch_if_missing = 0;
		fn_show_object = show_object__ma_allow_promisor;
		return 0;
	}

	die(_("invalid value for '%s': '%s'"), "--missing", arg);
	return 0;
}

static void show_edge(struct commit *commit)
{
	add_preferred_base(&commit->object.oid);
}

static int add_object_in_unpacked_pack(const struct object_id *oid,
				       struct packed_git *pack,
				       uint32_t pos,
				       void *data UNUSED)
{
	if (cruft) {
		off_t offset;
		time_t mtime;

		if (pack->is_cruft) {
			if (load_pack_mtimes(pack) < 0)
				die(_("could not load cruft pack .mtimes"));
			mtime = nth_packed_mtime(pack, pos);
		} else {
			mtime = pack->mtime;
		}
		offset = nth_packed_object_offset(pack, pos);

		add_cruft_object_entry(oid, OBJ_NONE, pack, offset,
				       NULL, mtime);
	} else {
		add_object_entry(oid, OBJ_NONE, "", 0);
	}
	return 0;
}

static void add_objects_in_unpacked_packs(void)
{
	if (for_each_packed_object(add_object_in_unpacked_pack, NULL,
				   FOR_EACH_OBJECT_PACK_ORDER |
				   FOR_EACH_OBJECT_LOCAL_ONLY |
				   FOR_EACH_OBJECT_SKIP_IN_CORE_KEPT_PACKS |
				   FOR_EACH_OBJECT_SKIP_ON_DISK_KEPT_PACKS))
		die(_("cannot open pack index"));
}

static int add_loose_object(const struct object_id *oid, const char *path,
			    void *data UNUSED)
{
	enum object_type type = oid_object_info(the_repository, oid, NULL);

	if (type < 0) {
		warning(_("loose object at %s could not be examined"), path);
		return 0;
	}

	if (cruft) {
		struct stat st;
		if (stat(path, &st) < 0) {
			if (errno == ENOENT)
				return 0;
			return error_errno("unable to stat %s", oid_to_hex(oid));
		}

		add_cruft_object_entry(oid, type, NULL, 0, NULL,
				       st.st_mtime);
	} else {
		add_object_entry(oid, type, "", 0);
	}
	return 0;
}

/*
 * We actually don't even have to worry about reachability here.
 * add_object_entry will weed out duplicates, so we just add every
 * loose object we find.
 */
static void add_unreachable_loose_objects(void)
{
	for_each_loose_file_in_objdir(repo_get_object_directory(the_repository),
				      add_loose_object,
				      NULL, NULL, NULL);
}

static int has_sha1_pack_kept_or_nonlocal(const struct object_id *oid)
{
	static struct packed_git *last_found = (void *)1;
	struct packed_git *p;

	p = (last_found != (void *)1) ? last_found :
					get_all_packs(the_repository);

	while (p) {
		if ((!p->pack_local || p->pack_keep ||
				p->pack_keep_in_core) &&
			find_pack_entry_one(oid->hash, p)) {
			last_found = p;
			return 1;
		}
		if (p == last_found)
			p = get_all_packs(the_repository);
		else
			p = p->next;
		if (p == last_found)
			p = p->next;
	}
	return 0;
}

/*
 * Store a list of sha1s that are should not be discarded
 * because they are either written too recently, or are
 * reachable from another object that was.
 *
 * This is filled by get_object_list.
 */
static struct oid_array recent_objects;

static int loosened_object_can_be_discarded(const struct object_id *oid,
					    timestamp_t mtime)
{
	if (!unpack_unreachable_expiration)
		return 0;
	if (mtime > unpack_unreachable_expiration)
		return 0;
	if (oid_array_lookup(&recent_objects, oid) >= 0)
		return 0;
	return 1;
}

static void loosen_unused_packed_objects(void)
{
	struct packed_git *p;
	uint32_t i;
	uint32_t loosened_objects_nr = 0;
	struct object_id oid;

	for (p = get_all_packs(the_repository); p; p = p->next) {
		if (!p->pack_local || p->pack_keep || p->pack_keep_in_core)
			continue;

		if (open_pack_index(p))
			die(_("cannot open pack index"));

		for (i = 0; i < p->num_objects; i++) {
			nth_packed_object_id(&oid, p, i);
			if (!packlist_find(&to_pack, &oid) &&
			    !has_sha1_pack_kept_or_nonlocal(&oid) &&
			    !loosened_object_can_be_discarded(&oid, p->mtime)) {
				if (force_object_loose(&oid, p->mtime))
					die(_("unable to force loose object"));
				loosened_objects_nr++;
			}
		}
	}

	trace2_data_intmax("pack-objects", the_repository,
			   "loosen_unused_packed_objects/loosened", loosened_objects_nr);
}

/*
 * This tracks any options which pack-reuse code expects to be on, or which a
 * reader of the pack might not understand, and which would therefore prevent
 * blind reuse of what we have on disk.
 */
static int pack_options_allow_reuse(void)
{
	return allow_pack_reuse != NO_PACK_REUSE &&
	       pack_to_stdout &&
	       !ignore_packed_keep_on_disk &&
	       !ignore_packed_keep_in_core &&
	       (!local || !have_non_local_packs) &&
	       !incremental;
}

static int get_object_list_from_bitmap(struct rev_info *revs)
{
	if (!(bitmap_git = prepare_bitmap_walk(revs, 0)))
		return -1;

	if (pack_options_allow_reuse())
		reuse_partial_packfile_from_bitmap(bitmap_git,
						   &reuse_packfiles,
						   &reuse_packfiles_nr,
						   &reuse_packfile_bitmap,
						   allow_pack_reuse == MULTI_PACK_REUSE);

	if (reuse_packfiles) {
		reuse_packfile_objects = bitmap_popcount(reuse_packfile_bitmap);
		if (!reuse_packfile_objects)
			BUG("expected non-empty reuse bitmap");

		nr_result += reuse_packfile_objects;
		nr_seen += reuse_packfile_objects;
		display_progress(progress_state, nr_seen);
	}

	traverse_bitmap_commit_list(bitmap_git, revs,
				    &add_object_entry_from_bitmap);
	return 0;
}

static void record_recent_object(struct object *obj,
				 const char *name UNUSED,
				 void *data UNUSED)
{
	oid_array_append(&recent_objects, &obj->oid);
}

static void record_recent_commit(struct commit *commit, void *data UNUSED)
{
	oid_array_append(&recent_objects, &commit->object.oid);
}

static int mark_bitmap_preferred_tip(const char *refname,
				     const char *referent UNUSED,
				     const struct object_id *oid,
				     int flags UNUSED,
				     void *data UNUSED)
{
	struct object_id peeled;
	struct object *object;

	if (!peel_iterated_oid(the_repository, oid, &peeled))
		oid = &peeled;

	object = parse_object_or_die(oid, refname);
	if (object->type == OBJ_COMMIT)
		object->flags |= NEEDS_BITMAP;

	return 0;
}

static void mark_bitmap_preferred_tips(void)
{
	struct string_list_item *item;
	const struct string_list *preferred_tips;

	preferred_tips = bitmap_preferred_tips(the_repository);
	if (!preferred_tips)
		return;

	for_each_string_list_item(item, preferred_tips) {
		refs_for_each_ref_in(get_main_ref_store(the_repository),
				     item->string, mark_bitmap_preferred_tip,
				     NULL);
	}
}

static inline int is_oid_interesting(struct repository *repo,
				     struct object_id *oid)
{
	struct object *o = lookup_object(repo, oid);
	return o && !(o->flags & UNINTERESTING);
}

static int add_objects_by_path(const char *path,
			       struct oid_array *oids,
			       enum object_type type,
			       void *data)
{
	struct object_entry **delta_list;
	size_t oe_start = to_pack.nr_objects;
	size_t oe_end;
	unsigned int sub_list_size;
	unsigned int *processed = data;

	/*
	 * First, add all objects to the packing data, including the ones
	 * marked UNINTERESTING (translated to 'exclude') as they can be
	 * used as delta bases.
	 */
	for (size_t i = 0; i < oids->nr; i++) {
		int exclude;
		struct object_info oi = OBJECT_INFO_INIT;
		struct object_id *oid = &oids->oid[i];

		/* Skip objects that do not exist locally. */
		if ((exclude_promisor_objects || arg_missing_action != MA_ERROR) &&
		    oid_object_info_extended(the_repository, oid, &oi,
					     OBJECT_INFO_FOR_PREFETCH) < 0)
			continue;

		exclude = !is_oid_interesting(the_repository, oid);

		if (exclude && !thin)
			continue;

		add_object_entry(oid, type, path, exclude);
	}

	oe_end = to_pack.nr_objects;

	/* We can skip delta calculations if it is a no-op. */
	if (oe_end == oe_start || !window)
		return 0;

	sub_list_size = 0;
	ALLOC_ARRAY(delta_list, oe_end - oe_start);

	for (size_t i = 0; i < oe_end - oe_start; i++) {
		struct object_entry *entry = to_pack.objects + oe_start + i;

		if (!should_attempt_deltas(entry))
			continue;

		delta_list[sub_list_size++] = entry;
	}

	/*
	 * Find delta bases among this list of objects that all match the same
	 * path. This causes the delta compression to be interleaved in the
	 * object walk, which can lead to confusing progress indicators. This is
	 * also incompatible with threaded delta calculations. In the future,
	 * consider creating a list of regions in the full to_pack.objects array
	 * that could be picked up by the threaded delta computation.
	 */
	if (sub_list_size && window) {
		QSORT(delta_list, sub_list_size, type_size_sort);
		find_deltas(delta_list, &sub_list_size, window, depth, processed);
	}

	free(delta_list);
	return 0;
}

static void get_object_list_path_walk(struct rev_info *revs)
{
	struct path_walk_info info = PATH_WALK_INFO_INIT;
	unsigned int processed = 0;

	info.revs = revs;
	info.path_fn = add_objects_by_path;
	info.path_fn_data = &processed;
	revs->tag_objects = 1;

	/*
	 * Allow the --[no-]sparse option to be interesting here, if only
	 * for testing purposes. Paths with no interesting objects will not
	 * contribute to the resulting pack, but only create noisy preferred
	 * base objects.
	 */
	info.prune_all_uninteresting = sparse;

	if (walk_objects_by_path(&info))
		die(_("failed to pack objects via path-walk"));
}

static void get_object_list(struct rev_info *revs, int ac, const char **av)
{
	struct setup_revision_opt s_r_opt = {
		.allow_exclude_promisor_objects = 1,
	};
	char line[1000];
	int flags = 0;
	int save_warning;

	save_commit_buffer = 0;
	setup_revisions(ac, av, revs, &s_r_opt);

	/* make sure shallows are read */
	is_repository_shallow(the_repository);

	save_warning = warn_on_object_refname_ambiguity;
	warn_on_object_refname_ambiguity = 0;

	while (fgets(line, sizeof(line), stdin) != NULL) {
		int len = strlen(line);
		if (len && line[len - 1] == '\n')
			line[--len] = 0;
		if (!len)
			break;
		if (*line == '-') {
			if (!strcmp(line, "--not")) {
				flags ^= UNINTERESTING;
				write_bitmap_index = 0;
				continue;
			}
			if (starts_with(line, "--shallow ")) {
				struct object_id oid;
				if (get_oid_hex(line + 10, &oid))
					die("not an object name '%s'", line + 10);
				register_shallow(the_repository, &oid);
				use_bitmap_index = 0;
				continue;
			}
			die(_("not a rev '%s'"), line);
		}
		if (handle_revision_arg(line, revs, flags, REVARG_CANNOT_BE_FILENAME))
			die(_("bad revision '%s'"), line);
	}

	warn_on_object_refname_ambiguity = save_warning;

	if (use_bitmap_index && !path_walk && !get_object_list_from_bitmap(revs))
		return;

	if (use_delta_islands)
		load_delta_islands(the_repository, progress);

	if (write_bitmap_index)
		mark_bitmap_preferred_tips();

	if (!fn_show_object)
		fn_show_object = show_object;

	if (path_walk) {
		get_object_list_path_walk(revs);
	} else {
		if (prepare_revision_walk(revs))
			die(_("revision walk setup failed"));
		mark_edges_uninteresting(revs, show_edge, sparse);
		traverse_commit_list(revs,
				show_commit, fn_show_object,
				NULL);
	}

	if (unpack_unreachable_expiration) {
		revs->ignore_missing_links = 1;
		if (add_unseen_recent_objects_to_traversal(revs,
				unpack_unreachable_expiration, NULL, 0))
			die(_("unable to add recent objects"));
		if (prepare_revision_walk(revs))
			die(_("revision walk setup failed"));
		traverse_commit_list(revs, record_recent_commit,
				     record_recent_object, NULL);
	}

	if (keep_unreachable)
		add_objects_in_unpacked_packs();
	if (pack_loose_unreachable)
		add_unreachable_loose_objects();
	if (unpack_unreachable)
		loosen_unused_packed_objects();

	oid_array_clear(&recent_objects);
}

static void add_extra_kept_packs(const struct string_list *names)
{
	struct packed_git *p;

	if (!names->nr)
		return;

	for (p = get_all_packs(the_repository); p; p = p->next) {
		const char *name = basename(p->pack_name);
		int i;

		if (!p->pack_local)
			continue;

		for (i = 0; i < names->nr; i++)
			if (!fspathcmp(name, names->items[i].string))
				break;

		if (i < names->nr) {
			p->pack_keep_in_core = 1;
			ignore_packed_keep_in_core = 1;
			continue;
		}
	}
}

static int option_parse_quiet(const struct option *opt, const char *arg,
			      int unset)
{
	int *val = opt->value;

	BUG_ON_OPT_ARG(arg);

	if (!unset)
		*val = 0;
	else if (!*val)
		*val = 1;
	return 0;
}

static int option_parse_index_version(const struct option *opt,
				      const char *arg, int unset)
{
	struct pack_idx_option *popts = opt->value;
	char *c;
	const char *val = arg;

	BUG_ON_OPT_NEG(unset);

	popts->version = strtoul(val, &c, 10);
	if (popts->version > 2)
		die(_("unsupported index version %s"), val);
	if (*c == ',' && c[1])
		popts->off32_limit = strtoul(c+1, &c, 0);
	if (*c || popts->off32_limit & 0x80000000)
		die(_("bad index version '%s'"), val);
	return 0;
}

static int option_parse_unpack_unreachable(const struct option *opt UNUSED,
					   const char *arg, int unset)
{
	if (unset) {
		unpack_unreachable = 0;
		unpack_unreachable_expiration = 0;
	}
	else {
		unpack_unreachable = 1;
		if (arg)
			unpack_unreachable_expiration = approxidate(arg);
	}
	return 0;
}

static int option_parse_cruft_expiration(const struct option *opt UNUSED,
					 const char *arg, int unset)
{
	if (unset) {
		cruft = 0;
		cruft_expiration = 0;
	} else {
		cruft = 1;
		if (arg)
			cruft_expiration = approxidate(arg);
	}
	return 0;
}

int cmd_pack_objects(int argc,
		     const char **argv,
		     const char *prefix,
		     struct repository *repo UNUSED)
{
	int use_internal_rev_list = 0;
	int shallow = 0;
	int all_progress_implied = 0;
	struct strvec rp = STRVEC_INIT;
	int rev_list_unpacked = 0, rev_list_all = 0, rev_list_reflog = 0;
	int rev_list_index = 0;
	int stdin_packs = 0;
	struct string_list keep_pack_list = STRING_LIST_INIT_NODUP;
	struct list_objects_filter_options filter_options =
		LIST_OBJECTS_FILTER_INIT;

	struct option pack_objects_options[] = {
		OPT_CALLBACK_F('q', "quiet", &progress, NULL,
			       N_("do not show progress meter"),
			       PARSE_OPT_NOARG, option_parse_quiet),
		OPT_SET_INT(0, "progress", &progress,
			    N_("show progress meter"), 1),
		OPT_SET_INT(0, "all-progress", &progress,
			    N_("show progress meter during object writing phase"), 2),
		OPT_BOOL(0, "all-progress-implied",
			 &all_progress_implied,
			 N_("similar to --all-progress when progress meter is shown")),
		OPT_CALLBACK_F(0, "index-version", &pack_idx_opts, N_("<version>[,<offset>]"),
		  N_("write the pack index file in the specified idx format version"),
		  PARSE_OPT_NONEG, option_parse_index_version),
		OPT_MAGNITUDE(0, "max-pack-size", &pack_size_limit,
			      N_("maximum size of each output pack file")),
		OPT_BOOL(0, "local", &local,
			 N_("ignore borrowed objects from alternate object store")),
		OPT_BOOL(0, "incremental", &incremental,
			 N_("ignore packed objects")),
		OPT_INTEGER(0, "window", &window,
			    N_("limit pack window by objects")),
		OPT_MAGNITUDE(0, "window-memory", &window_memory_limit,
			      N_("limit pack window by memory in addition to object limit")),
		OPT_INTEGER(0, "depth", &depth,
			    N_("maximum length of delta chain allowed in the resulting pack")),
		OPT_BOOL(0, "reuse-delta", &reuse_delta,
			 N_("reuse existing deltas")),
		OPT_BOOL(0, "reuse-object", &reuse_object,
			 N_("reuse existing objects")),
		OPT_BOOL(0, "delta-base-offset", &allow_ofs_delta,
			 N_("use OFS_DELTA objects")),
		OPT_INTEGER(0, "threads", &delta_search_threads,
			    N_("use threads when searching for best delta matches")),
		OPT_BOOL(0, "non-empty", &non_empty,
			 N_("do not create an empty pack output")),
		OPT_BOOL(0, "revs", &use_internal_rev_list,
			 N_("read revision arguments from standard input")),
		OPT_SET_INT_F(0, "unpacked", &rev_list_unpacked,
			      N_("limit the objects to those that are not yet packed"),
			      1, PARSE_OPT_NONEG),
		OPT_SET_INT_F(0, "all", &rev_list_all,
			      N_("include objects reachable from any reference"),
			      1, PARSE_OPT_NONEG),
		OPT_SET_INT_F(0, "reflog", &rev_list_reflog,
			      N_("include objects referred by reflog entries"),
			      1, PARSE_OPT_NONEG),
		OPT_SET_INT_F(0, "indexed-objects", &rev_list_index,
			      N_("include objects referred to by the index"),
			      1, PARSE_OPT_NONEG),
		OPT_BOOL(0, "stdin-packs", &stdin_packs,
			 N_("read packs from stdin")),
		OPT_BOOL(0, "stdout", &pack_to_stdout,
			 N_("output pack to stdout")),
		OPT_BOOL(0, "include-tag", &include_tag,
			 N_("include tag objects that refer to objects to be packed")),
		OPT_BOOL(0, "keep-unreachable", &keep_unreachable,
			 N_("keep unreachable objects")),
		OPT_BOOL(0, "pack-loose-unreachable", &pack_loose_unreachable,
			 N_("pack loose unreachable objects")),
		OPT_CALLBACK_F(0, "unpack-unreachable", NULL, N_("time"),
		  N_("unpack unreachable objects newer than <time>"),
		  PARSE_OPT_OPTARG, option_parse_unpack_unreachable),
		OPT_BOOL(0, "cruft", &cruft, N_("create a cruft pack")),
		OPT_CALLBACK_F(0, "cruft-expiration", NULL, N_("time"),
		  N_("expire cruft objects older than <time>"),
		  PARSE_OPT_OPTARG, option_parse_cruft_expiration),
		OPT_BOOL(0, "sparse", &sparse,
			 N_("use the sparse reachability algorithm")),
		OPT_BOOL(0, "thin", &thin,
			 N_("create thin packs")),
		OPT_BOOL(0, "path-walk", &path_walk,
			 N_("use the path-walk API to walk objects when possible")),
		OPT_BOOL(0, "shallow", &shallow,
			 N_("create packs suitable for shallow fetches")),
		OPT_BOOL(0, "honor-pack-keep", &ignore_packed_keep_on_disk,
			 N_("ignore packs that have companion .keep file")),
		OPT_STRING_LIST(0, "keep-pack", &keep_pack_list, N_("name"),
				N_("ignore this pack")),
		OPT_INTEGER(0, "compression", &pack_compression_level,
			    N_("pack compression level")),
		OPT_BOOL(0, "keep-true-parents", &grafts_keep_true_parents,
			 N_("do not hide commits by grafts")),
		OPT_BOOL(0, "use-bitmap-index", &use_bitmap_index,
			 N_("use a bitmap index if available to speed up counting objects")),
		OPT_SET_INT(0, "write-bitmap-index", &write_bitmap_index,
			    N_("write a bitmap index together with the pack index"),
			    WRITE_BITMAP_TRUE),
		OPT_SET_INT_F(0, "write-bitmap-index-quiet",
			      &write_bitmap_index,
			      N_("write a bitmap index if possible"),
			      WRITE_BITMAP_QUIET, PARSE_OPT_HIDDEN),
		OPT_PARSE_LIST_OBJECTS_FILTER(&filter_options),
		OPT_CALLBACK_F(0, "missing", NULL, N_("action"),
		  N_("handling for missing objects"), PARSE_OPT_NONEG,
		  option_parse_missing_action),
		OPT_BOOL(0, "exclude-promisor-objects", &exclude_promisor_objects,
			 N_("do not pack objects in promisor packfiles")),
		OPT_BOOL(0, "delta-islands", &use_delta_islands,
			 N_("respect islands during delta compression")),
		OPT_STRING_LIST(0, "uri-protocol", &uri_protocols,
				N_("protocol"),
				N_("exclude any configured uploadpack.blobpackfileuri with this protocol")),
		OPT_BOOL(0, "full-name-hash", &use_full_name_hash,
			 N_("(EXPERIMENTAL!) optimize delta compression across identical path names over time")),
		OPT_END(),
	};

	if (DFS_NUM_STATES > (1 << OE_DFS_STATE_BITS))
		BUG("too many dfs states, increase OE_DFS_STATE_BITS");

	disable_replace_refs();

	sparse = git_env_bool("GIT_TEST_PACK_SPARSE", -1);
	if (the_repository->gitdir) {
		prepare_repo_settings(the_repository);
		if (sparse < 0)
			sparse = the_repository->settings.pack_use_sparse;
		if (the_repository->settings.pack_use_multi_pack_reuse)
			allow_pack_reuse = MULTI_PACK_REUSE;
	}

	reset_pack_idx_option(&pack_idx_opts);
	pack_idx_opts.flags |= WRITE_REV;
	git_config(git_pack_config, NULL);
	if (git_env_bool(GIT_TEST_NO_WRITE_REV_INDEX, 0))
		pack_idx_opts.flags &= ~WRITE_REV;

	progress = isatty(2);
	argc = parse_options(argc, argv, prefix, pack_objects_options,
			     pack_usage, 0);

	if (argc) {
		base_name = argv[0];
		argc--;
	}
	if (pack_to_stdout != !base_name || argc)
		usage_with_options(pack_usage, pack_objects_options);

	if (path_walk < 0) {
		if (use_bitmap_index > 0 ||
		    !use_internal_rev_list)
			path_walk = 0;
		else
			path_walk = git_env_bool("GIT_TEST_PACK_PATH_WALK", 0);
	}

	if (depth < 0)
		depth = 0;
	if (depth >= (1 << OE_DEPTH_BITS)) {
		warning(_("delta chain depth %d is too deep, forcing %d"),
			depth, (1 << OE_DEPTH_BITS) - 1);
		depth = (1 << OE_DEPTH_BITS) - 1;
	}
	if (cache_max_small_delta_size >= (1U << OE_Z_DELTA_BITS)) {
		warning(_("pack.deltaCacheLimit is too high, forcing %d"),
			(1U << OE_Z_DELTA_BITS) - 1);
		cache_max_small_delta_size = (1U << OE_Z_DELTA_BITS) - 1;
	}
	if (window < 0)
		window = 0;

	strvec_push(&rp, "pack-objects");

	if (path_walk && filter_options.choice) {
		warning(_("cannot use --filter with --path-walk"));
		path_walk = 0;
	}
	if (path_walk && use_delta_islands) {
		warning(_("cannot use delta islands with --path-walk"));
		path_walk = 0;
	}
	if (path_walk && shallow) {
		warning(_("cannot use --shallow with --path-walk"));
		path_walk = 0;
	}
	if (path_walk) {
		strvec_push(&rp, "--boundary");
		 /*
		  * We must disable the bitmaps because we are removing
		  * the --objects / --objects-edge[-aggressive] options.
		  */
		use_bitmap_index = 0;
	} else if (thin) {
		use_internal_rev_list = 1;
		strvec_push(&rp, shallow
				? "--objects-edge-aggressive"
				: "--objects-edge");
	} else
		strvec_push(&rp, "--objects");

	if (rev_list_all) {
		use_internal_rev_list = 1;
		strvec_push(&rp, "--all");
	}
	if (rev_list_reflog) {
		use_internal_rev_list = 1;
		strvec_push(&rp, "--reflog");
	}
	if (rev_list_index) {
		use_internal_rev_list = 1;
		strvec_push(&rp, "--indexed-objects");
	}
	if (rev_list_unpacked && !stdin_packs) {
		use_internal_rev_list = 1;
		strvec_push(&rp, "--unpacked");
	}

	if (exclude_promisor_objects) {
		use_internal_rev_list = 1;
		fetch_if_missing = 0;
		strvec_push(&rp, "--exclude-promisor-objects");
	}
	if (unpack_unreachable || keep_unreachable || pack_loose_unreachable)
		use_internal_rev_list = 1;

	if (!reuse_object)
		reuse_delta = 0;
	if (pack_compression_level == -1)
		pack_compression_level = Z_DEFAULT_COMPRESSION;
	else if (pack_compression_level < 0 || pack_compression_level > Z_BEST_COMPRESSION)
		die(_("bad pack compression level %d"), pack_compression_level);

	if (!delta_search_threads)	/* --threads=0 means autodetect */
		delta_search_threads = online_cpus();

	if (!HAVE_THREADS && delta_search_threads != 1)
		warning(_("no threads support, ignoring --threads"));
	if (!pack_to_stdout && !pack_size_limit)
		pack_size_limit = pack_size_limit_cfg;
	if (pack_to_stdout && pack_size_limit)
		die(_("--max-pack-size cannot be used to build a pack for transfer"));
	if (pack_size_limit && pack_size_limit < 1024*1024) {
		warning(_("minimum pack size limit is 1 MiB"));
		pack_size_limit = 1024*1024;
	}

	if (!pack_to_stdout && thin)
		die(_("--thin cannot be used to build an indexable pack"));

	if (keep_unreachable && unpack_unreachable)
		die(_("options '%s' and '%s' cannot be used together"), "--keep-unreachable", "--unpack-unreachable");
	if (!rev_list_all || !rev_list_reflog || !rev_list_index)
		unpack_unreachable_expiration = 0;

	if (stdin_packs && filter_options.choice)
		die(_("cannot use --filter with --stdin-packs"));

	if (stdin_packs && use_internal_rev_list)
		die(_("cannot use internal rev list with --stdin-packs"));

	if (cruft) {
		if (use_internal_rev_list)
			die(_("cannot use internal rev list with --cruft"));
		if (stdin_packs)
			die(_("cannot use --stdin-packs with --cruft"));
	}

	/*
	 * "soft" reasons not to use bitmaps - for on-disk repack by default we want
	 *
	 * - to produce good pack (with bitmap index not-yet-packed objects are
	 *   packed in suboptimal order).
	 *
	 * - to use more robust pack-generation codepath (avoiding possible
	 *   bugs in bitmap code and possible bitmap index corruption).
	 */
	if (!pack_to_stdout)
		use_bitmap_index_default = 0;

	if (use_bitmap_index < 0)
		use_bitmap_index = use_bitmap_index_default;

	/* "hard" reasons not to use bitmaps; these just won't work at all */
	if (!use_internal_rev_list || (!pack_to_stdout && write_bitmap_index) || is_repository_shallow(the_repository))
		use_bitmap_index = 0;

	if (pack_to_stdout || !rev_list_all)
		write_bitmap_index = 0;

	if (write_bitmap_index && use_full_name_hash > 0)
		die(_("currently, the --full-name-hash option is incompatible with --write-bitmap-index"));
	if (use_full_name_hash < 0)
		use_full_name_hash = git_env_bool("GIT_TEST_FULL_NAME_HASH", 0);

	if (use_delta_islands)
		strvec_push(&rp, "--topo-order");

	if (progress && all_progress_implied)
		progress = 2;

	add_extra_kept_packs(&keep_pack_list);
	if (ignore_packed_keep_on_disk) {
		struct packed_git *p;
		for (p = get_all_packs(the_repository); p; p = p->next)
			if (p->pack_local && p->pack_keep)
				break;
		if (!p) /* no keep-able packs found */
			ignore_packed_keep_on_disk = 0;
	}
	if (local) {
		/*
		 * unlike ignore_packed_keep_on_disk above, we do not
		 * want to unset "local" based on looking at packs, as
		 * it also covers non-local objects
		 */
		struct packed_git *p;
		for (p = get_all_packs(the_repository); p; p = p->next) {
			if (!p->pack_local) {
				have_non_local_packs = 1;
				break;
			}
		}
	}

	trace2_region_enter("pack-objects", "enumerate-objects",
			    the_repository);
	prepare_packing_data(the_repository, &to_pack);

	if (progress && !cruft)
		progress_state = start_progress(_("Enumerating objects"), 0);
	if (stdin_packs) {
		/* avoids adding objects in excluded packs */
		ignore_packed_keep_in_core = 1;
		read_packs_list_from_stdin();
		if (rev_list_unpacked)
			add_unreachable_loose_objects();
	} else if (cruft) {
		read_cruft_objects();
	} else if (!use_internal_rev_list) {
		read_object_list_from_stdin();
	} else {
		struct rev_info revs;

		repo_init_revisions(the_repository, &revs, NULL);
		list_objects_filter_copy(&revs.filter, &filter_options);
		get_object_list(&revs, rp.nr, rp.v);
		release_revisions(&revs);
	}
	cleanup_preferred_base();
	if (include_tag && nr_result)
		refs_for_each_tag_ref(get_main_ref_store(the_repository),
				      add_ref_tag, NULL);
	stop_progress(&progress_state);
	trace2_region_leave("pack-objects", "enumerate-objects",
			    the_repository);

	if (non_empty && !nr_result)
		goto cleanup;
	if (nr_result) {
		trace2_region_enter("pack-objects", "prepare-pack",
				    the_repository);
		prepare_pack(window, depth);
		trace2_region_leave("pack-objects", "prepare-pack",
				    the_repository);
	}

	trace2_region_enter("pack-objects", "write-pack-file", the_repository);
	write_excluded_by_configs();
	write_pack_file();
	trace2_region_leave("pack-objects", "write-pack-file", the_repository);

	if (progress)
		fprintf_ln(stderr,
			   _("Total %"PRIu32" (delta %"PRIu32"),"
			     " reused %"PRIu32" (delta %"PRIu32"),"
			     " pack-reused %"PRIu32" (from %"PRIuMAX")"),
			   written, written_delta, reused, reused_delta,
			   reuse_packfile_objects,
			   (uintmax_t)reuse_packfiles_used_nr);

	trace2_data_intmax("pack-objects", the_repository, "written", written);
	trace2_data_intmax("pack-objects", the_repository, "written/delta", written_delta);
	trace2_data_intmax("pack-objects", the_repository, "reused", reused);
	trace2_data_intmax("pack-objects", the_repository, "reused/delta", reused_delta);
	trace2_data_intmax("pack-objects", the_repository, "pack-reused", reuse_packfile_objects);
	trace2_data_intmax("pack-objects", the_repository, "packs-reused", reuse_packfiles_used_nr);

cleanup:
	clear_packing_data(&to_pack);
	list_objects_filter_release(&filter_options);
	string_list_clear(&keep_pack_list, 0);
	strvec_clear(&rp);

	return 0;
}
