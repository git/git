#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "config.h"
#include "dir.h"
#include "hex.h"
#include "packfile.h"
#include "object-file.h"
#include "hash-lookup.h"
#include "midx.h"
#include "progress.h"
#include "trace2.h"
#include "chunk-format.h"
#include "pack-bitmap.h"
#include "pack-revindex.h"

int midx_checksum_valid(struct multi_pack_index *m);
void clear_midx_files_ext(const char *object_dir, const char *ext,
			  unsigned char *keep_hash);
int cmp_idx_or_pack_name(const char *idx_or_pack_name,
			 const char *idx_name);

const unsigned char *get_midx_checksum(struct multi_pack_index *m)
{
	return m->data + m->data_len - the_hash_algo->rawsz;
}

void get_midx_filename(struct strbuf *out, const char *object_dir)
{
	get_midx_filename_ext(out, object_dir, NULL, NULL);
}

void get_midx_filename_ext(struct strbuf *out, const char *object_dir,
			   const unsigned char *hash, const char *ext)
{
	strbuf_addf(out, "%s/pack/multi-pack-index", object_dir);
	if (ext)
		strbuf_addf(out, "-%s.%s", hash_to_hex(hash), ext);
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

#define MIDX_MIN_SIZE (MIDX_HEADER_SIZE + the_hash_algo->rawsz)

struct multi_pack_index *load_multi_pack_index(const char *object_dir, int local)
{
	struct multi_pack_index *m = NULL;
	int fd;
	struct stat st;
	size_t midx_size;
	void *midx_map = NULL;
	uint32_t hash_version;
	struct strbuf midx_name = STRBUF_INIT;
	uint32_t i;
	const char *cur_pack_name;
	struct chunkfile *cf = NULL;

	get_midx_filename(&midx_name, object_dir);

	fd = git_open(midx_name.buf);

	if (fd < 0)
		goto cleanup_fail;
	if (fstat(fd, &st)) {
		error_errno(_("failed to read %s"), midx_name.buf);
		goto cleanup_fail;
	}

	midx_size = xsize_t(st.st_size);

	if (midx_size < MIDX_MIN_SIZE) {
		error(_("multi-pack-index file %s is too small"), midx_name.buf);
		goto cleanup_fail;
	}

	strbuf_release(&midx_name);

	midx_map = xmmap(NULL, midx_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	FLEX_ALLOC_STR(m, object_dir, object_dir);
	m->data = midx_map;
	m->data_len = midx_size;
	m->local = local;

	m->signature = get_be32(m->data);
	if (m->signature != MIDX_SIGNATURE)
		die(_("multi-pack-index signature 0x%08x does not match signature 0x%08x"),
		      m->signature, MIDX_SIGNATURE);

	m->version = m->data[MIDX_BYTE_FILE_VERSION];
	if (m->version != MIDX_VERSION)
		die(_("multi-pack-index version %d not recognized"),
		      m->version);

	hash_version = m->data[MIDX_BYTE_HASH_VERSION];
	if (hash_version != oid_version(the_hash_algo)) {
		error(_("multi-pack-index hash version %u does not match version %u"),
		      hash_version, oid_version(the_hash_algo));
		goto cleanup_fail;
	}
	m->hash_len = the_hash_algo->rawsz;

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

	trace2_data_intmax("midx", the_repository, "load/num_packs", m->num_packs);
	trace2_data_intmax("midx", the_repository, "load/num_objects", m->num_objects);

	free_chunkfile(cf);
	return m;

cleanup_fail:
	free(m);
	strbuf_release(&midx_name);
	free_chunkfile(cf);
	if (midx_map)
		munmap(midx_map, midx_size);
	if (0 <= fd)
		close(fd);
	return NULL;
}

void close_midx(struct multi_pack_index *m)
{
	uint32_t i;

	if (!m)
		return;

	close_midx(m->next);

	munmap((unsigned char *)m->data, m->data_len);

	for (i = 0; i < m->num_packs; i++) {
		if (m->packs[i])
			m->packs[i]->multi_pack_index = 0;
	}
	FREE_AND_NULL(m->packs);
	FREE_AND_NULL(m->pack_names);
	free(m);
}

int prepare_midx_pack(struct repository *r, struct multi_pack_index *m, uint32_t pack_int_id)
{
	struct strbuf pack_name = STRBUF_INIT;
	struct packed_git *p;

	if (pack_int_id >= m->num_packs)
		die(_("bad pack-int-id: %u (%u total packs)"),
		    pack_int_id, m->num_packs);

	if (m->packs[pack_int_id])
		return 0;

	strbuf_addf(&pack_name, "%s/pack/%s", m->object_dir,
		    m->pack_names[pack_int_id]);

	p = add_packed_git(pack_name.buf, pack_name.len, m->local);
	strbuf_release(&pack_name);

	if (!p)
		return 1;

	p->multi_pack_index = 1;
	m->packs[pack_int_id] = p;
	install_packed_git(r, p);
	list_add_tail(&p->mru, &r->objects->packed_git_mru);

	return 0;
}

#define MIDX_CHUNK_BITMAPPED_PACKS_WIDTH (2 * sizeof(uint32_t))

int nth_bitmapped_pack(struct repository *r, struct multi_pack_index *m,
		       struct bitmapped_pack *bp, uint32_t pack_int_id)
{
	if (!m->chunk_bitmapped_packs)
		return error(_("MIDX does not contain the BTMP chunk"));

	if (prepare_midx_pack(r, m, pack_int_id))
		return error(_("could not load bitmapped pack %"PRIu32), pack_int_id);

	bp->p = m->packs[pack_int_id];
	bp->bitmap_pos = get_be32((char *)m->chunk_bitmapped_packs +
				  MIDX_CHUNK_BITMAPPED_PACKS_WIDTH * pack_int_id);
	bp->bitmap_nr = get_be32((char *)m->chunk_bitmapped_packs +
				 MIDX_CHUNK_BITMAPPED_PACKS_WIDTH * pack_int_id +
				 sizeof(uint32_t));
	bp->pack_int_id = pack_int_id;

	return 0;
}

int bsearch_midx(const struct object_id *oid, struct multi_pack_index *m, uint32_t *result)
{
	return bsearch_hash(oid->hash, m->chunk_oid_fanout, m->chunk_oid_lookup,
			    the_hash_algo->rawsz, result);
}

struct object_id *nth_midxed_object_oid(struct object_id *oid,
					struct multi_pack_index *m,
					uint32_t n)
{
	if (n >= m->num_objects)
		return NULL;

	oidread(oid, m->chunk_oid_lookup + st_mult(m->hash_len, n),
		the_repository->hash_algo);
	return oid;
}

off_t nth_midxed_offset(struct multi_pack_index *m, uint32_t pos)
{
	const unsigned char *offset_data;
	uint32_t offset32;

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
	return get_be32(m->chunk_object_offsets +
			(off_t)pos * MIDX_CHUNK_OFFSET_WIDTH);
}

int fill_midx_entry(struct repository *r,
		    const struct object_id *oid,
		    struct pack_entry *e,
		    struct multi_pack_index *m)
{
	uint32_t pos;
	uint32_t pack_int_id;
	struct packed_git *p;

	if (!bsearch_midx(oid, m, &pos))
		return 0;

	if (pos >= m->num_objects)
		return 0;

	pack_int_id = nth_midxed_pack_int_id(m, pos);

	if (prepare_midx_pack(r, m, pack_int_id))
		return 0;
	p = m->packs[pack_int_id];

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

int midx_locate_pack(struct multi_pack_index *m, const char *idx_or_pack_name,
		     uint32_t *pos)
{
	uint32_t first = 0, last = m->num_packs;

	while (first < last) {
		uint32_t mid = first + (last - first) / 2;
		const char *current;
		int cmp;

		current = m->pack_names[mid];
		cmp = cmp_idx_or_pack_name(idx_or_pack_name, current);
		if (!cmp) {
			if (pos)
				*pos = mid;
			return 1;
		}
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
	return midx_locate_pack(m, idx_or_pack_name, NULL);
}

int midx_preferred_pack(struct multi_pack_index *m, uint32_t *pack_int_id)
{
	if (m->preferred_pack_idx == -1) {
		if (load_midx_revindex(m) < 0) {
			m->preferred_pack_idx = -2;
			return -1;
		}

		m->preferred_pack_idx =
			nth_midxed_pack_int_id(m, pack_pos_to_midx(m, 0));
	} else if (m->preferred_pack_idx == -2)
		return -1; /* no revindex */

	*pack_int_id = m->preferred_pack_idx;
	return 0;
}

int prepare_multi_pack_index_one(struct repository *r, const char *object_dir, int local)
{
	struct multi_pack_index *m;
	struct multi_pack_index *m_search;

	prepare_repo_settings(r);
	if (!r->settings.core_multi_pack_index)
		return 0;

	for (m_search = r->objects->multi_pack_index; m_search; m_search = m_search->next)
		if (!strcmp(object_dir, m_search->object_dir))
			return 1;

	m = load_multi_pack_index(object_dir, local);

	if (m) {
		struct multi_pack_index *mp = r->objects->multi_pack_index;
		if (mp) {
			m->next = mp->next;
			mp->next = m;
		} else
			r->objects->multi_pack_index = m;
		return 1;
	}

	return 0;
}

int midx_checksum_valid(struct multi_pack_index *m)
{
	return hashfile_checksum_valid(m->data, m->data_len);
}

struct clear_midx_data {
	char *keep;
	const char *ext;
};

static void clear_midx_file_ext(const char *full_path, size_t full_path_len UNUSED,
				const char *file_name, void *_data)
{
	struct clear_midx_data *data = _data;

	if (!(starts_with(file_name, "multi-pack-index-") &&
	      ends_with(file_name, data->ext)))
		return;
	if (data->keep && !strcmp(data->keep, file_name))
		return;

	if (unlink(full_path))
		die_errno(_("failed to remove %s"), full_path);
}

void clear_midx_files_ext(const char *object_dir, const char *ext,
			  unsigned char *keep_hash)
{
	struct clear_midx_data data;
	memset(&data, 0, sizeof(struct clear_midx_data));

	if (keep_hash)
		data.keep = xstrfmt("multi-pack-index-%s%s",
				    hash_to_hex(keep_hash), ext);
	data.ext = ext;

	for_each_file_in_pack_dir(object_dir,
				  clear_midx_file_ext,
				  &data);

	free(data.keep);
}

void clear_midx_file(struct repository *r)
{
	struct strbuf midx = STRBUF_INIT;

	get_midx_filename(&midx, r->objects->odb->path);

	if (r->objects && r->objects->multi_pack_index) {
		close_midx(r->objects->multi_pack_index);
		r->objects->multi_pack_index = NULL;
	}

	if (remove_path(midx.buf))
		die(_("failed to clear multi-pack-index at %s"), midx.buf);

	clear_midx_files_ext(r->objects->odb->path, ".bitmap", NULL);
	clear_midx_files_ext(r->objects->odb->path, ".rev", NULL);

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

int verify_midx_file(struct repository *r, const char *object_dir, unsigned flags)
{
	struct pair_pos_vs_id *pairs = NULL;
	uint32_t i;
	struct progress *progress = NULL;
	struct multi_pack_index *m = load_multi_pack_index(object_dir, 1);
	verify_midx_error = 0;

	if (!m) {
		int result = 0;
		struct stat sb;
		struct strbuf filename = STRBUF_INIT;

		get_midx_filename(&filename, object_dir);

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
		progress = start_delayed_progress(_("Looking for referenced packfiles"),
					  m->num_packs);
	for (i = 0; i < m->num_packs; i++) {
		if (prepare_midx_pack(r, m, i))
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
		progress = start_sparse_progress(_("Verifying OID order in multi-pack-index"),
						 m->num_objects - 1);
	for (i = 0; i < m->num_objects - 1; i++) {
		struct object_id oid1, oid2;

		nth_midxed_object_oid(&oid1, m, i);
		nth_midxed_object_oid(&oid2, m, i + 1);

		if (oidcmp(&oid1, &oid2) >= 0)
			midx_report(_("oid lookup out of order: oid[%d] = %s >= %s = oid[%d]"),
				    i, oid_to_hex(&oid1), oid_to_hex(&oid2), i + 1);

		midx_display_sparse_progress(progress, i + 1);
	}
	stop_progress(&progress);

	/*
	 * Create an array mapping each object to its packfile id.  Sort it
	 * to group the objects by packfile.  Use this permutation to visit
	 * each of the objects and only require 1 packfile to be open at a
	 * time.
	 */
	ALLOC_ARRAY(pairs, m->num_objects);
	for (i = 0; i < m->num_objects; i++) {
		pairs[i].pos = i;
		pairs[i].pack_int_id = nth_midxed_pack_int_id(m, i);
	}

	if (flags & MIDX_PROGRESS)
		progress = start_sparse_progress(_("Sorting objects by packfile"),
						 m->num_objects);
	display_progress(progress, 0); /* TODO: Measure QSORT() progress */
	QSORT(pairs, m->num_objects, compare_pair_pos_vs_id);
	stop_progress(&progress);

	if (flags & MIDX_PROGRESS)
		progress = start_sparse_progress(_("Verifying object offsets"), m->num_objects);
	for (i = 0; i < m->num_objects; i++) {
		struct object_id oid;
		struct pack_entry e;
		off_t m_offset, p_offset;

		if (i > 0 && pairs[i-1].pack_int_id != pairs[i].pack_int_id &&
		    m->packs[pairs[i-1].pack_int_id])
		{
			close_pack_fd(m->packs[pairs[i-1].pack_int_id]);
			close_pack_index(m->packs[pairs[i-1].pack_int_id]);
		}

		nth_midxed_object_oid(&oid, m, pairs[i].pos);

		if (!fill_midx_entry(r, &oid, &e, m)) {
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
		p_offset = find_pack_entry_one(oid.hash, e.p);

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
