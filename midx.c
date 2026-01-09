#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "config.h"
#include "dir.h"
#include "hex.h"
#include "packfile.h"
#include "hash-lookup.h"
#include "midx.h"
#include "progress.h"
#include "trace2.h"
#include "chunk-format.h"
#include "pack-bitmap.h"
#include "pack-revindex.h"

#define MIDX_PACK_ERROR ((void *)(intptr_t)-1)

int midx_checksum_valid(struct multi_pack_index *m);
void clear_midx_files_ext(struct odb_source *source, const char *ext,
			  const char *keep_hash);
void clear_incremental_midx_files_ext(struct odb_source *source, const char *ext,
				      char **keep_hashes,
				      uint32_t hashes_nr);
int cmp_idx_or_pack_name(const char *idx_or_pack_name,
			 const char *idx_name);

const unsigned char *get_midx_checksum(struct multi_pack_index *m)
{
	return m->data + m->data_len - m->source->odb->repo->hash_algo->rawsz;
}

void get_midx_filename(struct odb_source *source, struct strbuf *out)
{
	get_midx_filename_ext(source, out, NULL, NULL);
}

void get_midx_filename_ext(struct odb_source *source, struct strbuf *out,
			   const unsigned char *hash, const char *ext)
{
	strbuf_addf(out, "%s/pack/multi-pack-index", source->path);
	if (ext)
		strbuf_addf(out, "-%s.%s", hash_to_hex_algop(hash, source->odb->repo->hash_algo), ext);
}

static int midx_read_oid_fanout(const unsigned char *chunk_start,
				size_t chunk_size, void *data)
{
	int i;
	struct multi_pack_index *m = data;
	m->chunk_oid_fanout = (uint32_t *)chunk_start;

	if (chunk_size != 4 * 256) {
		error(_("multi-pack-index OID fanout is of the wrong size"));
		return 1;
	}
	for (i = 0; i < 255; i++) {
		uint32_t oid_fanout1 = ntohl(m->chunk_oid_fanout[i]);
		uint32_t oid_fanout2 = ntohl(m->chunk_oid_fanout[i+1]);

		if (oid_fanout1 > oid_fanout2) {
			error(_("oid fanout out of order: fanout[%d] = %"PRIx32" > %"PRIx32" = fanout[%d]"),
			      i, oid_fanout1, oid_fanout2, i + 1);
			return 1;
		}
	}
	m->num_objects = ntohl(m->chunk_oid_fanout[255]);
	return 0;
}

static int midx_read_oid_lookup(const unsigned char *chunk_start,
				size_t chunk_size, void *data)
{
	struct multi_pack_index *m = data;
	m->chunk_oid_lookup = chunk_start;

	if (chunk_size != st_mult(m->hash_len, m->num_objects)) {
		error(_("multi-pack-index OID lookup chunk is the wrong size"));
		return 1;
	}
	return 0;
}

static int midx_read_object_offsets(const unsigned char *chunk_start,
				    size_t chunk_size, void *data)
{
	struct multi_pack_index *m = data;
	m->chunk_object_offsets = chunk_start;

	if (chunk_size != st_mult(m->num_objects, MIDX_CHUNK_OFFSET_WIDTH)) {
		error(_("multi-pack-index object offset chunk is the wrong size"));
		return 1;
	}
	return 0;
}

struct multi_pack_index *get_multi_pack_index(struct odb_source *source)
{
	packfile_store_prepare(source->packfiles);
	return source->packfiles->midx;
}

static struct multi_pack_index *load_multi_pack_index_one(struct odb_source *source,
							  const char *midx_name)
{
	struct repository *r = source->odb->repo;
	struct multi_pack_index *m = NULL;
	int fd;
	struct stat st;
	size_t midx_size;
	void *midx_map = NULL;
	uint32_t hash_version;
	uint32_t i;
	const char *cur_pack_name;
	struct chunkfile *cf = NULL;

	fd = git_open(midx_name);

	if (fd < 0)
		goto cleanup_fail;
	if (fstat(fd, &st)) {
		error_errno(_("failed to read %s"), midx_name);
		goto cleanup_fail;
	}

	midx_size = xsize_t(st.st_size);

	if (midx_size < (MIDX_HEADER_SIZE + r->hash_algo->rawsz)) {
		error(_("multi-pack-index file %s is too small"), midx_name);
		goto cleanup_fail;
	}

	midx_map = xmmap(NULL, midx_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	CALLOC_ARRAY(m, 1);
	m->data = midx_map;
	m->data_len = midx_size;
	m->source = source;

	m->signature = get_be32(m->data);
	if (m->signature != MIDX_SIGNATURE)
		die(_("multi-pack-index signature 0x%08x does not match signature 0x%08x"),
		      m->signature, MIDX_SIGNATURE);

	m->version = m->data[MIDX_BYTE_FILE_VERSION];
	if (m->version != MIDX_VERSION)
		die(_("multi-pack-index version %d not recognized"),
		      m->version);

	hash_version = m->data[MIDX_BYTE_HASH_VERSION];
	if (hash_version != oid_version(r->hash_algo)) {
		error(_("multi-pack-index hash version %u does not match version %u"),
		      hash_version, oid_version(r->hash_algo));
		goto cleanup_fail;
	}
	m->hash_len = r->hash_algo->rawsz;

	m->num_chunks = m->data[MIDX_BYTE_NUM_CHUNKS];

	m->num_packs = get_be32(m->data + MIDX_BYTE_NUM_PACKS);

	m->preferred_pack_idx = -1;

	cf = init_chunkfile(NULL);

	if (read_table_of_contents(cf, m->data, midx_size,
				   MIDX_HEADER_SIZE, m->num_chunks,
				   MIDX_CHUNK_ALIGNMENT))
		goto cleanup_fail;

	if (pair_chunk(cf, MIDX_CHUNKID_PACKNAMES, &m->chunk_pack_names, &m->chunk_pack_names_len))
		die(_("multi-pack-index required pack-name chunk missing or corrupted"));
	if (read_chunk(cf, MIDX_CHUNKID_OIDFANOUT, midx_read_oid_fanout, m))
		die(_("multi-pack-index required OID fanout chunk missing or corrupted"));
	if (read_chunk(cf, MIDX_CHUNKID_OIDLOOKUP, midx_read_oid_lookup, m))
		die(_("multi-pack-index required OID lookup chunk missing or corrupted"));
	if (read_chunk(cf, MIDX_CHUNKID_OBJECTOFFSETS, midx_read_object_offsets, m))
		die(_("multi-pack-index required object offsets chunk missing or corrupted"));

	pair_chunk(cf, MIDX_CHUNKID_LARGEOFFSETS, &m->chunk_large_offsets,
		   &m->chunk_large_offsets_len);
	if (git_env_bool("GIT_TEST_MIDX_READ_BTMP", 1))
		pair_chunk(cf, MIDX_CHUNKID_BITMAPPEDPACKS,
			   (const unsigned char **)&m->chunk_bitmapped_packs,
			   &m->chunk_bitmapped_packs_len);

	if (git_env_bool("GIT_TEST_MIDX_READ_RIDX", 1))
		pair_chunk(cf, MIDX_CHUNKID_REVINDEX, &m->chunk_revindex,
			   &m->chunk_revindex_len);

	CALLOC_ARRAY(m->pack_names, m->num_packs);
	CALLOC_ARRAY(m->packs, m->num_packs);

	cur_pack_name = (const char *)m->chunk_pack_names;
	for (i = 0; i < m->num_packs; i++) {
		const char *end;
		size_t avail = m->chunk_pack_names_len -
				(cur_pack_name - (const char *)m->chunk_pack_names);

		m->pack_names[i] = cur_pack_name;

		end = memchr(cur_pack_name, '\0', avail);
		if (!end)
			die(_("multi-pack-index pack-name chunk is too short"));
		cur_pack_name = end + 1;

		if (i && strcmp(m->pack_names[i], m->pack_names[i - 1]) <= 0)
			die(_("multi-pack-index pack names out of order: '%s' before '%s'"),
			      m->pack_names[i - 1],
			      m->pack_names[i]);
	}

	trace2_data_intmax("midx", r, "load/num_packs", m->num_packs);
	trace2_data_intmax("midx", r, "load/num_objects", m->num_objects);

	free_chunkfile(cf);
	return m;

cleanup_fail:
	free(m);
	free_chunkfile(cf);
	if (midx_map)
		munmap(midx_map, midx_size);
	if (0 <= fd)
		close(fd);
	return NULL;
}

void get_midx_chain_dirname(struct odb_source *source, struct strbuf *buf)
{
	strbuf_addf(buf, "%s/pack/multi-pack-index.d", source->path);
}

void get_midx_chain_filename(struct odb_source *source, struct strbuf *buf)
{
	get_midx_chain_dirname(source, buf);
	strbuf_addstr(buf, "/multi-pack-index-chain");
}

void get_split_midx_filename_ext(struct odb_source *source, struct strbuf *buf,
				 const unsigned char *hash, const char *ext)
{
	get_midx_chain_dirname(source, buf);
	strbuf_addf(buf, "/multi-pack-index-%s.%s",
		    hash_to_hex_algop(hash, source->odb->repo->hash_algo), ext);
}

static int open_multi_pack_index_chain(const struct git_hash_algo *hash_algo,
				       const char *chain_file, int *fd,
				       struct stat *st)
{
	*fd = git_open(chain_file);
	if (*fd < 0)
		return 0;
	if (fstat(*fd, st)) {
		close(*fd);
		return 0;
	}
	if (st->st_size < hash_algo->hexsz) {
		close(*fd);
		if (!st->st_size) {
			/* treat empty files the same as missing */
			errno = ENOENT;
		} else {
			warning(_("multi-pack-index chain file too small"));
			errno = EINVAL;
		}
		return 0;
	}
	return 1;
}

static int add_midx_to_chain(struct multi_pack_index *midx,
			     struct multi_pack_index *midx_chain)
{
	if (midx_chain) {
		if (unsigned_add_overflows(midx_chain->num_packs,
					   midx_chain->num_packs_in_base)) {
			warning(_("pack count in base MIDX too high: %"PRIuMAX),
				(uintmax_t)midx_chain->num_packs_in_base);
			return 0;
		}
		if (unsigned_add_overflows(midx_chain->num_objects,
					   midx_chain->num_objects_in_base)) {
			warning(_("object count in base MIDX too high: %"PRIuMAX),
				(uintmax_t)midx_chain->num_objects_in_base);
			return 0;
		}
		midx->num_packs_in_base = midx_chain->num_packs +
			midx_chain->num_packs_in_base;
		midx->num_objects_in_base = midx_chain->num_objects +
			midx_chain->num_objects_in_base;
	}

	midx->base_midx = midx_chain;
	midx->has_chain = 1;

	return 1;
}

static struct multi_pack_index *load_midx_chain_fd_st(struct odb_source *source,
						      int fd, struct stat *st,
						      int *incomplete_chain)
{
	const struct git_hash_algo *hash_algo = source->odb->repo->hash_algo;
	struct multi_pack_index *midx_chain = NULL;
	struct strbuf buf = STRBUF_INIT;
	int valid = 1;
	uint32_t i, count;
	FILE *fp = xfdopen(fd, "r");

	count = st->st_size / (hash_algo->hexsz + 1);

	for (i = 0; i < count; i++) {
		struct multi_pack_index *m;
		struct object_id layer;

		if (strbuf_getline_lf(&buf, fp) == EOF)
			break;

		if (get_oid_hex_algop(buf.buf, &layer, hash_algo)) {
			warning(_("invalid multi-pack-index chain: line '%s' "
				  "not a hash"),
				buf.buf);
			valid = 0;
			break;
		}

		valid = 0;

		strbuf_reset(&buf);
		get_split_midx_filename_ext(source, &buf,
					    layer.hash, MIDX_EXT_MIDX);
		m = load_multi_pack_index_one(source, buf.buf);

		if (m) {
			if (add_midx_to_chain(m, midx_chain)) {
				midx_chain = m;
				valid = 1;
			} else {
				close_midx(m);
			}
		}
		if (!valid) {
			warning(_("unable to find all multi-pack index files"));
			break;
		}
	}

	fclose(fp);
	strbuf_release(&buf);

	*incomplete_chain = !valid;
	return midx_chain;
}

static struct multi_pack_index *load_multi_pack_index_chain(struct odb_source *source)
{
	struct strbuf chain_file = STRBUF_INIT;
	struct stat st;
	int fd;
	struct multi_pack_index *m = NULL;

	get_midx_chain_filename(source, &chain_file);
	if (open_multi_pack_index_chain(source->odb->repo->hash_algo, chain_file.buf, &fd, &st)) {
		int incomplete;
		/* ownership of fd is taken over by load function */
		m = load_midx_chain_fd_st(source, fd, &st, &incomplete);
	}

	strbuf_release(&chain_file);
	return m;
}

struct multi_pack_index *load_multi_pack_index(struct odb_source *source)
{
	struct strbuf midx_name = STRBUF_INIT;
	struct multi_pack_index *m;

	get_midx_filename(source, &midx_name);

	m = load_multi_pack_index_one(source, midx_name.buf);
	if (!m)
		m = load_multi_pack_index_chain(source);

	strbuf_release(&midx_name);

	return m;
}

void close_midx(struct multi_pack_index *m)
{
	uint32_t i;

	if (!m)
		return;

	close_midx(m->base_midx);

	munmap((unsigned char *)m->data, m->data_len);

	for (i = 0; i < m->num_packs; i++) {
		if (m->packs[i] && m->packs[i] != MIDX_PACK_ERROR)
			m->packs[i]->multi_pack_index = 0;
	}
	FREE_AND_NULL(m->packs);
	FREE_AND_NULL(m->pack_names);
	free(m);
}

static uint32_t midx_for_object(struct multi_pack_index **_m, uint32_t pos)
{
	struct multi_pack_index *m = *_m;
	while (m && pos < m->num_objects_in_base)
		m = m->base_midx;

	if (!m)
		BUG("NULL multi-pack-index for object position: %"PRIu32, pos);

	if (pos >= m->num_objects + m->num_objects_in_base)
		die(_("invalid MIDX object position, MIDX is likely corrupt"));

	*_m = m;

	return pos - m->num_objects_in_base;
}

static uint32_t midx_for_pack(struct multi_pack_index **_m,
			      uint32_t pack_int_id)
{
	struct multi_pack_index *m = *_m;
	while (m && pack_int_id < m->num_packs_in_base)
		m = m->base_midx;

	if (!m)
		BUG("NULL multi-pack-index for pack ID: %"PRIu32, pack_int_id);

	if (pack_int_id >= m->num_packs + m->num_packs_in_base)
		die(_("bad pack-int-id: %u (%u total packs)"),
		    pack_int_id, m->num_packs + m->num_packs_in_base);

	*_m = m;

	return pack_int_id - m->num_packs_in_base;
}

int prepare_midx_pack(struct multi_pack_index *m,
		      uint32_t pack_int_id)
{
	struct strbuf pack_name = STRBUF_INIT;
	struct packed_git *p;

	pack_int_id = midx_for_pack(&m, pack_int_id);

	if (m->packs[pack_int_id] == MIDX_PACK_ERROR)
		return 1;
	if (m->packs[pack_int_id])
		return 0;

	strbuf_addf(&pack_name, "%s/pack/%s", m->source->path,
		    m->pack_names[pack_int_id]);
	p = packfile_store_load_pack(m->source->packfiles,
				     pack_name.buf, m->source->local);
	strbuf_release(&pack_name);

	if (!p) {
		m->packs[pack_int_id] = MIDX_PACK_ERROR;
		return 1;
	}

	p->multi_pack_index = 1;
	m->packs[pack_int_id] = p;

	return 0;
}

struct packed_git *nth_midxed_pack(struct multi_pack_index *m,
				   uint32_t pack_int_id)
{
	uint32_t local_pack_int_id = midx_for_pack(&m, pack_int_id);
	if (m->packs[local_pack_int_id] == MIDX_PACK_ERROR)
		return NULL;
	return m->packs[local_pack_int_id];
}

#define MIDX_CHUNK_BITMAPPED_PACKS_WIDTH (2 * sizeof(uint32_t))

int nth_bitmapped_pack(struct multi_pack_index *m,
		       struct bitmapped_pack *bp, uint32_t pack_int_id)
{
	uint32_t local_pack_int_id = midx_for_pack(&m, pack_int_id);

	if (!m->chunk_bitmapped_packs)
		return error(_("MIDX does not contain the BTMP chunk"));

	if (prepare_midx_pack(m, pack_int_id))
		return error(_("could not load bitmapped pack %"PRIu32), pack_int_id);

	bp->p = m->packs[local_pack_int_id];
	bp->bitmap_pos = get_be32((char *)m->chunk_bitmapped_packs +
				  MIDX_CHUNK_BITMAPPED_PACKS_WIDTH * local_pack_int_id);
	bp->bitmap_nr = get_be32((char *)m->chunk_bitmapped_packs +
				 MIDX_CHUNK_BITMAPPED_PACKS_WIDTH * local_pack_int_id +
				 sizeof(uint32_t));
	bp->pack_int_id = pack_int_id;
	bp->from_midx = m;

	return 0;
}

int bsearch_one_midx(const struct object_id *oid, struct multi_pack_index *m,
		     uint32_t *result)
{
	int ret = bsearch_hash(oid->hash, m->chunk_oid_fanout,
			       m->chunk_oid_lookup,
			       m->source->odb->repo->hash_algo->rawsz,
			       result);
	if (result)
		*result += m->num_objects_in_base;
	return ret;
}

int bsearch_midx(const struct object_id *oid, struct multi_pack_index *m,
		 uint32_t *result)
{
	for (; m; m = m->base_midx)
		if (bsearch_one_midx(oid, m, result))
			return 1;
	return 0;
}

int midx_has_oid(struct multi_pack_index *m, const struct object_id *oid)
{
	return bsearch_midx(oid, m, NULL);
}

struct object_id *nth_midxed_object_oid(struct object_id *oid,
					struct multi_pack_index *m,
					uint32_t n)
{
	if (n >= m->num_objects + m->num_objects_in_base)
		return NULL;

	n = midx_for_object(&m, n);

	oidread(oid, m->chunk_oid_lookup + st_mult(m->hash_len, n),
		m->source->odb->repo->hash_algo);
	return oid;
}

off_t nth_midxed_offset(struct multi_pack_index *m, uint32_t pos)
{
	const unsigned char *offset_data;
	uint32_t offset32;

	pos = midx_for_object(&m, pos);

	offset_data = m->chunk_object_offsets + (off_t)pos * MIDX_CHUNK_OFFSET_WIDTH;
	offset32 = get_be32(offset_data + sizeof(uint32_t));

	if (m->chunk_large_offsets && offset32 & MIDX_LARGE_OFFSET_NEEDED) {
		if (sizeof(off_t) < sizeof(uint64_t))
			die(_("multi-pack-index stores a 64-bit offset, but off_t is too small"));

		offset32 ^= MIDX_LARGE_OFFSET_NEEDED;
		if (offset32 >= m->chunk_large_offsets_len / sizeof(uint64_t))
			die(_("multi-pack-index large offset out of bounds"));
		return get_be64(m->chunk_large_offsets + sizeof(uint64_t) * offset32);
	}

	return offset32;
}

uint32_t nth_midxed_pack_int_id(struct multi_pack_index *m, uint32_t pos)
{
	pos = midx_for_object(&m, pos);

	return m->num_packs_in_base + get_be32(m->chunk_object_offsets +
					       (off_t)pos * MIDX_CHUNK_OFFSET_WIDTH);
}

int fill_midx_entry(struct multi_pack_index *m,
		    const struct object_id *oid,
		    struct pack_entry *e)
{
	uint32_t pos;
	uint32_t pack_int_id;
	struct packed_git *p;

	if (!bsearch_midx(oid, m, &pos))
		return 0;

	midx_for_object(&m, pos);
	pack_int_id = nth_midxed_pack_int_id(m, pos);

	if (prepare_midx_pack(m, pack_int_id))
		return 0;
	p = m->packs[pack_int_id - m->num_packs_in_base];

	/*
	* We are about to tell the caller where they can locate the
	* requested object.  We better make sure the packfile is
	* still here and can be accessed before supplying that
	* answer, as it may have been deleted since the MIDX was
	* loaded!
	*/
	if (!is_pack_valid(p))
		return 0;

	if (oidset_size(&p->bad_objects) &&
	    oidset_contains(&p->bad_objects, oid))
		return 0;

	e->offset = nth_midxed_offset(m, pos);
	e->p = p;

	return 1;
}

/* Match "foo.idx" against either "foo.pack" _or_ "foo.idx". */
int cmp_idx_or_pack_name(const char *idx_or_pack_name,
			 const char *idx_name)
{
	/* Skip past any initial matching prefix. */
	while (*idx_name && *idx_name == *idx_or_pack_name) {
		idx_name++;
		idx_or_pack_name++;
	}

	/*
	 * If we didn't match completely, we may have matched "pack-1234." and
	 * be left with "idx" and "pack" respectively, which is also OK. We do
	 * not have to check for "idx" and "idx", because that would have been
	 * a complete match (and in that case these strcmps will be false, but
	 * we'll correctly return 0 from the final strcmp() below.
	 *
	 * Technically this matches "fooidx" and "foopack", but we'd never have
	 * such names in the first place.
	 */
	if (!strcmp(idx_name, "idx") && !strcmp(idx_or_pack_name, "pack"))
		return 0;

	/*
	 * This not only checks for a complete match, but also orders based on
	 * the first non-identical character, which means our ordering will
	 * match a raw strcmp(). That makes it OK to use this to binary search
	 * a naively-sorted list.
	 */
	return strcmp(idx_or_pack_name, idx_name);
}

static int midx_contains_pack_1(struct multi_pack_index *m,
				const char *idx_or_pack_name)
{
	uint32_t first = 0, last = m->num_packs;

	while (first < last) {
		uint32_t mid = first + (last - first) / 2;
		const char *current;
		int cmp;

		current = m->pack_names[mid];
		cmp = cmp_idx_or_pack_name(idx_or_pack_name, current);
		if (!cmp)
			return 1;
		if (cmp > 0) {
			first = mid + 1;
			continue;
		}
		last = mid;
	}

	return 0;
}

int midx_contains_pack(struct multi_pack_index *m, const char *idx_or_pack_name)
{
	for (; m; m = m->base_midx)
		if (midx_contains_pack_1(m, idx_or_pack_name))
			return 1;
	return 0;
}

int midx_preferred_pack(struct multi_pack_index *m, uint32_t *pack_int_id)
{
	if (m->preferred_pack_idx == -1) {
		uint32_t midx_pos;
		if (load_midx_revindex(m) < 0) {
			m->preferred_pack_idx = -2;
			return -1;
		}

		midx_pos = pack_pos_to_midx(m, m->num_objects_in_base);

		m->preferred_pack_idx = nth_midxed_pack_int_id(m, midx_pos);

	} else if (m->preferred_pack_idx == -2)
		return -1; /* no revindex */

	*pack_int_id = m->preferred_pack_idx;
	return 0;
}

int prepare_multi_pack_index_one(struct odb_source *source)
{
	struct repository *r = source->odb->repo;

	prepare_repo_settings(r);
	if (!r->settings.core_multi_pack_index)
		return 0;

	if (source->packfiles->midx)
		return 1;

	source->packfiles->midx = load_multi_pack_index(source);

	return !!source->packfiles->midx;
}

int midx_checksum_valid(struct multi_pack_index *m)
{
	return hashfile_checksum_valid(m->source->odb->repo->hash_algo,
				       m->data, m->data_len);
}

struct clear_midx_data {
	char **keep;
	uint32_t keep_nr;
	const char *ext;
};

static void clear_midx_file_ext(const char *full_path, size_t full_path_len UNUSED,
				const char *file_name, void *_data)
{
	struct clear_midx_data *data = _data;
	uint32_t i;

	if (!(starts_with(file_name, "multi-pack-index-") &&
	      ends_with(file_name, data->ext)))
		return;
	for (i = 0; i < data->keep_nr; i++) {
		if (!strcmp(data->keep[i], file_name))
			return;
	}
	if (unlink(full_path))
		die_errno(_("failed to remove %s"), full_path);
}

void clear_midx_files_ext(struct odb_source *source, const char *ext,
			  const char *keep_hash)
{
	struct clear_midx_data data;
	memset(&data, 0, sizeof(struct clear_midx_data));

	if (keep_hash) {
		ALLOC_ARRAY(data.keep, 1);

		data.keep[0] = xstrfmt("multi-pack-index-%s.%s", keep_hash, ext);
		data.keep_nr = 1;
	}
	data.ext = ext;

	for_each_file_in_pack_dir(source->path,
				  clear_midx_file_ext,
				  &data);

	if (keep_hash)
		free(data.keep[0]);
	free(data.keep);
}

void clear_incremental_midx_files_ext(struct odb_source *source, const char *ext,
				      char **keep_hashes,
				      uint32_t hashes_nr)
{
	struct clear_midx_data data;
	uint32_t i;

	memset(&data, 0, sizeof(struct clear_midx_data));

	ALLOC_ARRAY(data.keep, hashes_nr);
	for (i = 0; i < hashes_nr; i++)
		data.keep[i] = xstrfmt("multi-pack-index-%s.%s", keep_hashes[i],
				       ext);
	data.keep_nr = hashes_nr;
	data.ext = ext;

	for_each_file_in_pack_subdir(source->path, "multi-pack-index.d",
				     clear_midx_file_ext, &data);

	for (i = 0; i < hashes_nr; i++)
		free(data.keep[i]);
	free(data.keep);
}

void clear_midx_file(struct repository *r)
{
	struct strbuf midx = STRBUF_INIT;

	get_midx_filename(r->objects->sources, &midx);

	if (r->objects) {
		struct odb_source *source;

		for (source = r->objects->sources; source; source = source->next) {
			if (source->packfiles->midx)
				close_midx(source->packfiles->midx);
			source->packfiles->midx = NULL;
		}
	}

	if (remove_path(midx.buf))
		die(_("failed to clear multi-pack-index at %s"), midx.buf);

	clear_midx_files_ext(r->objects->sources, MIDX_EXT_BITMAP, NULL);
	clear_midx_files_ext(r->objects->sources, MIDX_EXT_REV, NULL);

	strbuf_release(&midx);
}

static int verify_midx_error;

__attribute__((format (printf, 1, 2)))
static void midx_report(const char *fmt, ...)
{
	va_list ap;
	verify_midx_error = 1;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

struct pair_pos_vs_id
{
	uint32_t pos;
	uint32_t pack_int_id;
};

static int compare_pair_pos_vs_id(const void *_a, const void *_b)
{
	struct pair_pos_vs_id *a = (struct pair_pos_vs_id *)_a;
	struct pair_pos_vs_id *b = (struct pair_pos_vs_id *)_b;

	return b->pack_int_id - a->pack_int_id;
}

/*
 * Limit calls to display_progress() for performance reasons.
 * The interval here was arbitrarily chosen.
 */
#define SPARSE_PROGRESS_INTERVAL (1 << 12)
#define midx_display_sparse_progress(progress, n) \
	do { \
		uint64_t _n = (n); \
		if ((_n & (SPARSE_PROGRESS_INTERVAL - 1)) == 0) \
			display_progress(progress, _n); \
	} while (0)

int verify_midx_file(struct odb_source *source, unsigned flags)
{
	struct repository *r = source->odb->repo;
	struct pair_pos_vs_id *pairs = NULL;
	uint32_t i;
	struct progress *progress = NULL;
	struct multi_pack_index *m = load_multi_pack_index(source);
	struct multi_pack_index *curr;
	verify_midx_error = 0;

	if (!m) {
		int result = 0;
		struct stat sb;
		struct strbuf filename = STRBUF_INIT;

		get_midx_filename(source, &filename);

		if (!stat(filename.buf, &sb)) {
			error(_("multi-pack-index file exists, but failed to parse"));
			result = 1;
		}
		strbuf_release(&filename);
		return result;
	}

	if (!midx_checksum_valid(m))
		midx_report(_("incorrect checksum"));

	if (flags & MIDX_PROGRESS)
		progress = start_delayed_progress(r,
						  _("Looking for referenced packfiles"),
						  m->num_packs + m->num_packs_in_base);
	for (i = 0; i < m->num_packs + m->num_packs_in_base; i++) {
		if (prepare_midx_pack(m, i))
			midx_report("failed to load pack in position %d", i);

		display_progress(progress, i + 1);
	}
	stop_progress(&progress);

	if (m->num_objects == 0) {
		midx_report(_("the midx contains no oid"));
		/*
		 * Remaining tests assume that we have objects, so we can
		 * return here.
		 */
		goto cleanup;
	}

	if (flags & MIDX_PROGRESS)
		progress = start_sparse_progress(r,
						 _("Verifying OID order in multi-pack-index"),
						 m->num_objects - 1);

	for (curr = m; curr; curr = curr->base_midx) {
		for (i = 0; i < m->num_objects - 1; i++) {
			struct object_id oid1, oid2;

			nth_midxed_object_oid(&oid1, m, m->num_objects_in_base + i);
			nth_midxed_object_oid(&oid2, m, m->num_objects_in_base + i + 1);

			if (oidcmp(&oid1, &oid2) >= 0)
				midx_report(_("oid lookup out of order: oid[%d] = %s >= %s = oid[%d]"),
					    i, oid_to_hex(&oid1), oid_to_hex(&oid2), i + 1);

			midx_display_sparse_progress(progress, i + 1);
		}
	}
	stop_progress(&progress);

	/*
	 * Create an array mapping each object to its packfile id.  Sort it
	 * to group the objects by packfile.  Use this permutation to visit
	 * each of the objects and only require 1 packfile to be open at a
	 * time.
	 */
	ALLOC_ARRAY(pairs, m->num_objects + m->num_objects_in_base);
	for (i = 0; i < m->num_objects + m->num_objects_in_base; i++) {
		pairs[i].pos = i;
		pairs[i].pack_int_id = nth_midxed_pack_int_id(m, i);
	}

	if (flags & MIDX_PROGRESS)
		progress = start_sparse_progress(r,
						 _("Sorting objects by packfile"),
						 m->num_objects);
	display_progress(progress, 0); /* TODO: Measure QSORT() progress */
	QSORT(pairs, m->num_objects, compare_pair_pos_vs_id);
	stop_progress(&progress);

	if (flags & MIDX_PROGRESS)
		progress = start_sparse_progress(r,
						 _("Verifying object offsets"),
						 m->num_objects);
	for (i = 0; i < m->num_objects + m->num_objects_in_base; i++) {
		struct object_id oid;
		struct pack_entry e;
		off_t m_offset, p_offset;

		if (i > 0 && pairs[i-1].pack_int_id != pairs[i].pack_int_id &&
		    nth_midxed_pack(m, pairs[i-1].pack_int_id)) {
			uint32_t pack_int_id = pairs[i-1].pack_int_id;
			struct packed_git *p = nth_midxed_pack(m, pack_int_id);

			close_pack_fd(p);
			close_pack_index(p);
		}

		nth_midxed_object_oid(&oid, m, pairs[i].pos);

		if (!fill_midx_entry(m, &oid, &e)) {
			midx_report(_("failed to load pack entry for oid[%d] = %s"),
				    pairs[i].pos, oid_to_hex(&oid));
			continue;
		}

		if (open_pack_index(e.p)) {
			midx_report(_("failed to load pack-index for packfile %s"),
				    e.p->pack_name);
			break;
		}

		m_offset = e.offset;
		p_offset = find_pack_entry_one(&oid, e.p);

		if (m_offset != p_offset)
			midx_report(_("incorrect object offset for oid[%d] = %s: %"PRIx64" != %"PRIx64),
				    pairs[i].pos, oid_to_hex(&oid), m_offset, p_offset);

		midx_display_sparse_progress(progress, i + 1);
	}
	stop_progress(&progress);

cleanup:
	free(pairs);
	close_midx(m);

	return verify_midx_error;
}
