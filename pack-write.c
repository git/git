#include "git-compat-util.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "pack.h"
#include "csum-file.h"
#include "remote.h"
#include "chunk-format.h"
#include "object-file.h"
#include "pack-mtimes.h"
#include "pack-objects.h"
#include "pack-revindex.h"
#include "path.h"
#include "repository.h"
#include "strbuf.h"

void reset_pack_idx_option(struct pack_idx_option *opts)
{
	memset(opts, 0, sizeof(*opts));
	opts->version = 2;
	opts->off32_limit = 0x7fffffff;
	opts->delta_base_cache_limit = DEFAULT_DELTA_BASE_CACHE_LIMIT;
}

static int oid_compare(const void *_a, const void *_b)
{
	struct pack_idx_entry *a = *(struct pack_idx_entry **)_a;
	struct pack_idx_entry *b = *(struct pack_idx_entry **)_b;
	return oidcmp(&a->oid, &b->oid);
}

static int oid_compare_compat(const void *_a, const void *_b)
{
	struct pack_idx_entry *a = *(struct pack_idx_entry **)_a;
	struct pack_idx_entry *b = *(struct pack_idx_entry **)_b;
	return oidcmp(&a->compat_oid, &b->compat_oid);
}

static int cmp_uint32(const void *a_, const void *b_)
{
	uint32_t a = *((uint32_t *)a_);
	uint32_t b = *((uint32_t *)b_);

	return (a < b) ? -1 : (a != b);
}

static int need_large_offset(off_t offset, const struct pack_idx_option *opts)
{
	uint32_t ofsval;

	if ((offset >> 31) || (opts->off32_limit < offset))
		return 1;
	if (!opts->anomaly_nr)
		return 0;
	ofsval = offset;
	return !!bsearch(&ofsval, opts->anomaly, opts->anomaly_nr,
			 sizeof(ofsval), cmp_uint32);
}

static inline int last_matching_offset(const struct object_id *a, const struct object_id *b,
				       const struct git_hash_algo *algop)
{
	size_t i;
	for (i = 0; i < algop->rawsz; i++)
		if (a->hash[i] != b->hash[i])
			return i;
	/* We should never hit this case. */
	return i;
}

static const struct object_id *main_oid(const struct pack_idx_entry *ent)
{
	return &ent->oid;
}

static const struct object_id *compat_oid(const struct pack_idx_entry *ent)
{
	return &ent->compat_oid;
}

static uint32_t find_short_name_len(struct pack_idx_entry **sorted, size_t nr_objects,
				    const struct git_hash_algo *algop,
				    const struct object_id *(*fn)(const struct pack_idx_entry *))
{
	size_t i, len = 1;
	for (i = 1; i < nr_objects; i++) {
		const struct object_id *oid1 = fn(sorted[i - 1]);
		const struct object_id *oid2 = fn(sorted[i]);
		size_t offset = last_matching_offset(oid1, oid2, algop);

		if (offset >= len)
			len = offset + 1;
	}
	return len > algop->rawsz ? algop->rawsz : len;
}

struct pack_idx_format_header {
	uint32_t id;
	uint32_t short_name_len;
	uint64_t offset;
};

/* This struct contains fields beyond the signature and version number. */
struct pack_idx_header_v3 {
	uint32_t hdr_len;
	uint32_t nr_objects;
	uint32_t nr_formats;
};

static void write_v3_header(struct repository *repo,
			    int nr_objects, int nr_large_offset,
			    uint32_t short_name_len[2], int padding[2],
			    struct hashfile *f)
{
	struct pack_idx_header_v3 hdr;
	struct pack_idx_format_header format[2];
	const struct git_hash_algo *compat = repo->compat_hash_algo;
	uint32_t nformats = compat ? 2 : 1;
	/*
	 * signature (4) + version (4) + length (4) + nobjects(4) + nformats (4) +
	 * each format: identifier (4) + length of shortened names (4) +
	 *		table offset (8) +
	 * offset to trailer (8)
	 */
	uint32_t hdrlen = 4 + 4 + 4 + 4 + 4 + nformats * (4 + 4 + 8) + 8;
	uint64_t offset = hdrlen;
	uint64_t short_table = short_name_len[0] * nr_objects;

	hdr.hdr_len = htonl(hdrlen);
	hdr.nr_objects = htonl(nr_objects);
	hdr.nr_formats = htonl(nformats);
	hashwrite(f, &hdr, sizeof(hdr));

	/* padding */
	if (short_table & 3) {
		padding[0] = 4 - (short_table & 3);
		offset += padding[0];
	}

	format[0].id = htonl(repo->hash_algo->format_id);
	format[0].offset = htonll(offset);
	format[0].short_name_len = htonl(short_name_len[0]);
	hashwrite(f, &format[0], sizeof(format[0]));
	offset += (uint64_t)(short_name_len[0] + repo->hash_algo->rawsz + 4 + 4 + 4) * nr_objects +
		  8 * nr_large_offset;

	if (compat) {
		short_table = short_name_len[1] * nr_objects;
		/* padding */
		if (short_table & 3) {
			padding[1] = 4 - (short_table & 3);
			offset += padding[1];
		}

		format[1].id = htonl(compat->format_id);
		format[1].offset = htonll(offset);
		format[1].short_name_len = htonl(short_name_len[1]);
		hashwrite(f, &format[1], sizeof(format[1]));
		offset += (uint64_t)(short_name_len[1] + compat->rawsz + 4) * nr_objects;
	}

	offset = htonll(offset);
	hashwrite(f, &offset, sizeof(offset));
}

/*
 * The *hash contains the pack content hash.
 * The objects array passed in should be in pack order and will be sorted by
 * object ID on return.
 */
const char *write_idx_file(struct repository *repo,
			   const char *index_name, struct pack_idx_entry **objects,
			   int nr_objects, const struct pack_idx_option *opts,
			   const unsigned char *hash)
{
	struct hashfile *f;
	struct pack_idx_entry **sorted_by_sha, **sorted_by_sha_compat = NULL, **list, **last;
	int i, fd;
	uint32_t index_version;
	unsigned int nr_large_offset = 0;
	const struct git_hash_algo *compat = repo->compat_hash_algo;
	uint32_t short_name_len[2] = { 0 };
	int padding_size[2] = { 0 };
	uint8_t zero[4] = { 0 };

	index_version = opts->version;

	if (nr_objects) {
		sorted_by_sha = objects;
		list = sorted_by_sha;
		last = sorted_by_sha + nr_objects;
		for (i = 0; i < nr_objects; ++i) {
			objects[i]->idx = i;
			if (need_large_offset(objects[i]->offset, opts))
				nr_large_offset++;
		}
		/*
		 * If we're using index v3, we need both pack order and sorted
		 * order.
		 */
		if (index_version >= 3) {
			DUP_ARRAY(sorted_by_sha, objects, nr_objects);
			if (compat) {
				DUP_ARRAY(sorted_by_sha_compat, objects,
					  nr_objects);
				QSORT(sorted_by_sha_compat, nr_objects, oid_compare_compat);
			}
		}
		QSORT(sorted_by_sha, nr_objects, oid_compare);
	}
	else
		sorted_by_sha = list = last = NULL;

	if (opts->flags & WRITE_IDX_VERIFY) {
		assert(index_name);
		f = hashfd_check(repo->hash_algo, index_name);
	} else {
		if (!index_name) {
			struct strbuf tmp_file = STRBUF_INIT;
			fd = odb_mkstemp(repo->objects, &tmp_file,
					 "pack/tmp_idx_XXXXXX");
			index_name = strbuf_detach(&tmp_file, NULL);
		} else {
			unlink(index_name);
			fd = xopen(index_name, O_CREAT|O_EXCL|O_WRONLY, 0600);
		}
		f = hashfd(repo->hash_algo, fd, index_name);
	}

	/* if last object's offset is >= 2^31 we should use index V2 */
	if (index_version == 1 && nr_large_offset)
		index_version = 2;

	/* index versions 2 and above need a header */
	if (index_version >= 2) {
		struct pack_idx_header hdr;
		hdr.idx_signature = htonl(PACK_IDX_SIGNATURE);
		hdr.idx_version = htonl(index_version);
		hashwrite(f, &hdr, sizeof(hdr));
	}

	if (index_version >= 3) {
		short_name_len[0] = find_short_name_len(sorted_by_sha,
							nr_objects,
							repo->hash_algo,
							main_oid);
		if (compat)
			short_name_len[1] = find_short_name_len(sorted_by_sha_compat,
								nr_objects,
								compat, compat_oid);
		write_v3_header(repo, nr_objects, nr_large_offset, short_name_len, padding_size, f);
		hashwrite(f, zero, padding_size[0]);

		/* Write the sorted abbreviated names */
		list = sorted_by_sha;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			hashwrite(f, obj->oid.hash, short_name_len[0]);
			if ((opts->flags & WRITE_IDX_STRICT) &&
			    (i && oideq(&list[-2]->oid, &obj->oid)))
				die("The same object %s appears twice in the pack",
				    oid_to_hex(&obj->oid));
		}
	} else {
		/*
		 * Write the first-level table (the list is sorted,
		 * but we use a 256-entry lookup to be able to avoid
		 * having to do eight extra binary search iterations).
		 */
		for (i = 0; i < 256; i++) {
			struct pack_idx_entry **next = list;
			while (next < last) {
				struct pack_idx_entry *obj = *next;
				if (obj->oid.hash[0] != i)
					break;
				next++;
			}
			hashwrite_be32(f, next - sorted_by_sha);
			list = next;
		}
	}

	/*
	 * Write the actual SHA1 entries..
	 */
	list = objects;
	for (i = 0; i < nr_objects; i++) {
		struct pack_idx_entry *obj = *list++;
		if (index_version < 2)
			hashwrite_be32(f, obj->offset);
		hashwrite(f, obj->oid.hash, repo->hash_algo->rawsz);
		if ((opts->flags & WRITE_IDX_STRICT) && index_version < 3 &&
		    (i && oideq(&list[-2]->oid, &obj->oid)))
			die("The same object %s appears twice in the pack",
			    oid_to_hex(&obj->oid));
	}

	if (index_version >= 3) {
		/* write the sorted-to-pack index */
		list = sorted_by_sha;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			uint32_t index = htonl(obj->idx);
			hashwrite(f, &index, 4);
		}
	}

	if (index_version >= 2) {
		unsigned int nr_large_offset = 0;

		/* write the crc32 table */
		list = objects;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			hashwrite_be32(f, obj->crc32);
		}

		/* write the 32-bit offset table */
		list = objects;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			uint32_t offset;

			offset = (need_large_offset(obj->offset, opts)
				  ? (0x80000000 | nr_large_offset++)
				  : obj->offset);
			hashwrite_be32(f, offset);
		}

		/* write the large offset table */
		list = objects;
		while (nr_large_offset) {
			struct pack_idx_entry *obj = *list++;
			uint64_t offset = obj->offset;

			if (!need_large_offset(offset, opts))
				continue;
			hashwrite_be64(f, offset);
			nr_large_offset--;
		}
	}

	if (index_version >= 3 && compat) {
		hashwrite(f, zero, padding_size[1]);

		/* Write the sorted abbreviated names */
		list = sorted_by_sha_compat;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			hashwrite(f, obj->compat_oid.hash, short_name_len[1]);
			if ((opts->flags & WRITE_IDX_STRICT) &&
			    (i && oideq(&list[-2]->compat_oid, &obj->compat_oid)))
				die("The same object %s appears twice in the pack",
				    oid_to_hex(&obj->compat_oid));
		}

		list = objects;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			hashwrite(f, obj->compat_oid.hash, compat->rawsz);
		}

		/* write the sorted-to-pack index */
		list = sorted_by_sha_compat;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			uint32_t index = htonl(obj->idx);
			hashwrite(f, &index, 4);
		}
	}

	hashwrite(f, hash, repo->hash_algo->rawsz);
	finalize_hashfile(f, NULL, FSYNC_COMPONENT_PACK_METADATA,
			  CSUM_HASH_IN_STREAM | CSUM_CLOSE |
			  ((opts->flags & WRITE_IDX_VERIFY) ? 0 : CSUM_FSYNC));

	if (sorted_by_sha != objects) {
		QSORT(objects, nr_objects, oid_compare);
		free(sorted_by_sha);
	}
	free(sorted_by_sha_compat);
	return index_name;
}

static int pack_order_cmp(const void *va, const void *vb, void *ctx)
{
	struct pack_idx_entry **objects = ctx;

	off_t oa = objects[*(uint32_t*)va]->offset;
	off_t ob = objects[*(uint32_t*)vb]->offset;

	if (oa < ob)
		return -1;
	if (oa > ob)
		return 1;
	return 0;
}

static void write_rev_header(const struct git_hash_algo *hash_algo,
			     struct hashfile *f)
{
	hashwrite_be32(f, RIDX_SIGNATURE);
	hashwrite_be32(f, RIDX_VERSION);
	hashwrite_be32(f, oid_version(hash_algo));
}

static void write_rev_index_positions(struct hashfile *f,
				      uint32_t *pack_order,
				      uint32_t nr_objects)
{
	uint32_t i;
	for (i = 0; i < nr_objects; i++)
		hashwrite_be32(f, pack_order[i]);
}

static void write_rev_trailer(const struct git_hash_algo *hash_algo,
			      struct hashfile *f, const unsigned char *hash)
{
	hashwrite(f, hash, hash_algo->rawsz);
}

char *write_rev_file(struct repository *repo,
		     const char *rev_name,
		     struct pack_idx_entry **objects,
		     uint32_t nr_objects,
		     const unsigned char *hash,
		     unsigned flags)
{
	uint32_t *pack_order;
	uint32_t i;
	char *ret;

	if (!(flags & WRITE_REV) && !(flags & WRITE_REV_VERIFY))
		return NULL;

	ALLOC_ARRAY(pack_order, nr_objects);
	for (i = 0; i < nr_objects; i++)
		pack_order[i] = i;
	QSORT_S(pack_order, nr_objects, pack_order_cmp, objects);

	ret = write_rev_file_order(repo, rev_name, pack_order, nr_objects,
				   hash, flags);

	free(pack_order);

	return ret;
}

char *write_rev_file_order(struct repository *repo,
			   const char *rev_name,
			   uint32_t *pack_order,
			   uint32_t nr_objects,
			   const unsigned char *hash,
			   unsigned flags)
{
	struct hashfile *f;
	char *path;
	int fd;

	if ((flags & WRITE_REV) && (flags & WRITE_REV_VERIFY))
		die(_("cannot both write and verify reverse index"));

	if (flags & WRITE_REV) {
		if (!rev_name) {
			struct strbuf tmp_file = STRBUF_INIT;
			fd = odb_mkstemp(repo->objects, &tmp_file,
					 "pack/tmp_rev_XXXXXX");
			path = strbuf_detach(&tmp_file, NULL);
		} else {
			unlink(rev_name);
			fd = xopen(rev_name, O_CREAT|O_EXCL|O_WRONLY, 0600);
			path = xstrdup(rev_name);
		}
		f = hashfd(repo->hash_algo, fd, path);
	} else if (flags & WRITE_REV_VERIFY) {
		struct stat statbuf;
		if (stat(rev_name, &statbuf)) {
			if (errno == ENOENT) {
				/* .rev files are optional */
				return NULL;
			} else
				die_errno(_("could not stat: %s"), rev_name);
		}
		f = hashfd_check(repo->hash_algo, rev_name);
		path = xstrdup(rev_name);
	} else {
		return NULL;
	}

	write_rev_header(repo->hash_algo, f);

	write_rev_index_positions(f, pack_order, nr_objects);
	write_rev_trailer(repo->hash_algo, f, hash);

	if (adjust_shared_perm(repo, path) < 0)
		die(_("failed to make %s readable"), path);

	finalize_hashfile(f, NULL, FSYNC_COMPONENT_PACK_METADATA,
			  CSUM_HASH_IN_STREAM | CSUM_CLOSE |
			  ((flags & WRITE_IDX_VERIFY) ? 0 : CSUM_FSYNC));

	return path;
}

static void write_mtimes_header(const struct git_hash_algo *hash_algo,
				struct hashfile *f)
{
	hashwrite_be32(f, MTIMES_SIGNATURE);
	hashwrite_be32(f, MTIMES_VERSION);
	hashwrite_be32(f, oid_version(hash_algo));
}

/*
 * Writes the object mtimes of "objects" for use in a .mtimes file.
 * Note that objects must be in lexicographic (index) order, which is
 * the expected ordering of these values in the .mtimes file.
 */
static void write_mtimes_objects(struct hashfile *f,
				 struct packing_data *to_pack,
				 struct pack_idx_entry **objects,
				 uint32_t nr_objects)
{
	uint32_t i;
	for (i = 0; i < nr_objects; i++) {
		struct object_entry *e = (struct object_entry*)objects[i];
		hashwrite_be32(f, oe_cruft_mtime(to_pack, e));
	}
}

static void write_mtimes_trailer(const struct git_hash_algo *hash_algo,
				 struct hashfile *f, const unsigned char *hash)
{
	hashwrite(f, hash, hash_algo->rawsz);
}

static char *write_mtimes_file(struct repository *repo,
			       struct packing_data *to_pack,
			       struct pack_idx_entry **objects,
			       uint32_t nr_objects,
			       const unsigned char *hash)
{
	struct strbuf tmp_file = STRBUF_INIT;
	char *mtimes_name;
	struct hashfile *f;
	int fd;

	if (!to_pack)
		BUG("cannot call write_mtimes_file with NULL packing_data");

	fd = odb_mkstemp(repo->objects, &tmp_file, "pack/tmp_mtimes_XXXXXX");
	mtimes_name = strbuf_detach(&tmp_file, NULL);
	f = hashfd(repo->hash_algo, fd, mtimes_name);

	write_mtimes_header(repo->hash_algo, f);
	write_mtimes_objects(f, to_pack, objects, nr_objects);
	write_mtimes_trailer(repo->hash_algo, f, hash);

	if (adjust_shared_perm(repo, mtimes_name) < 0)
		die(_("failed to make %s readable"), mtimes_name);

	finalize_hashfile(f, NULL, FSYNC_COMPONENT_PACK_METADATA,
			  CSUM_HASH_IN_STREAM | CSUM_CLOSE | CSUM_FSYNC);

	return mtimes_name;
}

off_t write_pack_header(struct hashfile *f, uint32_t nr_entries)
{
	struct pack_header hdr;

	hdr.hdr_signature = htonl(PACK_SIGNATURE);
	hdr.hdr_version = htonl(PACK_VERSION);
	hdr.hdr_entries = htonl(nr_entries);
	hashwrite(f, &hdr, sizeof(hdr));
	return sizeof(hdr);
}

/*
 * Update pack header with object_count and compute new SHA1 for pack data
 * associated to pack_fd, and write that SHA1 at the end.  That new SHA1
 * is also returned in new_pack_sha1.
 *
 * If partial_pack_sha1 is non null, then the SHA1 of the existing pack
 * (without the header update) is computed and validated against the
 * one provided in partial_pack_sha1.  The validation is performed at
 * partial_pack_offset bytes in the pack file.  The SHA1 of the remaining
 * data (i.e. from partial_pack_offset to the end) is then computed and
 * returned in partial_pack_sha1.
 *
 * Note that new_pack_sha1 is updated last, so both new_pack_sha1 and
 * partial_pack_sha1 can refer to the same buffer if the caller is not
 * interested in the resulting SHA1 of pack data above partial_pack_offset.
 */
void fixup_pack_header_footer(const struct git_hash_algo *hash_algo,
			 int pack_fd,
			 unsigned char *new_pack_hash,
			 const char *pack_name,
			 uint32_t object_count,
			 unsigned char *partial_pack_hash,
			 off_t partial_pack_offset)
{
	int aligned_sz, buf_sz = 8 * 1024;
	struct git_hash_ctx old_hash_ctx, new_hash_ctx;
	struct pack_header hdr;
	char *buf;
	ssize_t read_result;

	git_hash_init(&old_hash_ctx, hash_algo);
	git_hash_init(&new_hash_ctx, hash_algo);

	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die_errno("Failed seeking to start of '%s'", pack_name);
	read_result = read_in_full(pack_fd, &hdr, sizeof(hdr));
	if (read_result < 0)
		die_errno("Unable to reread header of '%s'", pack_name);
	else if (read_result != sizeof(hdr))
		die_errno("Unexpected short read for header of '%s'",
			  pack_name);
	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die_errno("Failed seeking to start of '%s'", pack_name);
	git_hash_update(&old_hash_ctx, &hdr, sizeof(hdr));
	hdr.hdr_entries = htonl(object_count);
	git_hash_update(&new_hash_ctx, &hdr, sizeof(hdr));
	write_or_die(pack_fd, &hdr, sizeof(hdr));
	partial_pack_offset -= sizeof(hdr);

	buf = xmalloc(buf_sz);
	aligned_sz = buf_sz - sizeof(hdr);
	for (;;) {
		ssize_t m, n;
		m = (partial_pack_hash && partial_pack_offset < aligned_sz) ?
			partial_pack_offset : aligned_sz;
		n = xread(pack_fd, buf, m);
		if (!n)
			break;
		if (n < 0)
			die_errno("Failed to checksum '%s'", pack_name);
		git_hash_update(&new_hash_ctx, buf, n);

		aligned_sz -= n;
		if (!aligned_sz)
			aligned_sz = buf_sz;

		if (!partial_pack_hash)
			continue;

		git_hash_update(&old_hash_ctx, buf, n);
		partial_pack_offset -= n;
		if (partial_pack_offset == 0) {
			unsigned char hash[GIT_MAX_RAWSZ];
			git_hash_final(hash, &old_hash_ctx);
			if (!hasheq(hash, partial_pack_hash,
				    hash_algo))
				die("Unexpected checksum for %s "
				    "(disk corruption?)", pack_name);
			/*
			 * Now let's compute the SHA1 of the remainder of the
			 * pack, which also means making partial_pack_offset
			 * big enough not to matter anymore.
			 */
			git_hash_init(&old_hash_ctx, hash_algo);
			partial_pack_offset = ~partial_pack_offset;
			partial_pack_offset -= MSB(partial_pack_offset, 1);
		}
	}
	free(buf);

	if (partial_pack_hash)
		git_hash_final(partial_pack_hash, &old_hash_ctx);
	git_hash_final(new_pack_hash, &new_hash_ctx);
	write_or_die(pack_fd, new_pack_hash, hash_algo->rawsz);
	fsync_component_or_die(FSYNC_COMPONENT_PACK, pack_fd, pack_name);
}

int read_index_pack_report(struct repository *repo, int ip_out,
			   struct strbuf *pack_name, int *is_keep)
{
	const int len = repo->hash_algo->hexsz + 6;
	char packname[GIT_MAX_HEXSZ + 6];
	int is_keep_ignored;
	const char *name;

	if (!is_keep)
		is_keep = &is_keep_ignored;

	/*
	 * The first thing we expect from index-pack's output is
	 * "pack\t<name>\n" or "keep\t<name>\n", where <name> is the hexadecimal
	 * name of the packfile it has just written. The line is therefore five
	 * bytes of prefix, `hexsz` bytes of name, and a trailing newline. A
	 * "keep" prefix tells us that index-pack has also created the
	 * corresponding ".keep" file.
	 */
	if (read_in_full(ip_out, packname, len) != len || packname[len - 1] != '\n')
		return -1;

	packname[len - 1] = 0;
	if (skip_prefix(packname, "pack\t", &name))
		*is_keep = 0;
	else if (skip_prefix(packname, "keep\t", &name))
		*is_keep = 1;
	else
		return -1;

	strbuf_addstr(pack_name, name);

	return 0;
}

char *index_pack_lockfile(struct odb_source *source, int ip_out,
			  int *is_well_formed)
{
	struct strbuf pack_name = STRBUF_INIT;
	char *lockfile = NULL;
	int is_keep = 0;

	if (read_index_pack_report(source->odb->repo, ip_out, &pack_name, &is_keep)) {
		if (is_well_formed)
			*is_well_formed = 0;
		return NULL;
	}

	if (is_well_formed)
		*is_well_formed = 1;

	/* If there is no ".keep" file, then there is no lockfile to return. */
	if (is_keep)
		lockfile = xstrfmt("%s/pack/pack-%s.keep", source->path, pack_name.buf);

	strbuf_release(&pack_name);
	return lockfile;
}

/*
 * The per-object header is a pretty dense thing, which is
 *  - first byte: low four bits are "size", then three bits of "type",
 *    and the high bit is "size continues".
 *  - each byte afterwards: low seven bits are size continuation,
 *    with the high bit being "size continues"
 */
int encode_in_pack_object_header(unsigned char *hdr, int hdr_len,
				 enum object_type type, uintmax_t size)
{
	int n = 1;
	unsigned char c;

	if (type < OBJ_COMMIT || type > OBJ_REF_DELTA)
		die("bad type %d", type);

	c = (type << 4) | (size & 15);
	size >>= 4;
	while (size) {
		if (n == hdr_len)
			die("object size is too enormous to format");
		*hdr++ = c | 0x80;
		c = size & 0x7f;
		size >>= 7;
		n++;
	}
	*hdr = c;
	return n;
}

struct hashfile *create_tmp_packfile(struct repository *repo,
				     char **pack_tmp_name)
{
	struct strbuf tmpname = STRBUF_INIT;
	int fd;

	fd = odb_mkstemp(repo->objects, &tmpname, "pack/tmp_pack_XXXXXX");
	*pack_tmp_name = strbuf_detach(&tmpname, NULL);
	return hashfd(repo->hash_algo, fd, *pack_tmp_name);
}

static void rename_tmp_packfile(struct repository *repo,
				struct strbuf *name_prefix, const char *source,
				const char *ext)
{
	size_t name_prefix_len = name_prefix->len;

	strbuf_addstr(name_prefix, ext);
	if (finalize_object_file(repo, source, name_prefix->buf))
		die("unable to rename temporary file to '%s'",
		    name_prefix->buf);
	strbuf_setlen(name_prefix, name_prefix_len);
}

void rename_tmp_packfile_idx(struct repository *repo,
			     struct strbuf *name_buffer,
			     char **idx_tmp_name)
{
	rename_tmp_packfile(repo, name_buffer, *idx_tmp_name, "idx");
}

void stage_tmp_packfiles(struct repository *repo,
			 struct strbuf *name_buffer,
			 const char *pack_tmp_name,
			 struct pack_idx_entry **written_list,
			 uint32_t nr_written,
			 struct packing_data *to_pack,
			 struct pack_idx_option *pack_idx_opts,
			 unsigned char hash[],
			 char **idx_tmp_name)
{
	char *rev_tmp_name = NULL;
	char *mtimes_tmp_name = NULL;

	if (adjust_shared_perm(repo, pack_tmp_name))
		die_errno("unable to make temporary pack file readable");

	*idx_tmp_name = (char *)write_idx_file(repo, NULL, written_list,
					       nr_written, pack_idx_opts, hash);
	if (adjust_shared_perm(repo, *idx_tmp_name))
		die_errno("unable to make temporary index file readable");

	rev_tmp_name = write_rev_file(repo, NULL, written_list, nr_written,
				      hash, pack_idx_opts->flags);

	if (pack_idx_opts->flags & WRITE_MTIMES) {
		mtimes_tmp_name = write_mtimes_file(repo, to_pack,
						    written_list, nr_written,
						    hash);
	}

	rename_tmp_packfile(repo, name_buffer, pack_tmp_name, "pack");
	if (rev_tmp_name)
		rename_tmp_packfile(repo, name_buffer, rev_tmp_name, "rev");
	if (mtimes_tmp_name)
		rename_tmp_packfile(repo, name_buffer, mtimes_tmp_name, "mtimes");

	free(rev_tmp_name);
	free(mtimes_tmp_name);
}

void write_promisor_file(const char *promisor_name, struct ref **sought, int nr_sought)
{
	int i, err;
	FILE *output = xfopen(promisor_name, "w");

	for (i = 0; i < nr_sought; i++)
		fprintf(output, "%s %s\n", oid_to_hex(&sought[i]->old_oid),
			sought[i]->name);

	err = ferror(output);
	err |= fclose(output);
	if (err)
		die(_("could not write '%s' promisor file"), promisor_name);
}
