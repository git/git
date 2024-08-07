#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "pack.h"
#include "csum-file.h"
#include "remote.h"
#include "chunk-format.h"
#include "pack-mtimes.h"
#include "pack-objects.h"
#include "pack-revindex.h"
#include "path.h"
#include "strbuf.h"

void reset_pack_idx_option(struct pack_idx_option *opts)
{
	memset(opts, 0, sizeof(*opts));
	opts->version = 2;
	opts->off32_limit = 0x7fffffff;
}

static int sha1_compare(const void *_a, const void *_b)
{
	struct pack_idx_entry *a = *(struct pack_idx_entry **)_a;
	struct pack_idx_entry *b = *(struct pack_idx_entry **)_b;
	return oidcmp(&a->oid, &b->oid);
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

/*
 * The *sha1 contains the pack content SHA1 hash.
 * The objects array passed in will be sorted by SHA1 on exit.
 */
const char *write_idx_file(const char *index_name, struct pack_idx_entry **objects,
			   int nr_objects, const struct pack_idx_option *opts,
			   const unsigned char *sha1)
{
	struct hashfile *f;
	struct pack_idx_entry **sorted_by_sha, **list, **last;
	off_t last_obj_offset = 0;
	int i, fd;
	uint32_t index_version;

	if (nr_objects) {
		sorted_by_sha = objects;
		list = sorted_by_sha;
		last = sorted_by_sha + nr_objects;
		for (i = 0; i < nr_objects; ++i) {
			if (objects[i]->offset > last_obj_offset)
				last_obj_offset = objects[i]->offset;
		}
		QSORT(sorted_by_sha, nr_objects, sha1_compare);
	}
	else
		sorted_by_sha = list = last = NULL;

	if (opts->flags & WRITE_IDX_VERIFY) {
		assert(index_name);
		f = hashfd_check(index_name);
	} else {
		if (!index_name) {
			struct strbuf tmp_file = STRBUF_INIT;
			fd = odb_mkstemp(&tmp_file, "pack/tmp_idx_XXXXXX");
			index_name = strbuf_detach(&tmp_file, NULL);
		} else {
			unlink(index_name);
			fd = xopen(index_name, O_CREAT|O_EXCL|O_WRONLY, 0600);
		}
		f = hashfd(fd, index_name);
	}

	/* if last object's offset is >= 2^31 we should use index V2 */
	index_version = need_large_offset(last_obj_offset, opts) ? 2 : opts->version;

	/* index versions 2 and above need a header */
	if (index_version >= 2) {
		struct pack_idx_header hdr;
		hdr.idx_signature = htonl(PACK_IDX_SIGNATURE);
		hdr.idx_version = htonl(index_version);
		hashwrite(f, &hdr, sizeof(hdr));
	}

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

	/*
	 * Write the actual SHA1 entries..
	 */
	list = sorted_by_sha;
	for (i = 0; i < nr_objects; i++) {
		struct pack_idx_entry *obj = *list++;
		if (index_version < 2)
			hashwrite_be32(f, obj->offset);
		hashwrite(f, obj->oid.hash, the_hash_algo->rawsz);
		if ((opts->flags & WRITE_IDX_STRICT) &&
		    (i && oideq(&list[-2]->oid, &obj->oid)))
			die("The same object %s appears twice in the pack",
			    oid_to_hex(&obj->oid));
	}

	if (index_version >= 2) {
		unsigned int nr_large_offset = 0;

		/* write the crc32 table */
		list = sorted_by_sha;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			hashwrite_be32(f, obj->crc32);
		}

		/* write the 32-bit offset table */
		list = sorted_by_sha;
		for (i = 0; i < nr_objects; i++) {
			struct pack_idx_entry *obj = *list++;
			uint32_t offset;

			offset = (need_large_offset(obj->offset, opts)
				  ? (0x80000000 | nr_large_offset++)
				  : obj->offset);
			hashwrite_be32(f, offset);
		}

		/* write the large offset table */
		list = sorted_by_sha;
		while (nr_large_offset) {
			struct pack_idx_entry *obj = *list++;
			uint64_t offset = obj->offset;

			if (!need_large_offset(offset, opts))
				continue;
			hashwrite_be64(f, offset);
			nr_large_offset--;
		}
	}

	hashwrite(f, sha1, the_hash_algo->rawsz);
	finalize_hashfile(f, NULL, FSYNC_COMPONENT_PACK_METADATA,
			  CSUM_HASH_IN_STREAM | CSUM_CLOSE |
			  ((opts->flags & WRITE_IDX_VERIFY) ? 0 : CSUM_FSYNC));
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

static void write_rev_header(struct hashfile *f)
{
	hashwrite_be32(f, RIDX_SIGNATURE);
	hashwrite_be32(f, RIDX_VERSION);
	hashwrite_be32(f, oid_version(the_hash_algo));
}

static void write_rev_index_positions(struct hashfile *f,
				      uint32_t *pack_order,
				      uint32_t nr_objects)
{
	uint32_t i;
	for (i = 0; i < nr_objects; i++)
		hashwrite_be32(f, pack_order[i]);
}

static void write_rev_trailer(struct hashfile *f, const unsigned char *hash)
{
	hashwrite(f, hash, the_hash_algo->rawsz);
}

const char *write_rev_file(const char *rev_name,
			   struct pack_idx_entry **objects,
			   uint32_t nr_objects,
			   const unsigned char *hash,
			   unsigned flags)
{
	uint32_t *pack_order;
	uint32_t i;
	const char *ret;

	if (!(flags & WRITE_REV) && !(flags & WRITE_REV_VERIFY))
		return NULL;

	ALLOC_ARRAY(pack_order, nr_objects);
	for (i = 0; i < nr_objects; i++)
		pack_order[i] = i;
	QSORT_S(pack_order, nr_objects, pack_order_cmp, objects);

	ret = write_rev_file_order(rev_name, pack_order, nr_objects, hash,
				   flags);

	free(pack_order);

	return ret;
}

const char *write_rev_file_order(const char *rev_name,
				 uint32_t *pack_order,
				 uint32_t nr_objects,
				 const unsigned char *hash,
				 unsigned flags)
{
	struct hashfile *f;
	int fd;

	if ((flags & WRITE_REV) && (flags & WRITE_REV_VERIFY))
		die(_("cannot both write and verify reverse index"));

	if (flags & WRITE_REV) {
		if (!rev_name) {
			struct strbuf tmp_file = STRBUF_INIT;
			fd = odb_mkstemp(&tmp_file, "pack/tmp_rev_XXXXXX");
			rev_name = strbuf_detach(&tmp_file, NULL);
		} else {
			unlink(rev_name);
			fd = xopen(rev_name, O_CREAT|O_EXCL|O_WRONLY, 0600);
		}
		f = hashfd(fd, rev_name);
	} else if (flags & WRITE_REV_VERIFY) {
		struct stat statbuf;
		if (stat(rev_name, &statbuf)) {
			if (errno == ENOENT) {
				/* .rev files are optional */
				return NULL;
			} else
				die_errno(_("could not stat: %s"), rev_name);
		}
		f = hashfd_check(rev_name);
	} else
		return NULL;

	write_rev_header(f);

	write_rev_index_positions(f, pack_order, nr_objects);
	write_rev_trailer(f, hash);

	if (rev_name && adjust_shared_perm(rev_name) < 0)
		die(_("failed to make %s readable"), rev_name);

	finalize_hashfile(f, NULL, FSYNC_COMPONENT_PACK_METADATA,
			  CSUM_HASH_IN_STREAM | CSUM_CLOSE |
			  ((flags & WRITE_IDX_VERIFY) ? 0 : CSUM_FSYNC));

	return rev_name;
}

static void write_mtimes_header(struct hashfile *f)
{
	hashwrite_be32(f, MTIMES_SIGNATURE);
	hashwrite_be32(f, MTIMES_VERSION);
	hashwrite_be32(f, oid_version(the_hash_algo));
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

static void write_mtimes_trailer(struct hashfile *f, const unsigned char *hash)
{
	hashwrite(f, hash, the_hash_algo->rawsz);
}

static char *write_mtimes_file(struct packing_data *to_pack,
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

	fd = odb_mkstemp(&tmp_file, "pack/tmp_mtimes_XXXXXX");
	mtimes_name = strbuf_detach(&tmp_file, NULL);
	f = hashfd(fd, mtimes_name);

	write_mtimes_header(f);
	write_mtimes_objects(f, to_pack, objects, nr_objects);
	write_mtimes_trailer(f, hash);

	if (adjust_shared_perm(mtimes_name) < 0)
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
void fixup_pack_header_footer(int pack_fd,
			 unsigned char *new_pack_hash,
			 const char *pack_name,
			 uint32_t object_count,
			 unsigned char *partial_pack_hash,
			 off_t partial_pack_offset)
{
	int aligned_sz, buf_sz = 8 * 1024;
	git_hash_ctx old_hash_ctx, new_hash_ctx;
	struct pack_header hdr;
	char *buf;
	ssize_t read_result;

	the_hash_algo->init_fn(&old_hash_ctx);
	the_hash_algo->init_fn(&new_hash_ctx);

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
	the_hash_algo->update_fn(&old_hash_ctx, &hdr, sizeof(hdr));
	hdr.hdr_entries = htonl(object_count);
	the_hash_algo->update_fn(&new_hash_ctx, &hdr, sizeof(hdr));
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
		the_hash_algo->update_fn(&new_hash_ctx, buf, n);

		aligned_sz -= n;
		if (!aligned_sz)
			aligned_sz = buf_sz;

		if (!partial_pack_hash)
			continue;

		the_hash_algo->update_fn(&old_hash_ctx, buf, n);
		partial_pack_offset -= n;
		if (partial_pack_offset == 0) {
			unsigned char hash[GIT_MAX_RAWSZ];
			the_hash_algo->final_fn(hash, &old_hash_ctx);
			if (!hasheq(hash, partial_pack_hash,
				    the_repository->hash_algo))
				die("Unexpected checksum for %s "
				    "(disk corruption?)", pack_name);
			/*
			 * Now let's compute the SHA1 of the remainder of the
			 * pack, which also means making partial_pack_offset
			 * big enough not to matter anymore.
			 */
			the_hash_algo->init_fn(&old_hash_ctx);
			partial_pack_offset = ~partial_pack_offset;
			partial_pack_offset -= MSB(partial_pack_offset, 1);
		}
	}
	free(buf);

	if (partial_pack_hash)
		the_hash_algo->final_fn(partial_pack_hash, &old_hash_ctx);
	the_hash_algo->final_fn(new_pack_hash, &new_hash_ctx);
	write_or_die(pack_fd, new_pack_hash, the_hash_algo->rawsz);
	fsync_component_or_die(FSYNC_COMPONENT_PACK, pack_fd, pack_name);
}

char *index_pack_lockfile(int ip_out, int *is_well_formed)
{
	char packname[GIT_MAX_HEXSZ + 6];
	const int len = the_hash_algo->hexsz + 6;

	/*
	 * The first thing we expect from index-pack's output
	 * is "pack\t%40s\n" or "keep\t%40s\n" (46 bytes) where
	 * %40s is the newly created pack SHA1 name.  In the "keep"
	 * case, we need it to remove the corresponding .keep file
	 * later on.  If we don't get that then tough luck with it.
	 */
	if (read_in_full(ip_out, packname, len) == len && packname[len-1] == '\n') {
		const char *name;

		if (is_well_formed)
			*is_well_formed = 1;
		packname[len-1] = 0;
		if (skip_prefix(packname, "keep\t", &name))
			return xstrfmt("%s/pack/pack-%s.keep",
				       get_object_directory(), name);
		return NULL;
	}
	if (is_well_formed)
		*is_well_formed = 0;
	return NULL;
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

struct hashfile *create_tmp_packfile(char **pack_tmp_name)
{
	struct strbuf tmpname = STRBUF_INIT;
	int fd;

	fd = odb_mkstemp(&tmpname, "pack/tmp_pack_XXXXXX");
	*pack_tmp_name = strbuf_detach(&tmpname, NULL);
	return hashfd(fd, *pack_tmp_name);
}

static void rename_tmp_packfile(struct strbuf *name_prefix, const char *source,
				const char *ext)
{
	size_t name_prefix_len = name_prefix->len;

	strbuf_addstr(name_prefix, ext);
	if (rename(source, name_prefix->buf))
		die_errno("unable to rename temporary file to '%s'",
			  name_prefix->buf);
	strbuf_setlen(name_prefix, name_prefix_len);
}

void rename_tmp_packfile_idx(struct strbuf *name_buffer,
			     char **idx_tmp_name)
{
	rename_tmp_packfile(name_buffer, *idx_tmp_name, "idx");
}

void stage_tmp_packfiles(struct strbuf *name_buffer,
			 const char *pack_tmp_name,
			 struct pack_idx_entry **written_list,
			 uint32_t nr_written,
			 struct packing_data *to_pack,
			 struct pack_idx_option *pack_idx_opts,
			 unsigned char hash[],
			 char **idx_tmp_name)
{
	const char *rev_tmp_name = NULL;
	char *mtimes_tmp_name = NULL;

	if (adjust_shared_perm(pack_tmp_name))
		die_errno("unable to make temporary pack file readable");

	*idx_tmp_name = (char *)write_idx_file(NULL, written_list, nr_written,
					       pack_idx_opts, hash);
	if (adjust_shared_perm(*idx_tmp_name))
		die_errno("unable to make temporary index file readable");

	rev_tmp_name = write_rev_file(NULL, written_list, nr_written, hash,
				      pack_idx_opts->flags);

	if (pack_idx_opts->flags & WRITE_MTIMES) {
		mtimes_tmp_name = write_mtimes_file(to_pack, written_list,
						    nr_written,
						    hash);
	}

	rename_tmp_packfile(name_buffer, pack_tmp_name, "pack");
	if (rev_tmp_name)
		rename_tmp_packfile(name_buffer, rev_tmp_name, "rev");
	if (mtimes_tmp_name)
		rename_tmp_packfile(name_buffer, mtimes_tmp_name, "mtimes");

	free((char *)rev_tmp_name);
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
