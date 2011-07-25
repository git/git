#include "builtin.h"
#include "cache.h"
#include "attr.h"
#include "object.h"
#include "blob.h"
#include "commit.h"
#include "tag.h"
#include "tree.h"
#include "delta.h"
#include "pack.h"
#include "pack-revindex.h"
#include "csum-file.h"
#include "tree-walk.h"
#include "diff.h"
#include "revision.h"
#include "list-objects.h"
#include "progress.h"
#include "refs.h"
#include "thread-utils.h"

static const char pack_usage[] =
  "git pack-objects [ -q | --progress | --all-progress ]\n"
  "        [--all-progress-implied]\n"
  "        [--max-pack-size=<n>] [--local] [--incremental]\n"
  "        [--window=<n>] [--window-memory=<n>] [--depth=<n>]\n"
  "        [--no-reuse-delta] [--no-reuse-object] [--delta-base-offset]\n"
  "        [--threads=<n>] [--non-empty] [--revs [--unpacked | --all]]\n"
  "        [--reflog] [--stdout | base-name] [--include-tag]\n"
  "        [--keep-unreachable | --unpack-unreachable]\n"
  "        [< ref-list | < object-list]";

struct object_entry {
	struct pack_idx_entry idx;
	unsigned long size;	/* uncompressed size */
	struct packed_git *in_pack; 	/* already in pack */
	off_t in_pack_offset;
	struct object_entry *delta;	/* delta base object */
	struct object_entry *delta_child; /* deltified objects who bases me */
	struct object_entry *delta_sibling; /* other deltified objects who
					     * uses the same base as me
					     */
	void *delta_data;	/* cached delta (uncompressed) */
	unsigned long delta_size;	/* delta data size (uncompressed) */
	unsigned long z_delta_size;	/* delta data size (compressed) */
	unsigned int hash;	/* name hint hash */
	enum object_type type;
	enum object_type in_pack_type;	/* could be delta */
	unsigned char in_pack_header_size;
	unsigned char preferred_base; /* we do not pack this, but is available
				       * to be used as the base object to delta
				       * objects against.
				       */
	unsigned char no_try_delta;
	unsigned char tagged; /* near the very tip of refs */
	unsigned char filled; /* assigned write-order */
};

/*
 * Objects we are going to pack are collected in objects array (dynamically
 * expanded).  nr_objects & nr_alloc controls this array.  They are stored
 * in the order we see -- typically rev-list --objects order that gives us
 * nice "minimum seek" order.
 */
static struct object_entry *objects;
static struct pack_idx_entry **written_list;
static uint32_t nr_objects, nr_alloc, nr_result, nr_written;

static int non_empty;
static int reuse_delta = 1, reuse_object = 1;
static int keep_unreachable, unpack_unreachable, include_tag;
static int local;
static int incremental;
static int ignore_packed_keep;
static int allow_ofs_delta;
static struct pack_idx_option pack_idx_opts;
static const char *base_name;
static int progress = 1;
static int window = 10;
static unsigned long pack_size_limit, pack_size_limit_cfg;
static int depth = 50;
static int delta_search_threads;
static int pack_to_stdout;
static int num_preferred_base;
static struct progress *progress_state;
static int pack_compression_level = Z_DEFAULT_COMPRESSION;
static int pack_compression_seen;

static unsigned long delta_cache_size = 0;
static unsigned long max_delta_cache_size = 256 * 1024 * 1024;
static unsigned long cache_max_small_delta_size = 1000;

static unsigned long window_memory_limit = 0;

/*
 * The object names in objects array are hashed with this hashtable,
 * to help looking up the entry by object name.
 * This hashtable is built after all the objects are seen.
 */
static int *object_ix;
static int object_ix_hashsz;
static struct object_entry *locate_object_entry(const unsigned char *sha1);

/*
 * stats
 */
static uint32_t written, written_delta;
static uint32_t reused, reused_delta;


static void *get_delta(struct object_entry *entry)
{
	unsigned long size, base_size, delta_size;
	void *buf, *base_buf, *delta_buf;
	enum object_type type;

	buf = read_sha1_file(entry->idx.sha1, &type, &size);
	if (!buf)
		die("unable to read %s", sha1_to_hex(entry->idx.sha1));
	base_buf = read_sha1_file(entry->delta->idx.sha1, &type, &base_size);
	if (!base_buf)
		die("unable to read %s", sha1_to_hex(entry->delta->idx.sha1));
	delta_buf = diff_delta(base_buf, base_size,
			       buf, size, &delta_size, 0);
	if (!delta_buf || delta_size != entry->delta_size)
		die("delta size changed");
	free(buf);
	free(base_buf);
	return delta_buf;
}

static unsigned long do_compress(void **pptr, unsigned long size)
{
	git_zstream stream;
	void *in, *out;
	unsigned long maxsize;

	memset(&stream, 0, sizeof(stream));
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

static void copy_pack_data(struct sha1file *f,
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
		sha1write(f, in, avail);
		offset += avail;
		len -= avail;
	}
}

/* Return 0 if we will bust the pack-size limit */
static unsigned long write_object(struct sha1file *f,
				  struct object_entry *entry,
				  off_t write_offset)
{
	unsigned long size, limit, datalen;
	void *buf;
	unsigned char header[10], dheader[10];
	unsigned hdrlen;
	enum object_type type;
	int usable_delta, to_reuse;

	if (!pack_to_stdout)
		crc32_begin(f);

	type = entry->type;

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

	if (!entry->delta)
		usable_delta = 0;	/* no delta */
	else if (!pack_size_limit)
	       usable_delta = 1;	/* unlimited packfile */
	else if (entry->delta->idx.offset == (off_t)-1)
		usable_delta = 0;	/* base was written to another pack */
	else if (entry->delta->idx.offset)
		usable_delta = 1;	/* base already exists in this pack */
	else
		usable_delta = 0;	/* base could end up in another pack */

	if (!reuse_object)
		to_reuse = 0;	/* explicit */
	else if (!entry->in_pack)
		to_reuse = 0;	/* can't reuse what we don't have */
	else if (type == OBJ_REF_DELTA || type == OBJ_OFS_DELTA)
				/* check_object() decided it for us ... */
		to_reuse = usable_delta;
				/* ... but pack split may override that */
	else if (type != entry->in_pack_type)
		to_reuse = 0;	/* pack has delta which is unusable */
	else if (entry->delta)
		to_reuse = 0;	/* we want to pack afresh */
	else
		to_reuse = 1;	/* we have it in-pack undeltified,
				 * and we do not need to deltify it.
				 */

	if (!to_reuse) {
		no_reuse:
		if (!usable_delta) {
			buf = read_sha1_file(entry->idx.sha1, &type, &size);
			if (!buf)
				die("unable to read %s", sha1_to_hex(entry->idx.sha1));
			/*
			 * make sure no cached delta data remains from a
			 * previous attempt before a pack split occurred.
			 */
			free(entry->delta_data);
			entry->delta_data = NULL;
			entry->z_delta_size = 0;
		} else if (entry->delta_data) {
			size = entry->delta_size;
			buf = entry->delta_data;
			entry->delta_data = NULL;
			type = (allow_ofs_delta && entry->delta->idx.offset) ?
				OBJ_OFS_DELTA : OBJ_REF_DELTA;
		} else {
			buf = get_delta(entry);
			size = entry->delta_size;
			type = (allow_ofs_delta && entry->delta->idx.offset) ?
				OBJ_OFS_DELTA : OBJ_REF_DELTA;
		}

		if (entry->z_delta_size)
			datalen = entry->z_delta_size;
		else
			datalen = do_compress(&buf, size);

		/*
		 * The object header is a byte of 'type' followed by zero or
		 * more bytes of length.
		 */
		hdrlen = encode_in_pack_object_header(type, size, header);

		if (type == OBJ_OFS_DELTA) {
			/*
			 * Deltas with relative base contain an additional
			 * encoding of the relative offset for the delta
			 * base from this object's position in the pack.
			 */
			off_t ofs = entry->idx.offset - entry->delta->idx.offset;
			unsigned pos = sizeof(dheader) - 1;
			dheader[pos] = ofs & 127;
			while (ofs >>= 7)
				dheader[--pos] = 128 | (--ofs & 127);
			if (limit && hdrlen + sizeof(dheader) - pos + datalen + 20 >= limit) {
				free(buf);
				return 0;
			}
			sha1write(f, header, hdrlen);
			sha1write(f, dheader + pos, sizeof(dheader) - pos);
			hdrlen += sizeof(dheader) - pos;
		} else if (type == OBJ_REF_DELTA) {
			/*
			 * Deltas with a base reference contain
			 * an additional 20 bytes for the base sha1.
			 */
			if (limit && hdrlen + 20 + datalen + 20 >= limit) {
				free(buf);
				return 0;
			}
			sha1write(f, header, hdrlen);
			sha1write(f, entry->delta->idx.sha1, 20);
			hdrlen += 20;
		} else {
			if (limit && hdrlen + datalen + 20 >= limit) {
				free(buf);
				return 0;
			}
			sha1write(f, header, hdrlen);
		}
		sha1write(f, buf, datalen);
		free(buf);
	}
	else {
		struct packed_git *p = entry->in_pack;
		struct pack_window *w_curs = NULL;
		struct revindex_entry *revidx;
		off_t offset;

		if (entry->delta)
			type = (allow_ofs_delta && entry->delta->idx.offset) ?
				OBJ_OFS_DELTA : OBJ_REF_DELTA;
		hdrlen = encode_in_pack_object_header(type, entry->size, header);

		offset = entry->in_pack_offset;
		revidx = find_pack_revindex(p, offset);
		datalen = revidx[1].offset - offset;
		if (!pack_to_stdout && p->index_version > 1 &&
		    check_pack_crc(p, &w_curs, offset, datalen, revidx->nr)) {
			error("bad packed object CRC for %s", sha1_to_hex(entry->idx.sha1));
			unuse_pack(&w_curs);
			goto no_reuse;
		}

		offset += entry->in_pack_header_size;
		datalen -= entry->in_pack_header_size;
		if (!pack_to_stdout && p->index_version == 1 &&
		    check_pack_inflate(p, &w_curs, offset, datalen, entry->size)) {
			error("corrupt packed object for %s", sha1_to_hex(entry->idx.sha1));
			unuse_pack(&w_curs);
			goto no_reuse;
		}

		if (type == OBJ_OFS_DELTA) {
			off_t ofs = entry->idx.offset - entry->delta->idx.offset;
			unsigned pos = sizeof(dheader) - 1;
			dheader[pos] = ofs & 127;
			while (ofs >>= 7)
				dheader[--pos] = 128 | (--ofs & 127);
			if (limit && hdrlen + sizeof(dheader) - pos + datalen + 20 >= limit) {
				unuse_pack(&w_curs);
				return 0;
			}
			sha1write(f, header, hdrlen);
			sha1write(f, dheader + pos, sizeof(dheader) - pos);
			hdrlen += sizeof(dheader) - pos;
			reused_delta++;
		} else if (type == OBJ_REF_DELTA) {
			if (limit && hdrlen + 20 + datalen + 20 >= limit) {
				unuse_pack(&w_curs);
				return 0;
			}
			sha1write(f, header, hdrlen);
			sha1write(f, entry->delta->idx.sha1, 20);
			hdrlen += 20;
			reused_delta++;
		} else {
			if (limit && hdrlen + datalen + 20 >= limit) {
				unuse_pack(&w_curs);
				return 0;
			}
			sha1write(f, header, hdrlen);
		}
		copy_pack_data(f, p, &w_curs, offset, datalen);
		unuse_pack(&w_curs);
		reused++;
	}
	if (usable_delta)
		written_delta++;
	written++;
	if (!pack_to_stdout)
		entry->idx.crc32 = crc32_end(f);
	return hdrlen + datalen;
}

static int write_one(struct sha1file *f,
			       struct object_entry *e,
			       off_t *offset)
{
	unsigned long size;

	/* offset is non zero if object is written already. */
	if (e->idx.offset || e->preferred_base)
		return -1;

	/* if we are deltified, write out base object first. */
	if (e->delta && !write_one(f, e->delta, offset))
		return 0;

	e->idx.offset = *offset;
	size = write_object(f, e, *offset);
	if (!size) {
		e->idx.offset = 0;
		return 0;
	}
	written_list[nr_written++] = &e->idx;

	/* make sure off_t is sufficiently large not to wrap */
	if (signed_add_overflows(*offset, size))
		die("pack too large for current definition of off_t");
	*offset += size;
	return 1;
}

static int mark_tagged(const char *path, const unsigned char *sha1, int flag,
		       void *cb_data)
{
	unsigned char peeled[20];
	struct object_entry *entry = locate_object_entry(sha1);

	if (entry)
		entry->tagged = 1;
	if (!peel_ref(path, peeled)) {
		entry = locate_object_entry(peeled);
		if (entry)
			entry->tagged = 1;
	}
	return 0;
}

static void add_to_write_order(struct object_entry **wo,
			       int *endp,
			       struct object_entry *e)
{
	if (e->filled)
		return;
	wo[(*endp)++] = e;
	e->filled = 1;
}

static void add_descendants_to_write_order(struct object_entry **wo,
					   int *endp,
					   struct object_entry *e)
{
	struct object_entry *child;

	for (child = e->delta_child; child; child = child->delta_sibling)
		add_to_write_order(wo, endp, child);
	for (child = e->delta_child; child; child = child->delta_sibling)
		add_descendants_to_write_order(wo, endp, child);
}

static void add_family_to_write_order(struct object_entry **wo,
				      int *endp,
				      struct object_entry *e)
{
	struct object_entry *root;

	for (root = e; root->delta; root = root->delta)
		; /* nothing */
	add_to_write_order(wo, endp, root);
	add_descendants_to_write_order(wo, endp, root);
}

static struct object_entry **compute_write_order(void)
{
	int i, wo_end;

	struct object_entry **wo = xmalloc(nr_objects * sizeof(*wo));

	for (i = 0; i < nr_objects; i++) {
		objects[i].tagged = 0;
		objects[i].filled = 0;
		objects[i].delta_child = NULL;
		objects[i].delta_sibling = NULL;
	}

	/*
	 * Fully connect delta_child/delta_sibling network.
	 * Make sure delta_sibling is sorted in the original
	 * recency order.
	 */
	for (i = nr_objects - 1; 0 <= i; i--) {
		struct object_entry *e = &objects[i];
		if (!e->delta)
			continue;
		/* Mark me as the first child */
		e->delta_sibling = e->delta->delta_child;
		e->delta->delta_child = e;
	}

	/*
	 * Mark objects that are at the tip of tags.
	 */
	for_each_tag_ref(mark_tagged, NULL);

	/*
	 * Give the commits in the original recency order until
	 * we see a tagged tip.
	 */
	for (i = wo_end = 0; i < nr_objects; i++) {
		if (objects[i].tagged)
			break;
		add_to_write_order(wo, &wo_end, &objects[i]);
	}

	/*
	 * Then fill all the tagged tips.
	 */
	for (; i < nr_objects; i++) {
		if (objects[i].tagged)
			add_to_write_order(wo, &wo_end, &objects[i]);
	}

	/*
	 * And then all remaining commits and tags.
	 */
	for (i = 0; i < nr_objects; i++) {
		if (objects[i].type != OBJ_COMMIT &&
		    objects[i].type != OBJ_TAG)
			continue;
		add_to_write_order(wo, &wo_end, &objects[i]);
	}

	/*
	 * And then all the trees.
	 */
	for (i = 0; i < nr_objects; i++) {
		if (objects[i].type != OBJ_TREE)
			continue;
		add_to_write_order(wo, &wo_end, &objects[i]);
	}

	/*
	 * Finally all the rest in really tight order
	 */
	for (i = 0; i < nr_objects; i++)
		add_family_to_write_order(wo, &wo_end, &objects[i]);

	return wo;
}

static void write_pack_file(void)
{
	uint32_t i = 0, j;
	struct sha1file *f;
	off_t offset;
	struct pack_header hdr;
	uint32_t nr_remaining = nr_result;
	time_t last_mtime = 0;
	struct object_entry **write_order;

	if (progress > pack_to_stdout)
		progress_state = start_progress("Writing objects", nr_result);
	written_list = xmalloc(nr_objects * sizeof(*written_list));
	write_order = compute_write_order();

	do {
		unsigned char sha1[20];
		char *pack_tmp_name = NULL;

		if (pack_to_stdout) {
			f = sha1fd_throughput(1, "<stdout>", progress_state);
		} else {
			char tmpname[PATH_MAX];
			int fd;
			fd = odb_mkstemp(tmpname, sizeof(tmpname),
					 "pack/tmp_pack_XXXXXX");
			pack_tmp_name = xstrdup(tmpname);
			f = sha1fd(fd, pack_tmp_name);
		}

		hdr.hdr_signature = htonl(PACK_SIGNATURE);
		hdr.hdr_version = htonl(PACK_VERSION);
		hdr.hdr_entries = htonl(nr_remaining);
		sha1write(f, &hdr, sizeof(hdr));
		offset = sizeof(hdr);
		nr_written = 0;
		for (; i < nr_objects; i++) {
			struct object_entry *e = write_order[i];
			if (!write_one(f, e, &offset))
				break;
			display_progress(progress_state, written);
		}

		/*
		 * Did we write the wrong # entries in the header?
		 * If so, rewrite it like in fast-import
		 */
		if (pack_to_stdout) {
			sha1close(f, sha1, CSUM_CLOSE);
		} else if (nr_written == nr_remaining) {
			sha1close(f, sha1, CSUM_FSYNC);
		} else {
			int fd = sha1close(f, sha1, 0);
			fixup_pack_header_footer(fd, sha1, pack_tmp_name,
						 nr_written, sha1, offset);
			close(fd);
		}

		if (!pack_to_stdout) {
			struct stat st;
			const char *idx_tmp_name;
			char tmpname[PATH_MAX];

			idx_tmp_name = write_idx_file(NULL, written_list, nr_written,
						      &pack_idx_opts, sha1);

			snprintf(tmpname, sizeof(tmpname), "%s-%s.pack",
				 base_name, sha1_to_hex(sha1));
			free_pack_by_name(tmpname);
			if (adjust_shared_perm(pack_tmp_name))
				die_errno("unable to make temporary pack file readable");
			if (rename(pack_tmp_name, tmpname))
				die_errno("unable to rename temporary pack file");

			/*
			 * Packs are runtime accessed in their mtime
			 * order since newer packs are more likely to contain
			 * younger objects.  So if we are creating multiple
			 * packs then we should modify the mtime of later ones
			 * to preserve this property.
			 */
			if (stat(tmpname, &st) < 0) {
				warning("failed to stat %s: %s",
					tmpname, strerror(errno));
			} else if (!last_mtime) {
				last_mtime = st.st_mtime;
			} else {
				struct utimbuf utb;
				utb.actime = st.st_atime;
				utb.modtime = --last_mtime;
				if (utime(tmpname, &utb) < 0)
					warning("failed utime() on %s: %s",
						tmpname, strerror(errno));
			}

			snprintf(tmpname, sizeof(tmpname), "%s-%s.idx",
				 base_name, sha1_to_hex(sha1));
			if (adjust_shared_perm(idx_tmp_name))
				die_errno("unable to make temporary index file readable");
			if (rename(idx_tmp_name, tmpname))
				die_errno("unable to rename temporary index file");

			free((void *) idx_tmp_name);
			free(pack_tmp_name);
			puts(sha1_to_hex(sha1));
		}

		/* mark written objects as written to previous pack */
		for (j = 0; j < nr_written; j++) {
			written_list[j]->offset = (off_t)-1;
		}
		nr_remaining -= nr_written;
	} while (nr_remaining && i < nr_objects);

	free(written_list);
	free(write_order);
	stop_progress(&progress_state);
	if (written != nr_result)
		die("wrote %"PRIu32" objects while expecting %"PRIu32,
			written, nr_result);
}

static int locate_object_entry_hash(const unsigned char *sha1)
{
	int i;
	unsigned int ui;
	memcpy(&ui, sha1, sizeof(unsigned int));
	i = ui % object_ix_hashsz;
	while (0 < object_ix[i]) {
		if (!hashcmp(sha1, objects[object_ix[i] - 1].idx.sha1))
			return i;
		if (++i == object_ix_hashsz)
			i = 0;
	}
	return -1 - i;
}

static struct object_entry *locate_object_entry(const unsigned char *sha1)
{
	int i;

	if (!object_ix_hashsz)
		return NULL;

	i = locate_object_entry_hash(sha1);
	if (0 <= i)
		return &objects[object_ix[i]-1];
	return NULL;
}

static void rehash_objects(void)
{
	uint32_t i;
	struct object_entry *oe;

	object_ix_hashsz = nr_objects * 3;
	if (object_ix_hashsz < 1024)
		object_ix_hashsz = 1024;
	object_ix = xrealloc(object_ix, sizeof(int) * object_ix_hashsz);
	memset(object_ix, 0, sizeof(int) * object_ix_hashsz);
	for (i = 0, oe = objects; i < nr_objects; i++, oe++) {
		int ix = locate_object_entry_hash(oe->idx.sha1);
		if (0 <= ix)
			continue;
		ix = -1 - ix;
		object_ix[ix] = i + 1;
	}
}

static unsigned name_hash(const char *name)
{
	unsigned c, hash = 0;

	if (!name)
		return 0;

	/*
	 * This effectively just creates a sortable number from the
	 * last sixteen non-whitespace characters. Last characters
	 * count "most", so things that end in ".c" sort together.
	 */
	while ((c = *name++) != 0) {
		if (isspace(c))
			continue;
		hash = (hash >> 2) + (c << 24);
	}
	return hash;
}

static void setup_delta_attr_check(struct git_attr_check *check)
{
	static struct git_attr *attr_delta;

	if (!attr_delta)
		attr_delta = git_attr("delta");

	check[0].attr = attr_delta;
}

static int no_try_delta(const char *path)
{
	struct git_attr_check check[1];

	setup_delta_attr_check(check);
	if (git_checkattr(path, ARRAY_SIZE(check), check))
		return 0;
	if (ATTR_FALSE(check->value))
		return 1;
	return 0;
}

static int add_object_entry(const unsigned char *sha1, enum object_type type,
			    const char *name, int exclude)
{
	struct object_entry *entry;
	struct packed_git *p, *found_pack = NULL;
	off_t found_offset = 0;
	int ix;
	unsigned hash = name_hash(name);

	ix = nr_objects ? locate_object_entry_hash(sha1) : -1;
	if (ix >= 0) {
		if (exclude) {
			entry = objects + object_ix[ix] - 1;
			if (!entry->preferred_base)
				nr_result--;
			entry->preferred_base = 1;
		}
		return 0;
	}

	if (!exclude && local && has_loose_object_nonlocal(sha1))
		return 0;

	for (p = packed_git; p; p = p->next) {
		off_t offset = find_pack_entry_one(sha1, p);
		if (offset) {
			if (!found_pack) {
				found_offset = offset;
				found_pack = p;
			}
			if (exclude)
				break;
			if (incremental)
				return 0;
			if (local && !p->pack_local)
				return 0;
			if (ignore_packed_keep && p->pack_local && p->pack_keep)
				return 0;
		}
	}

	if (nr_objects >= nr_alloc) {
		nr_alloc = (nr_alloc  + 1024) * 3 / 2;
		objects = xrealloc(objects, nr_alloc * sizeof(*entry));
	}

	entry = objects + nr_objects++;
	memset(entry, 0, sizeof(*entry));
	hashcpy(entry->idx.sha1, sha1);
	entry->hash = hash;
	if (type)
		entry->type = type;
	if (exclude)
		entry->preferred_base = 1;
	else
		nr_result++;
	if (found_pack) {
		entry->in_pack = found_pack;
		entry->in_pack_offset = found_offset;
	}

	if (object_ix_hashsz * 3 <= nr_objects * 4)
		rehash_objects();
	else
		object_ix[-1 - ix] = nr_objects;

	display_progress(progress_state, nr_objects);

	if (name && no_try_delta(name))
		entry->no_try_delta = 1;

	return 1;
}

struct pbase_tree_cache {
	unsigned char sha1[20];
	int ref;
	int temporary;
	void *tree_data;
	unsigned long tree_size;
};

static struct pbase_tree_cache *(pbase_tree_cache[256]);
static int pbase_tree_cache_ix(const unsigned char *sha1)
{
	return sha1[0] % ARRAY_SIZE(pbase_tree_cache);
}
static int pbase_tree_cache_ix_incr(int ix)
{
	return (ix+1) % ARRAY_SIZE(pbase_tree_cache);
}

static struct pbase_tree {
	struct pbase_tree *next;
	/* This is a phony "cache" entry; we are not
	 * going to evict it nor find it through _get()
	 * mechanism -- this is for the toplevel node that
	 * would almost always change with any commit.
	 */
	struct pbase_tree_cache pcache;
} *pbase_tree;

static struct pbase_tree_cache *pbase_tree_get(const unsigned char *sha1)
{
	struct pbase_tree_cache *ent, *nent;
	void *data;
	unsigned long size;
	enum object_type type;
	int neigh;
	int my_ix = pbase_tree_cache_ix(sha1);
	int available_ix = -1;

	/* pbase-tree-cache acts as a limited hashtable.
	 * your object will be found at your index or within a few
	 * slots after that slot if it is cached.
	 */
	for (neigh = 0; neigh < 8; neigh++) {
		ent = pbase_tree_cache[my_ix];
		if (ent && !hashcmp(ent->sha1, sha1)) {
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
	data = read_sha1_file(sha1, &type, &size);
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
	hashcpy(nent->sha1, sha1);
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

static int name_cmp_len(const char *name)
{
	int i;
	for (i = 0; name[i] && name[i] != '\n' && name[i] != '/'; i++)
		;
	return i;
}

static void add_pbase_object(struct tree_desc *tree,
			     const char *name,
			     int cmplen,
			     const char *fullname)
{
	struct name_entry entry;
	int cmp;

	while (tree_entry(tree,&entry)) {
		if (S_ISGITLINK(entry.mode))
			continue;
		cmp = tree_entry_len(entry.path, entry.sha1) != cmplen ? 1 :
		      memcmp(name, entry.path, cmplen);
		if (cmp > 0)
			continue;
		if (cmp < 0)
			return;
		if (name[cmplen] != '/') {
			add_object_entry(entry.sha1,
					 object_type(entry.mode),
					 fullname, 1);
			return;
		}
		if (S_ISDIR(entry.mode)) {
			struct tree_desc sub;
			struct pbase_tree_cache *tree;
			const char *down = name+cmplen+1;
			int downlen = name_cmp_len(down);

			tree = pbase_tree_get(entry.sha1);
			if (!tree)
				return;
			init_tree_desc(&sub, tree->tree_data, tree->tree_size);

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
		int mi = (hi + lo) / 2;
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
	int pos = (!done_pbase_paths) ? -1 : done_pbase_path_pos(hash);
	if (0 <= pos)
		return 1;
	pos = -pos - 1;
	if (done_pbase_paths_alloc <= done_pbase_paths_num) {
		done_pbase_paths_alloc = alloc_nr(done_pbase_paths_alloc);
		done_pbase_paths = xrealloc(done_pbase_paths,
					    done_pbase_paths_alloc *
					    sizeof(unsigned));
	}
	done_pbase_paths_num++;
	if (pos < done_pbase_paths_num)
		memmove(done_pbase_paths + pos + 1,
			done_pbase_paths + pos,
			(done_pbase_paths_num - pos - 1) * sizeof(unsigned));
	done_pbase_paths[pos] = hash;
	return 0;
}

static void add_preferred_base_object(const char *name)
{
	struct pbase_tree *it;
	int cmplen;
	unsigned hash = name_hash(name);

	if (!num_preferred_base || check_pbase_path(hash))
		return;

	cmplen = name_cmp_len(name);
	for (it = pbase_tree; it; it = it->next) {
		if (cmplen == 0) {
			add_object_entry(it->pcache.sha1, OBJ_TREE, NULL, 1);
		}
		else {
			struct tree_desc tree;
			init_tree_desc(&tree, it->pcache.tree_data, it->pcache.tree_size);
			add_pbase_object(&tree, name, cmplen, name);
		}
	}
}

static void add_preferred_base(unsigned char *sha1)
{
	struct pbase_tree *it;
	void *data;
	unsigned long size;
	unsigned char tree_sha1[20];

	if (window <= num_preferred_base++)
		return;

	data = read_object_with_reference(sha1, tree_type, &size, tree_sha1);
	if (!data)
		return;

	for (it = pbase_tree; it; it = it->next) {
		if (!hashcmp(it->pcache.sha1, tree_sha1)) {
			free(data);
			return;
		}
	}

	it = xcalloc(1, sizeof(*it));
	it->next = pbase_tree;
	pbase_tree = it;

	hashcpy(it->pcache.sha1, tree_sha1);
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
		struct pbase_tree *this = it;
		it = this->next;
		free(this->pcache.tree_data);
		free(this);
	}

	for (i = 0; i < ARRAY_SIZE(pbase_tree_cache); i++) {
		if (!pbase_tree_cache[i])
			continue;
		free(pbase_tree_cache[i]->tree_data);
		free(pbase_tree_cache[i]);
		pbase_tree_cache[i] = NULL;
	}

	free(done_pbase_paths);
	done_pbase_paths = NULL;
	done_pbase_paths_num = done_pbase_paths_alloc = 0;
}

static void check_object(struct object_entry *entry)
{
	if (entry->in_pack) {
		struct packed_git *p = entry->in_pack;
		struct pack_window *w_curs = NULL;
		const unsigned char *base_ref = NULL;
		struct object_entry *base_entry;
		unsigned long used, used_0;
		unsigned long avail;
		off_t ofs;
		unsigned char *buf, c;

		buf = use_pack(p, &w_curs, entry->in_pack_offset, &avail);

		/*
		 * We want in_pack_type even if we do not reuse delta
		 * since non-delta representations could still be reused.
		 */
		used = unpack_object_header_buffer(buf, avail,
						   &entry->in_pack_type,
						   &entry->size);
		if (used == 0)
			goto give_up;

		/*
		 * Determine if this is a delta and if so whether we can
		 * reuse it or not.  Otherwise let's find out as cheaply as
		 * possible what the actual type and size for this object is.
		 */
		switch (entry->in_pack_type) {
		default:
			/* Not a delta hence we've already got all we need. */
			entry->type = entry->in_pack_type;
			entry->in_pack_header_size = used;
			if (entry->type < OBJ_COMMIT || entry->type > OBJ_BLOB)
				goto give_up;
			unuse_pack(&w_curs);
			return;
		case OBJ_REF_DELTA:
			if (reuse_delta && !entry->preferred_base)
				base_ref = use_pack(p, &w_curs,
						entry->in_pack_offset + used, NULL);
			entry->in_pack_header_size = used + 20;
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
					error("delta base offset overflow in pack for %s",
					      sha1_to_hex(entry->idx.sha1));
					goto give_up;
				}
				c = buf[used_0++];
				ofs = (ofs << 7) + (c & 127);
			}
			ofs = entry->in_pack_offset - ofs;
			if (ofs <= 0 || ofs >= entry->in_pack_offset) {
				error("delta base offset out of bound for %s",
				      sha1_to_hex(entry->idx.sha1));
				goto give_up;
			}
			if (reuse_delta && !entry->preferred_base) {
				struct revindex_entry *revidx;
				revidx = find_pack_revindex(p, ofs);
				if (!revidx)
					goto give_up;
				base_ref = nth_packed_object_sha1(p, revidx->nr);
			}
			entry->in_pack_header_size = used + used_0;
			break;
		}

		if (base_ref && (base_entry = locate_object_entry(base_ref))) {
			/*
			 * If base_ref was set above that means we wish to
			 * reuse delta data, and we even found that base
			 * in the list of objects we want to pack. Goodie!
			 *
			 * Depth value does not matter - find_deltas() will
			 * never consider reused delta as the base object to
			 * deltify other objects against, in order to avoid
			 * circular deltas.
			 */
			entry->type = entry->in_pack_type;
			entry->delta = base_entry;
			entry->delta_size = entry->size;
			entry->delta_sibling = base_entry->delta_child;
			base_entry->delta_child = entry;
			unuse_pack(&w_curs);
			return;
		}

		if (entry->type) {
			/*
			 * This must be a delta and we already know what the
			 * final object type is.  Let's extract the actual
			 * object size from the delta header.
			 */
			entry->size = get_size_from_delta(p, &w_curs,
					entry->in_pack_offset + entry->in_pack_header_size);
			if (entry->size == 0)
				goto give_up;
			unuse_pack(&w_curs);
			return;
		}

		/*
		 * No choice but to fall back to the recursive delta walk
		 * with sha1_object_info() to find about the object type
		 * at this point...
		 */
		give_up:
		unuse_pack(&w_curs);
	}

	entry->type = sha1_object_info(entry->idx.sha1, &entry->size);
	/*
	 * The error condition is checked in prepare_pack().  This is
	 * to permit a missing preferred base object to be ignored
	 * as a preferred base.  Doing so can result in a larger
	 * pack file, but the transfer will still take place.
	 */
}

static int pack_offset_sort(const void *_a, const void *_b)
{
	const struct object_entry *a = *(struct object_entry **)_a;
	const struct object_entry *b = *(struct object_entry **)_b;

	/* avoid filesystem trashing with loose objects */
	if (!a->in_pack && !b->in_pack)
		return hashcmp(a->idx.sha1, b->idx.sha1);

	if (a->in_pack < b->in_pack)
		return -1;
	if (a->in_pack > b->in_pack)
		return 1;
	return a->in_pack_offset < b->in_pack_offset ? -1 :
			(a->in_pack_offset > b->in_pack_offset);
}

static void get_object_details(void)
{
	uint32_t i;
	struct object_entry **sorted_by_offset;

	sorted_by_offset = xcalloc(nr_objects, sizeof(struct object_entry *));
	for (i = 0; i < nr_objects; i++)
		sorted_by_offset[i] = objects + i;
	qsort(sorted_by_offset, nr_objects, sizeof(*sorted_by_offset), pack_offset_sort);

	for (i = 0; i < nr_objects; i++) {
		struct object_entry *entry = sorted_by_offset[i];
		check_object(entry);
		if (big_file_threshold <= entry->size)
			entry->no_try_delta = 1;
	}

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

	if (a->type > b->type)
		return -1;
	if (a->type < b->type)
		return 1;
	if (a->hash > b->hash)
		return -1;
	if (a->hash < b->hash)
		return 1;
	if (a->preferred_base > b->preferred_base)
		return -1;
	if (a->preferred_base < b->preferred_base)
		return 1;
	if (a->size > b->size)
		return -1;
	if (a->size < b->size)
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

#ifndef NO_PTHREADS

static pthread_mutex_t read_mutex;
#define read_lock()		pthread_mutex_lock(&read_mutex)
#define read_unlock()		pthread_mutex_unlock(&read_mutex)

static pthread_mutex_t cache_mutex;
#define cache_lock()		pthread_mutex_lock(&cache_mutex)
#define cache_unlock()		pthread_mutex_unlock(&cache_mutex)

static pthread_mutex_t progress_mutex;
#define progress_lock()		pthread_mutex_lock(&progress_mutex)
#define progress_unlock()	pthread_mutex_unlock(&progress_mutex)

#else

#define read_lock()		(void)0
#define read_unlock()		(void)0
#define cache_lock()		(void)0
#define cache_unlock()		(void)0
#define progress_lock()		(void)0
#define progress_unlock()	(void)0

#endif

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
	if (trg_entry->type != src_entry->type)
		return -1;

	/*
	 * We do not bother to try a delta that we discarded
	 * on an earlier try, but only when reusing delta data.
	 */
	if (reuse_delta && trg_entry->in_pack &&
	    trg_entry->in_pack == src_entry->in_pack &&
	    trg_entry->in_pack_type != OBJ_REF_DELTA &&
	    trg_entry->in_pack_type != OBJ_OFS_DELTA)
		return 0;

	/* Let's not bust the allowed depth. */
	if (src->depth >= max_depth)
		return 0;

	/* Now some size filtering heuristics. */
	trg_size = trg_entry->size;
	if (!trg_entry->delta) {
		max_size = trg_size/2 - 20;
		ref_depth = 1;
	} else {
		max_size = trg_entry->delta_size;
		ref_depth = trg->depth;
	}
	max_size = (uint64_t)max_size * (max_depth - src->depth) /
						(max_depth - ref_depth + 1);
	if (max_size == 0)
		return 0;
	src_size = src_entry->size;
	sizediff = src_size < trg_size ? trg_size - src_size : 0;
	if (sizediff >= max_size)
		return 0;
	if (trg_size < src_size / 32)
		return 0;

	/* Load data if not already done */
	if (!trg->data) {
		read_lock();
		trg->data = read_sha1_file(trg_entry->idx.sha1, &type, &sz);
		read_unlock();
		if (!trg->data)
			die("object %s cannot be read",
			    sha1_to_hex(trg_entry->idx.sha1));
		if (sz != trg_size)
			die("object %s inconsistent object length (%lu vs %lu)",
			    sha1_to_hex(trg_entry->idx.sha1), sz, trg_size);
		*mem_usage += sz;
	}
	if (!src->data) {
		read_lock();
		src->data = read_sha1_file(src_entry->idx.sha1, &type, &sz);
		read_unlock();
		if (!src->data) {
			if (src_entry->preferred_base) {
				static int warned = 0;
				if (!warned++)
					warning("object %s cannot be read",
						sha1_to_hex(src_entry->idx.sha1));
				/*
				 * Those objects are not included in the
				 * resulting pack.  Be resilient and ignore
				 * them if they can't be read, in case the
				 * pack could be created nevertheless.
				 */
				return 0;
			}
			die("object %s cannot be read",
			    sha1_to_hex(src_entry->idx.sha1));
		}
		if (sz != src_size)
			die("object %s inconsistent object length (%lu vs %lu)",
			    sha1_to_hex(src_entry->idx.sha1), sz, src_size);
		*mem_usage += sz;
	}
	if (!src->index) {
		src->index = create_delta_index(src->data, src_size);
		if (!src->index) {
			static int warned = 0;
			if (!warned++)
				warning("suboptimal pack - out of memory");
			return 0;
		}
		*mem_usage += sizeof_delta_index(src->index);
	}

	delta_buf = create_delta(src->index, trg->data, trg_size, &delta_size, max_size);
	if (!delta_buf)
		return 0;

	if (trg_entry->delta) {
		/* Prefer only shallower same-sized deltas. */
		if (delta_size == trg_entry->delta_size &&
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
		delta_cache_size -= trg_entry->delta_size;
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

	trg_entry->delta = src_entry;
	trg_entry->delta_size = delta_size;
	trg->depth = src->depth + 1;

	return 1;
}

static unsigned int check_delta_limit(struct object_entry *me, unsigned int n)
{
	struct object_entry *child = me->delta_child;
	unsigned int m = n;
	while (child) {
		unsigned int c = check_delta_limit(child, n + 1);
		if (m < c)
			m = c;
		child = child->delta_sibling;
	}
	return m;
}

static unsigned long free_unpacked(struct unpacked *n)
{
	unsigned long freed_mem = sizeof_delta_index(n->index);
	free_delta_index(n->index);
	n->index = NULL;
	if (n->data) {
		freed_mem += n->entry->size;
		free(n->data);
		n->data = NULL;
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

	array = xcalloc(window, sizeof(struct unpacked));

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
			uint32_t tail = (idx + window - count) % window;
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
		if (entry->delta_child) {
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
			entry->z_delta_size = do_compress(&entry->delta_data,
							  entry->delta_size);
			cache_lock();
			delta_cache_size -= entry->delta_size;
			delta_cache_size += entry->z_delta_size;
			cache_unlock();
		}

		/* if we made n a delta, and if n is already at max
		 * depth, leaving it in the window is pointless.  we
		 * should evict it first.
		 */
		if (entry->delta && max_depth <= n->depth)
			continue;

		/*
		 * Move the best delta base up in the window, after the
		 * currently deltified object, to keep it longer.  It will
		 * be the first base object to be attempted next.
		 */
		if (entry->delta) {
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

#ifndef NO_PTHREADS

static void try_to_free_from_threads(size_t size)
{
	read_lock();
	release_pack_memory(size, -1);
	read_unlock();
}

static try_to_free_t old_try_to_free_routine;

/*
 * The main thread waits on the condition that (at least) one of the workers
 * has stopped working (which is indicated in the .working member of
 * struct thread_params).
 * When a work thread has completed its work, it sets .working to 0 and
 * signals the main thread and waits on the condition that .data_ready
 * becomes 1.
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
	init_recursive_mutex(&read_mutex);
	pthread_mutex_init(&cache_mutex, NULL);
	pthread_mutex_init(&progress_mutex, NULL);
	pthread_cond_init(&progress_cond, NULL);
	old_try_to_free_routine = set_try_to_free_routine(try_to_free_from_threads);
}

static void cleanup_threaded_search(void)
{
	set_try_to_free_routine(old_try_to_free_routine);
	pthread_cond_destroy(&progress_cond);
	pthread_mutex_destroy(&read_mutex);
	pthread_mutex_destroy(&cache_mutex);
	pthread_mutex_destroy(&progress_mutex);
}

static void *threaded_find_deltas(void *arg)
{
	struct thread_params *me = arg;

	while (me->remaining) {
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
	}
	/* leave ->working 1 so that this doesn't get more work assigned */
	return NULL;
}

static void ll_find_deltas(struct object_entry **list, unsigned list_size,
			   int window, int depth, unsigned *processed)
{
	struct thread_params *p;
	int i, ret, active_threads = 0;

	init_threaded_search();

	if (!delta_search_threads)	/* --threads=0 means autodetect */
		delta_search_threads = online_cpus();
	if (delta_search_threads <= 1) {
		find_deltas(list, &list_size, window, depth, processed);
		cleanup_threaded_search();
		return;
	}
	if (progress > pack_to_stdout)
		fprintf(stderr, "Delta compression using up to %d threads.\n",
				delta_search_threads);
	p = xcalloc(delta_search_threads, sizeof(*p));

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
			die("unable to create thread: %s", strerror(ret));
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

#else
#define ll_find_deltas(l, s, w, d, p)	find_deltas(l, &s, w, d, p)
#endif

static int add_ref_tag(const char *path, const unsigned char *sha1, int flag, void *cb_data)
{
	unsigned char peeled[20];

	if (!prefixcmp(path, "refs/tags/") && /* is a tag? */
	    !peel_ref(path, peeled)        && /* peelable? */
	    !is_null_sha1(peeled)          && /* annotated tag? */
	    locate_object_entry(peeled))      /* object packed? */
		add_object_entry(sha1, OBJ_TAG, NULL, 0);
	return 0;
}

static void prepare_pack(int window, int depth)
{
	struct object_entry **delta_list;
	uint32_t i, nr_deltas;
	unsigned n;

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

	if (!nr_objects || !window || !depth)
		return;

	delta_list = xmalloc(nr_objects * sizeof(*delta_list));
	nr_deltas = n = 0;

	for (i = 0; i < nr_objects; i++) {
		struct object_entry *entry = objects + i;

		if (entry->delta)
			/* This happens if we decided to reuse existing
			 * delta from a pack.  "reuse_delta &&" is implied.
			 */
			continue;

		if (entry->size < 50)
			continue;

		if (entry->no_try_delta)
			continue;

		if (!entry->preferred_base) {
			nr_deltas++;
			if (entry->type < 0)
				die("unable to get type of object %s",
				    sha1_to_hex(entry->idx.sha1));
		} else {
			if (entry->type < 0) {
				/*
				 * This object is not found, but we
				 * don't have to include it anyway.
				 */
				continue;
			}
		}

		delta_list[n++] = entry;
	}

	if (nr_deltas && n > 1) {
		unsigned nr_done = 0;
		if (progress)
			progress_state = start_progress("Compressing objects",
							nr_deltas);
		qsort(delta_list, n, sizeof(*delta_list), type_size_sort);
		ll_find_deltas(delta_list, n, window+1, depth, &nr_done);
		stop_progress(&progress_state);
		if (nr_done != nr_deltas)
			die("inconsistency with delta count");
	}
	free(delta_list);
}

static int git_pack_config(const char *k, const char *v, void *cb)
{
	if (!strcmp(k, "pack.window")) {
		window = git_config_int(k, v);
		return 0;
	}
	if (!strcmp(k, "pack.windowmemory")) {
		window_memory_limit = git_config_ulong(k, v);
		return 0;
	}
	if (!strcmp(k, "pack.depth")) {
		depth = git_config_int(k, v);
		return 0;
	}
	if (!strcmp(k, "pack.compression")) {
		int level = git_config_int(k, v);
		if (level == -1)
			level = Z_DEFAULT_COMPRESSION;
		else if (level < 0 || level > Z_BEST_COMPRESSION)
			die("bad pack compression level %d", level);
		pack_compression_level = level;
		pack_compression_seen = 1;
		return 0;
	}
	if (!strcmp(k, "pack.deltacachesize")) {
		max_delta_cache_size = git_config_int(k, v);
		return 0;
	}
	if (!strcmp(k, "pack.deltacachelimit")) {
		cache_max_small_delta_size = git_config_int(k, v);
		return 0;
	}
	if (!strcmp(k, "pack.threads")) {
		delta_search_threads = git_config_int(k, v);
		if (delta_search_threads < 0)
			die("invalid number of threads specified (%d)",
			    delta_search_threads);
#ifdef NO_PTHREADS
		if (delta_search_threads != 1)
			warning("no threads support, ignoring %s", k);
#endif
		return 0;
	}
	if (!strcmp(k, "pack.indexversion")) {
		pack_idx_opts.version = git_config_int(k, v);
		if (pack_idx_opts.version > 2)
			die("bad pack.indexversion=%"PRIu32,
			    pack_idx_opts.version);
		return 0;
	}
	if (!strcmp(k, "pack.packsizelimit")) {
		pack_size_limit_cfg = git_config_ulong(k, v);
		return 0;
	}
	return git_default_config(k, v, cb);
}

static void read_object_list_from_stdin(void)
{
	char line[40 + 1 + PATH_MAX + 2];
	unsigned char sha1[20];

	for (;;) {
		if (!fgets(line, sizeof(line), stdin)) {
			if (feof(stdin))
				break;
			if (!ferror(stdin))
				die("fgets returned NULL, not EOF, not error!");
			if (errno != EINTR)
				die_errno("fgets");
			clearerr(stdin);
			continue;
		}
		if (line[0] == '-') {
			if (get_sha1_hex(line+1, sha1))
				die("expected edge sha1, got garbage:\n %s",
				    line);
			add_preferred_base(sha1);
			continue;
		}
		if (get_sha1_hex(line, sha1))
			die("expected sha1, got garbage:\n %s", line);

		add_preferred_base_object(line+41);
		add_object_entry(sha1, 0, line+41, 0);
	}
}

#define OBJECT_ADDED (1u<<20)

static void show_commit(struct commit *commit, void *data)
{
	add_object_entry(commit->object.sha1, OBJ_COMMIT, NULL, 0);
	commit->object.flags |= OBJECT_ADDED;
}

static void show_object(struct object *obj, const struct name_path *path, const char *last)
{
	char *name = path_name(path, last);

	add_preferred_base_object(name);
	add_object_entry(obj->sha1, obj->type, name, 0);
	obj->flags |= OBJECT_ADDED;

	/*
	 * We will have generated the hash from the name,
	 * but not saved a pointer to it - we can free it
	 */
	free((char *)name);
}

static void show_edge(struct commit *commit)
{
	add_preferred_base(commit->object.sha1);
}

struct in_pack_object {
	off_t offset;
	struct object *object;
};

struct in_pack {
	int alloc;
	int nr;
	struct in_pack_object *array;
};

static void mark_in_pack_object(struct object *object, struct packed_git *p, struct in_pack *in_pack)
{
	in_pack->array[in_pack->nr].offset = find_pack_entry_one(object->sha1, p);
	in_pack->array[in_pack->nr].object = object;
	in_pack->nr++;
}

/*
 * Compare the objects in the offset order, in order to emulate the
 * "git rev-list --objects" output that produced the pack originally.
 */
static int ofscmp(const void *a_, const void *b_)
{
	struct in_pack_object *a = (struct in_pack_object *)a_;
	struct in_pack_object *b = (struct in_pack_object *)b_;

	if (a->offset < b->offset)
		return -1;
	else if (a->offset > b->offset)
		return 1;
	else
		return hashcmp(a->object->sha1, b->object->sha1);
}

static void add_objects_in_unpacked_packs(struct rev_info *revs)
{
	struct packed_git *p;
	struct in_pack in_pack;
	uint32_t i;

	memset(&in_pack, 0, sizeof(in_pack));

	for (p = packed_git; p; p = p->next) {
		const unsigned char *sha1;
		struct object *o;

		if (!p->pack_local || p->pack_keep)
			continue;
		if (open_pack_index(p))
			die("cannot open pack index");

		ALLOC_GROW(in_pack.array,
			   in_pack.nr + p->num_objects,
			   in_pack.alloc);

		for (i = 0; i < p->num_objects; i++) {
			sha1 = nth_packed_object_sha1(p, i);
			o = lookup_unknown_object(sha1);
			if (!(o->flags & OBJECT_ADDED))
				mark_in_pack_object(o, p, &in_pack);
			o->flags |= OBJECT_ADDED;
		}
	}

	if (in_pack.nr) {
		qsort(in_pack.array, in_pack.nr, sizeof(in_pack.array[0]),
		      ofscmp);
		for (i = 0; i < in_pack.nr; i++) {
			struct object *o = in_pack.array[i].object;
			add_object_entry(o->sha1, o->type, "", 0);
		}
	}
	free(in_pack.array);
}

static int has_sha1_pack_kept_or_nonlocal(const unsigned char *sha1)
{
	static struct packed_git *last_found = (void *)1;
	struct packed_git *p;

	p = (last_found != (void *)1) ? last_found : packed_git;

	while (p) {
		if ((!p->pack_local || p->pack_keep) &&
			find_pack_entry_one(sha1, p)) {
			last_found = p;
			return 1;
		}
		if (p == last_found)
			p = packed_git;
		else
			p = p->next;
		if (p == last_found)
			p = p->next;
	}
	return 0;
}

static void loosen_unused_packed_objects(struct rev_info *revs)
{
	struct packed_git *p;
	uint32_t i;
	const unsigned char *sha1;

	for (p = packed_git; p; p = p->next) {
		if (!p->pack_local || p->pack_keep)
			continue;

		if (open_pack_index(p))
			die("cannot open pack index");

		for (i = 0; i < p->num_objects; i++) {
			sha1 = nth_packed_object_sha1(p, i);
			if (!locate_object_entry(sha1) &&
				!has_sha1_pack_kept_or_nonlocal(sha1))
				if (force_object_loose(sha1, p->mtime))
					die("unable to force loose object");
		}
	}
}

static void get_object_list(int ac, const char **av)
{
	struct rev_info revs;
	char line[1000];
	int flags = 0;

	init_revisions(&revs, NULL);
	save_commit_buffer = 0;
	setup_revisions(ac, av, &revs, NULL);

	while (fgets(line, sizeof(line), stdin) != NULL) {
		int len = strlen(line);
		if (len && line[len - 1] == '\n')
			line[--len] = 0;
		if (!len)
			break;
		if (*line == '-') {
			if (!strcmp(line, "--not")) {
				flags ^= UNINTERESTING;
				continue;
			}
			die("not a rev '%s'", line);
		}
		if (handle_revision_arg(line, &revs, flags, 1))
			die("bad revision '%s'", line);
	}

	if (prepare_revision_walk(&revs))
		die("revision walk setup failed");
	mark_edges_uninteresting(revs.commits, &revs, show_edge);
	traverse_commit_list(&revs, show_commit, show_object, NULL);

	if (keep_unreachable)
		add_objects_in_unpacked_packs(&revs);
	if (unpack_unreachable)
		loosen_unused_packed_objects(&revs);
}

int cmd_pack_objects(int argc, const char **argv, const char *prefix)
{
	int use_internal_rev_list = 0;
	int thin = 0;
	int all_progress_implied = 0;
	uint32_t i;
	const char **rp_av;
	int rp_ac_alloc = 64;
	int rp_ac;

	read_replace_refs = 0;

	rp_av = xcalloc(rp_ac_alloc, sizeof(*rp_av));

	rp_av[0] = "pack-objects";
	rp_av[1] = "--objects"; /* --thin will make it --objects-edge */
	rp_ac = 2;

	reset_pack_idx_option(&pack_idx_opts);
	git_config(git_pack_config, NULL);
	if (!pack_compression_seen && core_compression_seen)
		pack_compression_level = core_compression_level;

	progress = isatty(2);
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (*arg != '-')
			break;

		if (!strcmp("--non-empty", arg)) {
			non_empty = 1;
			continue;
		}
		if (!strcmp("--local", arg)) {
			local = 1;
			continue;
		}
		if (!strcmp("--incremental", arg)) {
			incremental = 1;
			continue;
		}
		if (!strcmp("--honor-pack-keep", arg)) {
			ignore_packed_keep = 1;
			continue;
		}
		if (!prefixcmp(arg, "--compression=")) {
			char *end;
			int level = strtoul(arg+14, &end, 0);
			if (!arg[14] || *end)
				usage(pack_usage);
			if (level == -1)
				level = Z_DEFAULT_COMPRESSION;
			else if (level < 0 || level > Z_BEST_COMPRESSION)
				die("bad pack compression level %d", level);
			pack_compression_level = level;
			continue;
		}
		if (!prefixcmp(arg, "--max-pack-size=")) {
			pack_size_limit_cfg = 0;
			if (!git_parse_ulong(arg+16, &pack_size_limit))
				usage(pack_usage);
			continue;
		}
		if (!prefixcmp(arg, "--window=")) {
			char *end;
			window = strtoul(arg+9, &end, 0);
			if (!arg[9] || *end)
				usage(pack_usage);
			continue;
		}
		if (!prefixcmp(arg, "--window-memory=")) {
			if (!git_parse_ulong(arg+16, &window_memory_limit))
				usage(pack_usage);
			continue;
		}
		if (!prefixcmp(arg, "--threads=")) {
			char *end;
			delta_search_threads = strtoul(arg+10, &end, 0);
			if (!arg[10] || *end || delta_search_threads < 0)
				usage(pack_usage);
#ifdef NO_PTHREADS
			if (delta_search_threads != 1)
				warning("no threads support, "
					"ignoring %s", arg);
#endif
			continue;
		}
		if (!prefixcmp(arg, "--depth=")) {
			char *end;
			depth = strtoul(arg+8, &end, 0);
			if (!arg[8] || *end)
				usage(pack_usage);
			continue;
		}
		if (!strcmp("--progress", arg)) {
			progress = 1;
			continue;
		}
		if (!strcmp("--all-progress", arg)) {
			progress = 2;
			continue;
		}
		if (!strcmp("--all-progress-implied", arg)) {
			all_progress_implied = 1;
			continue;
		}
		if (!strcmp("-q", arg)) {
			progress = 0;
			continue;
		}
		if (!strcmp("--no-reuse-delta", arg)) {
			reuse_delta = 0;
			continue;
		}
		if (!strcmp("--no-reuse-object", arg)) {
			reuse_object = reuse_delta = 0;
			continue;
		}
		if (!strcmp("--delta-base-offset", arg)) {
			allow_ofs_delta = 1;
			continue;
		}
		if (!strcmp("--stdout", arg)) {
			pack_to_stdout = 1;
			continue;
		}
		if (!strcmp("--revs", arg)) {
			use_internal_rev_list = 1;
			continue;
		}
		if (!strcmp("--keep-unreachable", arg)) {
			keep_unreachable = 1;
			continue;
		}
		if (!strcmp("--unpack-unreachable", arg)) {
			unpack_unreachable = 1;
			continue;
		}
		if (!strcmp("--include-tag", arg)) {
			include_tag = 1;
			continue;
		}
		if (!strcmp("--unpacked", arg) ||
		    !strcmp("--reflog", arg) ||
		    !strcmp("--all", arg)) {
			use_internal_rev_list = 1;
			if (rp_ac >= rp_ac_alloc - 1) {
				rp_ac_alloc = alloc_nr(rp_ac_alloc);
				rp_av = xrealloc(rp_av,
						 rp_ac_alloc * sizeof(*rp_av));
			}
			rp_av[rp_ac++] = arg;
			continue;
		}
		if (!strcmp("--thin", arg)) {
			use_internal_rev_list = 1;
			thin = 1;
			rp_av[1] = "--objects-edge";
			continue;
		}
		if (!prefixcmp(arg, "--index-version=")) {
			char *c;
			pack_idx_opts.version = strtoul(arg + 16, &c, 10);
			if (pack_idx_opts.version > 2)
				die("bad %s", arg);
			if (*c == ',')
				pack_idx_opts.off32_limit = strtoul(c+1, &c, 0);
			if (*c || pack_idx_opts.off32_limit & 0x80000000)
				die("bad %s", arg);
			continue;
		}
		if (!strcmp(arg, "--keep-true-parents")) {
			grafts_replace_parents = 0;
			continue;
		}
		usage(pack_usage);
	}

	/* Traditionally "pack-objects [options] base extra" failed;
	 * we would however want to take refs parameter that would
	 * have been given to upstream rev-list ourselves, which means
	 * we somehow want to say what the base name is.  So the
	 * syntax would be:
	 *
	 * pack-objects [options] base <refs...>
	 *
	 * in other words, we would treat the first non-option as the
	 * base_name and send everything else to the internal revision
	 * walker.
	 */

	if (!pack_to_stdout)
		base_name = argv[i++];

	if (pack_to_stdout != !base_name)
		usage(pack_usage);

	if (!pack_to_stdout && !pack_size_limit)
		pack_size_limit = pack_size_limit_cfg;
	if (pack_to_stdout && pack_size_limit)
		die("--max-pack-size cannot be used to build a pack for transfer.");
	if (pack_size_limit && pack_size_limit < 1024*1024) {
		warning("minimum pack size limit is 1 MiB");
		pack_size_limit = 1024*1024;
	}

	if (!pack_to_stdout && thin)
		die("--thin cannot be used to build an indexable pack.");

	if (keep_unreachable && unpack_unreachable)
		die("--keep-unreachable and --unpack-unreachable are incompatible.");

	if (progress && all_progress_implied)
		progress = 2;

	prepare_packed_git();

	if (progress)
		progress_state = start_progress("Counting objects", 0);
	if (!use_internal_rev_list)
		read_object_list_from_stdin();
	else {
		rp_av[rp_ac] = NULL;
		get_object_list(rp_ac, rp_av);
	}
	cleanup_preferred_base();
	if (include_tag && nr_result)
		for_each_ref(add_ref_tag, NULL);
	stop_progress(&progress_state);

	if (non_empty && !nr_result)
		return 0;
	if (nr_result)
		prepare_pack(window, depth);
	write_pack_file();
	if (progress)
		fprintf(stderr, "Total %"PRIu32" (delta %"PRIu32"),"
			" reused %"PRIu32" (delta %"PRIu32")\n",
			written, written_delta, reused, reused_delta);
	return 0;
}
