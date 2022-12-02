#include "cache.h"
#include "config.h"
#include "csum-file.h"
#include "dir.h"
#include "lockfile.h"
#include "packfile.h"
#include "object-store.h"
#include "hash-lookup.h"
#include "midx.h"
#include "progress.h"
#include "trace2.h"
#include "run-command.h"
#include "repository.h"
#include "chunk-format.h"
#include "pack.h"
#include "pack-bitmap.h"
#include "refs.h"
#include "revision.h"
#include "list-objects.h"

#define MIDX_SIGNATURE 0x4d494458 /* "MIDX" */
#define MIDX_VERSION 1
#define MIDX_BYTE_FILE_VERSION 4
#define MIDX_BYTE_HASH_VERSION 5
#define MIDX_BYTE_NUM_CHUNKS 6
#define MIDX_BYTE_NUM_PACKS 8
#define MIDX_HEADER_SIZE 12
#define MIDX_MIN_SIZE (MIDX_HEADER_SIZE + the_hash_algo->rawsz)

#define MIDX_CHUNK_ALIGNMENT 4
#define MIDX_CHUNKID_PACKNAMES 0x504e414d /* "PNAM" */
#define MIDX_CHUNKID_OIDFANOUT 0x4f494446 /* "OIDF" */
#define MIDX_CHUNKID_OIDLOOKUP 0x4f49444c /* "OIDL" */
#define MIDX_CHUNKID_OBJECTOFFSETS 0x4f4f4646 /* "OOFF" */
#define MIDX_CHUNKID_LARGEOFFSETS 0x4c4f4646 /* "LOFF" */
#define MIDX_CHUNKID_REVINDEX 0x52494458 /* "RIDX" */
#define MIDX_CHUNK_FANOUT_SIZE (sizeof(uint32_t) * 256)
#define MIDX_CHUNK_OFFSET_WIDTH (2 * sizeof(uint32_t))
#define MIDX_CHUNK_LARGE_OFFSET_WIDTH (sizeof(uint64_t))
#define MIDX_LARGE_OFFSET_NEEDED 0x80000000

#define PACK_EXPIRED UINT_MAX

const unsigned char *get_midx_checksum(struct multi_pack_index *m)
{
	return m->data + m->data_len - the_hash_algo->rawsz;
}

void get_midx_filename(struct strbuf *out, const char *object_dir)
{
	strbuf_addf(out, "%s/pack/multi-pack-index", object_dir);
}

void get_midx_rev_filename(struct strbuf *out, struct multi_pack_index *m)
{
	get_midx_filename(out, m->object_dir);
	strbuf_addf(out, "-%s.rev", hash_to_hex(get_midx_checksum(m)));
}

static int midx_read_oid_fanout(const unsigned char *chunk_start,
				size_t chunk_size, void *data)
{
	struct multi_pack_index *m = data;
	m->chunk_oid_fanout = (uint32_t *)chunk_start;

	if (chunk_size != 4 * 256) {
		error(_("multi-pack-index OID fanout is of the wrong size"));
		return 1;
	}
	return 0;
}

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

	cf = init_chunkfile(NULL);

	if (read_table_of_contents(cf, m->data, midx_size,
				   MIDX_HEADER_SIZE, m->num_chunks))
		goto cleanup_fail;

	if (pair_chunk(cf, MIDX_CHUNKID_PACKNAMES, &m->chunk_pack_names) == CHUNK_NOT_FOUND)
		die(_("multi-pack-index missing required pack-name chunk"));
	if (read_chunk(cf, MIDX_CHUNKID_OIDFANOUT, midx_read_oid_fanout, m) == CHUNK_NOT_FOUND)
		die(_("multi-pack-index missing required OID fanout chunk"));
	if (pair_chunk(cf, MIDX_CHUNKID_OIDLOOKUP, &m->chunk_oid_lookup) == CHUNK_NOT_FOUND)
		die(_("multi-pack-index missing required OID lookup chunk"));
	if (pair_chunk(cf, MIDX_CHUNKID_OBJECTOFFSETS, &m->chunk_object_offsets) == CHUNK_NOT_FOUND)
		die(_("multi-pack-index missing required object offsets chunk"));

	pair_chunk(cf, MIDX_CHUNKID_LARGEOFFSETS, &m->chunk_large_offsets);

	if (git_env_bool("GIT_TEST_MIDX_READ_RIDX", 1))
		pair_chunk(cf, MIDX_CHUNKID_REVINDEX, &m->chunk_revindex);

	m->num_objects = ntohl(m->chunk_oid_fanout[255]);

	CALLOC_ARRAY(m->pack_names, m->num_packs);
	CALLOC_ARRAY(m->packs, m->num_packs);

	cur_pack_name = (const char *)m->chunk_pack_names;
	for (i = 0; i < m->num_packs; i++) {
		m->pack_names[i] = cur_pack_name;

		cur_pack_name += strlen(cur_pack_name) + 1;

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

	oidread(oid, m->chunk_oid_lookup + m->hash_len * n);
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
		return get_be64(m->chunk_large_offsets + sizeof(uint64_t) * offset32);
	}

	return offset32;
}

uint32_t nth_midxed_pack_int_id(struct multi_pack_index *m, uint32_t pos)
{
	return get_be32(m->chunk_object_offsets +
			(off_t)pos * MIDX_CHUNK_OFFSET_WIDTH);
}

int fill_midx_entry(struct repository * r,
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
static int cmp_idx_or_pack_name(const char *idx_or_pack_name,
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

int midx_contains_pack(struct multi_pack_index *m, const char *idx_or_pack_name)
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

static size_t write_midx_header(struct hashfile *f,
				unsigned char num_chunks,
				uint32_t num_packs)
{
	hashwrite_be32(f, MIDX_SIGNATURE);
	hashwrite_u8(f, MIDX_VERSION);
	hashwrite_u8(f, oid_version(the_hash_algo));
	hashwrite_u8(f, num_chunks);
	hashwrite_u8(f, 0); /* unused */
	hashwrite_be32(f, num_packs);

	return MIDX_HEADER_SIZE;
}

struct pack_info {
	uint32_t orig_pack_int_id;
	char *pack_name;
	struct packed_git *p;
	unsigned expired : 1;
};

static int pack_info_compare(const void *_a, const void *_b)
{
	struct pack_info *a = (struct pack_info *)_a;
	struct pack_info *b = (struct pack_info *)_b;
	return strcmp(a->pack_name, b->pack_name);
}

static int idx_or_pack_name_cmp(const void *_va, const void *_vb)
{
	const char *pack_name = _va;
	const struct pack_info *compar = _vb;

	return cmp_idx_or_pack_name(pack_name, compar->pack_name);
}

struct write_midx_context {
	struct pack_info *info;
	uint32_t nr;
	uint32_t alloc;
	struct multi_pack_index *m;
	struct progress *progress;
	unsigned pack_paths_checked;

	struct pack_midx_entry *entries;
	uint32_t entries_nr;

	uint32_t *pack_perm;
	uint32_t *pack_order;
	unsigned large_offsets_needed:1;
	uint32_t num_large_offsets;

	int preferred_pack_idx;

	struct string_list *to_include;
};

static void add_pack_to_midx(const char *full_path, size_t full_path_len,
			     const char *file_name, void *data)
{
	struct write_midx_context *ctx = data;

	if (ends_with(file_name, ".idx")) {
		display_progress(ctx->progress, ++ctx->pack_paths_checked);
		/*
		 * Note that at most one of ctx->m and ctx->to_include are set,
		 * so we are testing midx_contains_pack() and
		 * string_list_has_string() independently (guarded by the
		 * appropriate NULL checks).
		 *
		 * We could support passing to_include while reusing an existing
		 * MIDX, but don't currently since the reuse process drags
		 * forward all packs from an existing MIDX (without checking
		 * whether or not they appear in the to_include list).
		 *
		 * If we added support for that, these next two conditional
		 * should be performed independently (likely checking
		 * to_include before the existing MIDX).
		 */
		if (ctx->m && midx_contains_pack(ctx->m, file_name))
			return;
		else if (ctx->to_include &&
			 !string_list_has_string(ctx->to_include, file_name))
			return;

		ALLOC_GROW(ctx->info, ctx->nr + 1, ctx->alloc);

		ctx->info[ctx->nr].p = add_packed_git(full_path,
						      full_path_len,
						      0);

		if (!ctx->info[ctx->nr].p) {
			warning(_("failed to add packfile '%s'"),
				full_path);
			return;
		}

		if (open_pack_index(ctx->info[ctx->nr].p)) {
			warning(_("failed to open pack-index '%s'"),
				full_path);
			close_pack(ctx->info[ctx->nr].p);
			FREE_AND_NULL(ctx->info[ctx->nr].p);
			return;
		}

		ctx->info[ctx->nr].pack_name = xstrdup(file_name);
		ctx->info[ctx->nr].orig_pack_int_id = ctx->nr;
		ctx->info[ctx->nr].expired = 0;
		ctx->nr++;
	}
}

struct pack_midx_entry {
	struct object_id oid;
	uint32_t pack_int_id;
	time_t pack_mtime;
	uint64_t offset;
	unsigned preferred : 1;
};

static int midx_oid_compare(const void *_a, const void *_b)
{
	const struct pack_midx_entry *a = (const struct pack_midx_entry *)_a;
	const struct pack_midx_entry *b = (const struct pack_midx_entry *)_b;
	int cmp = oidcmp(&a->oid, &b->oid);

	if (cmp)
		return cmp;

	/* Sort objects in a preferred pack first when multiple copies exist. */
	if (a->preferred > b->preferred)
		return -1;
	if (a->preferred < b->preferred)
		return 1;

	if (a->pack_mtime > b->pack_mtime)
		return -1;
	else if (a->pack_mtime < b->pack_mtime)
		return 1;

	return a->pack_int_id - b->pack_int_id;
}

static int nth_midxed_pack_midx_entry(struct multi_pack_index *m,
				      struct pack_midx_entry *e,
				      uint32_t pos)
{
	if (pos >= m->num_objects)
		return 1;

	nth_midxed_object_oid(&e->oid, m, pos);
	e->pack_int_id = nth_midxed_pack_int_id(m, pos);
	e->offset = nth_midxed_offset(m, pos);

	/* consider objects in midx to be from "old" packs */
	e->pack_mtime = 0;
	return 0;
}

static void fill_pack_entry(uint32_t pack_int_id,
			    struct packed_git *p,
			    uint32_t cur_object,
			    struct pack_midx_entry *entry,
			    int preferred)
{
	if (nth_packed_object_id(&entry->oid, p, cur_object) < 0)
		die(_("failed to locate object %d in packfile"), cur_object);

	entry->pack_int_id = pack_int_id;
	entry->pack_mtime = p->mtime;

	entry->offset = nth_packed_object_offset(p, cur_object);
	entry->preferred = !!preferred;
}

struct midx_fanout {
	struct pack_midx_entry *entries;
	uint32_t nr;
	uint32_t alloc;
};

static void midx_fanout_grow(struct midx_fanout *fanout, uint32_t nr)
{
	ALLOC_GROW(fanout->entries, nr, fanout->alloc);
}

static void midx_fanout_sort(struct midx_fanout *fanout)
{
	QSORT(fanout->entries, fanout->nr, midx_oid_compare);
}

static void midx_fanout_add_midx_fanout(struct midx_fanout *fanout,
					struct multi_pack_index *m,
					uint32_t cur_fanout,
					int preferred_pack)
{
	uint32_t start = 0, end;
	uint32_t cur_object;

	if (cur_fanout)
		start = ntohl(m->chunk_oid_fanout[cur_fanout - 1]);
	end = ntohl(m->chunk_oid_fanout[cur_fanout]);

	for (cur_object = start; cur_object < end; cur_object++) {
		if ((preferred_pack > -1) &&
		    (preferred_pack == nth_midxed_pack_int_id(m, cur_object))) {
			/*
			 * Objects from preferred packs are added
			 * separately.
			 */
			continue;
		}

		midx_fanout_grow(fanout, fanout->nr + 1);
		nth_midxed_pack_midx_entry(m,
					   &fanout->entries[fanout->nr],
					   cur_object);
		fanout->entries[fanout->nr].preferred = 0;
		fanout->nr++;
	}
}

static void midx_fanout_add_pack_fanout(struct midx_fanout *fanout,
					struct pack_info *info,
					uint32_t cur_pack,
					int preferred,
					uint32_t cur_fanout)
{
	struct packed_git *pack = info[cur_pack].p;
	uint32_t start = 0, end;
	uint32_t cur_object;

	if (cur_fanout)
		start = get_pack_fanout(pack, cur_fanout - 1);
	end = get_pack_fanout(pack, cur_fanout);

	for (cur_object = start; cur_object < end; cur_object++) {
		midx_fanout_grow(fanout, fanout->nr + 1);
		fill_pack_entry(cur_pack,
				info[cur_pack].p,
				cur_object,
				&fanout->entries[fanout->nr],
				preferred);
		fanout->nr++;
	}
}

/*
 * It is possible to artificially get into a state where there are many
 * duplicate copies of objects. That can create high memory pressure if
 * we are to create a list of all objects before de-duplication. To reduce
 * this memory pressure without a significant performance drop, automatically
 * group objects by the first byte of their object id. Use the IDX fanout
 * tables to group the data, copy to a local array, then sort.
 *
 * Copy only the de-duplicated entries (selected by most-recent modified time
 * of a packfile containing the object).
 */
static struct pack_midx_entry *get_sorted_entries(struct multi_pack_index *m,
						  struct pack_info *info,
						  uint32_t nr_packs,
						  uint32_t *nr_objects,
						  int preferred_pack)
{
	uint32_t cur_fanout, cur_pack, cur_object;
	uint32_t alloc_objects, total_objects = 0;
	struct midx_fanout fanout = { 0 };
	struct pack_midx_entry *deduplicated_entries = NULL;
	uint32_t start_pack = m ? m->num_packs : 0;

	for (cur_pack = start_pack; cur_pack < nr_packs; cur_pack++)
		total_objects += info[cur_pack].p->num_objects;

	/*
	 * As we de-duplicate by fanout value, we expect the fanout
	 * slices to be evenly distributed, with some noise. Hence,
	 * allocate slightly more than one 256th.
	 */
	alloc_objects = fanout.alloc = total_objects > 3200 ? total_objects / 200 : 16;

	ALLOC_ARRAY(fanout.entries, fanout.alloc);
	ALLOC_ARRAY(deduplicated_entries, alloc_objects);
	*nr_objects = 0;

	for (cur_fanout = 0; cur_fanout < 256; cur_fanout++) {
		fanout.nr = 0;

		if (m)
			midx_fanout_add_midx_fanout(&fanout, m, cur_fanout,
						    preferred_pack);

		for (cur_pack = start_pack; cur_pack < nr_packs; cur_pack++) {
			int preferred = cur_pack == preferred_pack;
			midx_fanout_add_pack_fanout(&fanout,
						    info, cur_pack,
						    preferred, cur_fanout);
		}

		if (-1 < preferred_pack && preferred_pack < start_pack)
			midx_fanout_add_pack_fanout(&fanout, info,
						    preferred_pack, 1,
						    cur_fanout);

		midx_fanout_sort(&fanout);

		/*
		 * The batch is now sorted by OID and then mtime (descending).
		 * Take only the first duplicate.
		 */
		for (cur_object = 0; cur_object < fanout.nr; cur_object++) {
			if (cur_object && oideq(&fanout.entries[cur_object - 1].oid,
						&fanout.entries[cur_object].oid))
				continue;

			ALLOC_GROW(deduplicated_entries, *nr_objects + 1, alloc_objects);
			memcpy(&deduplicated_entries[*nr_objects],
			       &fanout.entries[cur_object],
			       sizeof(struct pack_midx_entry));
			(*nr_objects)++;
		}
	}

	free(fanout.entries);
	return deduplicated_entries;
}

static int write_midx_pack_names(struct hashfile *f, void *data)
{
	struct write_midx_context *ctx = data;
	uint32_t i;
	unsigned char padding[MIDX_CHUNK_ALIGNMENT];
	size_t written = 0;

	for (i = 0; i < ctx->nr; i++) {
		size_t writelen;

		if (ctx->info[i].expired)
			continue;

		if (i && strcmp(ctx->info[i].pack_name, ctx->info[i - 1].pack_name) <= 0)
			BUG("incorrect pack-file order: %s before %s",
			    ctx->info[i - 1].pack_name,
			    ctx->info[i].pack_name);

		writelen = strlen(ctx->info[i].pack_name) + 1;
		hashwrite(f, ctx->info[i].pack_name, writelen);
		written += writelen;
	}

	/* add padding to be aligned */
	i = MIDX_CHUNK_ALIGNMENT - (written % MIDX_CHUNK_ALIGNMENT);
	if (i < MIDX_CHUNK_ALIGNMENT) {
		memset(padding, 0, sizeof(padding));
		hashwrite(f, padding, i);
	}

	return 0;
}

static int write_midx_oid_fanout(struct hashfile *f,
				 void *data)
{
	struct write_midx_context *ctx = data;
	struct pack_midx_entry *list = ctx->entries;
	struct pack_midx_entry *last = ctx->entries + ctx->entries_nr;
	uint32_t count = 0;
	uint32_t i;

	/*
	* Write the first-level table (the list is sorted,
	* but we use a 256-entry lookup to be able to avoid
	* having to do eight extra binary search iterations).
	*/
	for (i = 0; i < 256; i++) {
		struct pack_midx_entry *next = list;

		while (next < last && next->oid.hash[0] == i) {
			count++;
			next++;
		}

		hashwrite_be32(f, count);
		list = next;
	}

	return 0;
}

static int write_midx_oid_lookup(struct hashfile *f,
				 void *data)
{
	struct write_midx_context *ctx = data;
	unsigned char hash_len = the_hash_algo->rawsz;
	struct pack_midx_entry *list = ctx->entries;
	uint32_t i;

	for (i = 0; i < ctx->entries_nr; i++) {
		struct pack_midx_entry *obj = list++;

		if (i < ctx->entries_nr - 1) {
			struct pack_midx_entry *next = list;
			if (oidcmp(&obj->oid, &next->oid) >= 0)
				BUG("OIDs not in order: %s >= %s",
				    oid_to_hex(&obj->oid),
				    oid_to_hex(&next->oid));
		}

		hashwrite(f, obj->oid.hash, (int)hash_len);
	}

	return 0;
}

static int write_midx_object_offsets(struct hashfile *f,
				     void *data)
{
	struct write_midx_context *ctx = data;
	struct pack_midx_entry *list = ctx->entries;
	uint32_t i, nr_large_offset = 0;

	for (i = 0; i < ctx->entries_nr; i++) {
		struct pack_midx_entry *obj = list++;

		if (ctx->pack_perm[obj->pack_int_id] == PACK_EXPIRED)
			BUG("object %s is in an expired pack with int-id %d",
			    oid_to_hex(&obj->oid),
			    obj->pack_int_id);

		hashwrite_be32(f, ctx->pack_perm[obj->pack_int_id]);

		if (ctx->large_offsets_needed && obj->offset >> 31)
			hashwrite_be32(f, MIDX_LARGE_OFFSET_NEEDED | nr_large_offset++);
		else if (!ctx->large_offsets_needed && obj->offset >> 32)
			BUG("object %s requires a large offset (%"PRIx64") but the MIDX is not writing large offsets!",
			    oid_to_hex(&obj->oid),
			    obj->offset);
		else
			hashwrite_be32(f, (uint32_t)obj->offset);
	}

	return 0;
}

static int write_midx_large_offsets(struct hashfile *f,
				    void *data)
{
	struct write_midx_context *ctx = data;
	struct pack_midx_entry *list = ctx->entries;
	struct pack_midx_entry *end = ctx->entries + ctx->entries_nr;
	uint32_t nr_large_offset = ctx->num_large_offsets;

	while (nr_large_offset) {
		struct pack_midx_entry *obj;
		uint64_t offset;

		if (list >= end)
			BUG("too many large-offset objects");

		obj = list++;
		offset = obj->offset;

		if (!(offset >> 31))
			continue;

		hashwrite_be64(f, offset);

		nr_large_offset--;
	}

	return 0;
}

static int write_midx_revindex(struct hashfile *f,
			       void *data)
{
	struct write_midx_context *ctx = data;
	uint32_t i;

	for (i = 0; i < ctx->entries_nr; i++)
		hashwrite_be32(f, ctx->pack_order[i]);

	return 0;
}

struct midx_pack_order_data {
	uint32_t nr;
	uint32_t pack;
	off_t offset;
};

static int midx_pack_order_cmp(const void *va, const void *vb)
{
	const struct midx_pack_order_data *a = va, *b = vb;
	if (a->pack < b->pack)
		return -1;
	else if (a->pack > b->pack)
		return 1;
	else if (a->offset < b->offset)
		return -1;
	else if (a->offset > b->offset)
		return 1;
	else
		return 0;
}

static uint32_t *midx_pack_order(struct write_midx_context *ctx)
{
	struct midx_pack_order_data *data;
	uint32_t *pack_order;
	uint32_t i;

	ALLOC_ARRAY(data, ctx->entries_nr);
	for (i = 0; i < ctx->entries_nr; i++) {
		struct pack_midx_entry *e = &ctx->entries[i];
		data[i].nr = i;
		data[i].pack = ctx->pack_perm[e->pack_int_id];
		if (!e->preferred)
			data[i].pack |= (1U << 31);
		data[i].offset = e->offset;
	}

	QSORT(data, ctx->entries_nr, midx_pack_order_cmp);

	ALLOC_ARRAY(pack_order, ctx->entries_nr);
	for (i = 0; i < ctx->entries_nr; i++)
		pack_order[i] = data[i].nr;
	free(data);

	return pack_order;
}

static void write_midx_reverse_index(char *midx_name, unsigned char *midx_hash,
				     struct write_midx_context *ctx)
{
	struct strbuf buf = STRBUF_INIT;
	const char *tmp_file;

	strbuf_addf(&buf, "%s-%s.rev", midx_name, hash_to_hex(midx_hash));

	tmp_file = write_rev_file_order(NULL, ctx->pack_order, ctx->entries_nr,
					midx_hash, WRITE_REV);

	if (finalize_object_file(tmp_file, buf.buf))
		die(_("cannot store reverse index file"));

	strbuf_release(&buf);
}

static void clear_midx_files_ext(const char *object_dir, const char *ext,
				 unsigned char *keep_hash);

static int midx_checksum_valid(struct multi_pack_index *m)
{
	return hashfile_checksum_valid(m->data, m->data_len);
}

static void prepare_midx_packing_data(struct packing_data *pdata,
				      struct write_midx_context *ctx)
{
	uint32_t i;

	memset(pdata, 0, sizeof(struct packing_data));
	prepare_packing_data(the_repository, pdata);

	for (i = 0; i < ctx->entries_nr; i++) {
		struct pack_midx_entry *from = &ctx->entries[ctx->pack_order[i]];
		struct object_entry *to = packlist_alloc(pdata, &from->oid);

		oe_set_in_pack(pdata, to,
			       ctx->info[ctx->pack_perm[from->pack_int_id]].p);
	}
}

static int add_ref_to_pending(const char *refname,
			      const struct object_id *oid,
			      int flag, void *cb_data)
{
	struct rev_info *revs = (struct rev_info*)cb_data;
	struct object *object;

	if ((flag & REF_ISSYMREF) && (flag & REF_ISBROKEN)) {
		warning("symbolic ref is dangling: %s", refname);
		return 0;
	}

	object = parse_object_or_die(oid, refname);
	if (object->type != OBJ_COMMIT)
		return 0;

	add_pending_object(revs, object, "");
	if (bitmap_is_preferred_refname(revs->repo, refname))
		object->flags |= NEEDS_BITMAP;
	return 0;
}

struct bitmap_commit_cb {
	struct commit **commits;
	size_t commits_nr, commits_alloc;

	struct write_midx_context *ctx;
};

static const struct object_id *bitmap_oid_access(size_t index,
						 const void *_entries)
{
	const struct pack_midx_entry *entries = _entries;
	return &entries[index].oid;
}

static void bitmap_show_commit(struct commit *commit, void *_data)
{
	struct bitmap_commit_cb *data = _data;
	int pos = oid_pos(&commit->object.oid, data->ctx->entries,
			  data->ctx->entries_nr,
			  bitmap_oid_access);
	if (pos < 0)
		return;

	ALLOC_GROW(data->commits, data->commits_nr + 1, data->commits_alloc);
	data->commits[data->commits_nr++] = commit;
}

static int read_refs_snapshot(const char *refs_snapshot,
			      struct rev_info *revs)
{
	struct strbuf buf = STRBUF_INIT;
	struct object_id oid;
	FILE *f = xfopen(refs_snapshot, "r");

	while (strbuf_getline(&buf, f) != EOF) {
		struct object *object;
		int preferred = 0;
		char *hex = buf.buf;
		const char *end = NULL;

		if (buf.len && *buf.buf == '+') {
			preferred = 1;
			hex = &buf.buf[1];
		}

		if (parse_oid_hex(hex, &oid, &end) < 0)
			die(_("could not parse line: %s"), buf.buf);
		if (*end)
			die(_("malformed line: %s"), buf.buf);

		object = parse_object_or_die(&oid, NULL);
		if (preferred)
			object->flags |= NEEDS_BITMAP;

		add_pending_object(revs, object, "");
	}

	fclose(f);
	strbuf_release(&buf);
	return 0;
}

static struct commit **find_commits_for_midx_bitmap(uint32_t *indexed_commits_nr_p,
						    const char *refs_snapshot,
						    struct write_midx_context *ctx)
{
	struct rev_info revs;
	struct bitmap_commit_cb cb = {0};

	cb.ctx = ctx;

	repo_init_revisions(the_repository, &revs, NULL);
	if (refs_snapshot) {
		read_refs_snapshot(refs_snapshot, &revs);
	} else {
		setup_revisions(0, NULL, &revs, NULL);
		for_each_ref(add_ref_to_pending, &revs);
	}

	/*
	 * Skipping promisor objects here is intentional, since it only excludes
	 * them from the list of reachable commits that we want to select from
	 * when computing the selection of MIDX'd commits to receive bitmaps.
	 *
	 * Reachability bitmaps do require that their objects be closed under
	 * reachability, but fetching any objects missing from promisors at this
	 * point is too late. But, if one of those objects can be reached from
	 * an another object that is included in the bitmap, then we will
	 * complain later that we don't have reachability closure (and fail
	 * appropriately).
	 */
	fetch_if_missing = 0;
	revs.exclude_promisor_objects = 1;

	if (prepare_revision_walk(&revs))
		die(_("revision walk setup failed"));

	traverse_commit_list(&revs, bitmap_show_commit, NULL, &cb);
	if (indexed_commits_nr_p)
		*indexed_commits_nr_p = cb.commits_nr;

	release_revisions(&revs);
	return cb.commits;
}

static int write_midx_bitmap(const char *midx_name,
			     const unsigned char *midx_hash,
			     struct packing_data *pdata,
			     struct commit **commits,
			     uint32_t commits_nr,
			     uint32_t *pack_order,
			     unsigned flags)
{
	int ret, i;
	uint16_t options = 0;
	struct pack_idx_entry **index;
	char *bitmap_name = xstrfmt("%s-%s.bitmap", midx_name,
					hash_to_hex(midx_hash));

	if (flags & MIDX_WRITE_BITMAP_HASH_CACHE)
		options |= BITMAP_OPT_HASH_CACHE;

	if (flags & MIDX_WRITE_BITMAP_LOOKUP_TABLE)
		options |= BITMAP_OPT_LOOKUP_TABLE;

	/*
	 * Build the MIDX-order index based on pdata.objects (which is already
	 * in MIDX order; c.f., 'midx_pack_order_cmp()' for the definition of
	 * this order).
	 */
	ALLOC_ARRAY(index, pdata->nr_objects);
	for (i = 0; i < pdata->nr_objects; i++)
		index[i] = &pdata->objects[i].idx;

	bitmap_writer_show_progress(flags & MIDX_PROGRESS);
	bitmap_writer_build_type_index(pdata, index, pdata->nr_objects);

	/*
	 * bitmap_writer_finish expects objects in lex order, but pack_order
	 * gives us exactly that. use it directly instead of re-sorting the
	 * array.
	 *
	 * This changes the order of objects in 'index' between
	 * bitmap_writer_build_type_index and bitmap_writer_finish.
	 *
	 * The same re-ordering takes place in the single-pack bitmap code via
	 * write_idx_file(), which is called by finish_tmp_packfile(), which
	 * happens between bitmap_writer_build_type_index() and
	 * bitmap_writer_finish().
	 */
	for (i = 0; i < pdata->nr_objects; i++)
		index[pack_order[i]] = &pdata->objects[i].idx;

	bitmap_writer_select_commits(commits, commits_nr, -1);
	ret = bitmap_writer_build(pdata);
	if (ret < 0)
		goto cleanup;

	bitmap_writer_set_checksum(midx_hash);
	bitmap_writer_finish(index, pdata->nr_objects, bitmap_name, options);

cleanup:
	free(index);
	free(bitmap_name);
	return ret;
}

static struct multi_pack_index *lookup_multi_pack_index(struct repository *r,
							const char *object_dir)
{
	struct multi_pack_index *result = NULL;
	struct multi_pack_index *cur;
	char *obj_dir_real = real_pathdup(object_dir, 1);
	struct strbuf cur_path_real = STRBUF_INIT;

	/* Ensure the given object_dir is local, or a known alternate. */
	find_odb(r, obj_dir_real);

	for (cur = get_multi_pack_index(r); cur; cur = cur->next) {
		strbuf_realpath(&cur_path_real, cur->object_dir, 1);
		if (!strcmp(obj_dir_real, cur_path_real.buf)) {
			result = cur;
			goto cleanup;
		}
	}

cleanup:
	free(obj_dir_real);
	strbuf_release(&cur_path_real);
	return result;
}

static int write_midx_internal(const char *object_dir,
			       struct string_list *packs_to_include,
			       struct string_list *packs_to_drop,
			       const char *preferred_pack_name,
			       const char *refs_snapshot,
			       unsigned flags)
{
	struct strbuf midx_name = STRBUF_INIT;
	unsigned char midx_hash[GIT_MAX_RAWSZ];
	uint32_t i;
	struct hashfile *f = NULL;
	struct lock_file lk;
	struct write_midx_context ctx = { 0 };
	int pack_name_concat_len = 0;
	int dropped_packs = 0;
	int result = 0;
	struct chunkfile *cf;

	get_midx_filename(&midx_name, object_dir);
	if (safe_create_leading_directories(midx_name.buf))
		die_errno(_("unable to create leading directories of %s"),
			  midx_name.buf);

	if (!packs_to_include) {
		/*
		 * Only reference an existing MIDX when not filtering which
		 * packs to include, since all packs and objects are copied
		 * blindly from an existing MIDX if one is present.
		 */
		ctx.m = lookup_multi_pack_index(the_repository, object_dir);
	}

	if (ctx.m && !midx_checksum_valid(ctx.m)) {
		warning(_("ignoring existing multi-pack-index; checksum mismatch"));
		ctx.m = NULL;
	}

	ctx.nr = 0;
	ctx.alloc = ctx.m ? ctx.m->num_packs : 16;
	ctx.info = NULL;
	ALLOC_ARRAY(ctx.info, ctx.alloc);

	if (ctx.m) {
		for (i = 0; i < ctx.m->num_packs; i++) {
			ALLOC_GROW(ctx.info, ctx.nr + 1, ctx.alloc);

			ctx.info[ctx.nr].orig_pack_int_id = i;
			ctx.info[ctx.nr].pack_name = xstrdup(ctx.m->pack_names[i]);
			ctx.info[ctx.nr].p = ctx.m->packs[i];
			ctx.info[ctx.nr].expired = 0;

			if (flags & MIDX_WRITE_REV_INDEX) {
				/*
				 * If generating a reverse index, need to have
				 * packed_git's loaded to compare their
				 * mtimes and object count.
				 */
				if (prepare_midx_pack(the_repository, ctx.m, i)) {
					error(_("could not load pack"));
					result = 1;
					goto cleanup;
				}

				if (open_pack_index(ctx.m->packs[i]))
					die(_("could not open index for %s"),
					    ctx.m->packs[i]->pack_name);
				ctx.info[ctx.nr].p = ctx.m->packs[i];
			}

			ctx.nr++;
		}
	}

	ctx.pack_paths_checked = 0;
	if (flags & MIDX_PROGRESS)
		ctx.progress = start_delayed_progress(_("Adding packfiles to multi-pack-index"), 0);
	else
		ctx.progress = NULL;

	ctx.to_include = packs_to_include;

	for_each_file_in_pack_dir(object_dir, add_pack_to_midx, &ctx);
	stop_progress(&ctx.progress);

	if ((ctx.m && ctx.nr == ctx.m->num_packs) &&
	    !(packs_to_include || packs_to_drop)) {
		struct bitmap_index *bitmap_git;
		int bitmap_exists;
		int want_bitmap = flags & MIDX_WRITE_BITMAP;

		bitmap_git = prepare_midx_bitmap_git(ctx.m);
		bitmap_exists = bitmap_git && bitmap_is_midx(bitmap_git);
		free_bitmap_index(bitmap_git);

		if (bitmap_exists || !want_bitmap) {
			/*
			 * The correct MIDX already exists, and so does a
			 * corresponding bitmap (or one wasn't requested).
			 */
			if (!want_bitmap)
				clear_midx_files_ext(object_dir, ".bitmap",
						     NULL);
			goto cleanup;
		}
	}

	if (preferred_pack_name) {
		int found = 0;
		for (i = 0; i < ctx.nr; i++) {
			if (!cmp_idx_or_pack_name(preferred_pack_name,
						  ctx.info[i].pack_name)) {
				ctx.preferred_pack_idx = i;
				found = 1;
				break;
			}
		}

		if (!found)
			warning(_("unknown preferred pack: '%s'"),
				preferred_pack_name);
	} else if (ctx.nr &&
		   (flags & (MIDX_WRITE_REV_INDEX | MIDX_WRITE_BITMAP))) {
		struct packed_git *oldest = ctx.info[ctx.preferred_pack_idx].p;
		ctx.preferred_pack_idx = 0;

		if (packs_to_drop && packs_to_drop->nr)
			BUG("cannot write a MIDX bitmap during expiration");

		/*
		 * set a preferred pack when writing a bitmap to ensure that
		 * the pack from which the first object is selected in pseudo
		 * pack-order has all of its objects selected from that pack
		 * (and not another pack containing a duplicate)
		 */
		for (i = 1; i < ctx.nr; i++) {
			struct packed_git *p = ctx.info[i].p;

			if (!oldest->num_objects || p->mtime < oldest->mtime) {
				oldest = p;
				ctx.preferred_pack_idx = i;
			}
		}

		if (!oldest->num_objects) {
			/*
			 * If all packs are empty; unset the preferred index.
			 * This is acceptable since there will be no duplicate
			 * objects to resolve, so the preferred value doesn't
			 * matter.
			 */
			ctx.preferred_pack_idx = -1;
		}
	} else {
		/*
		 * otherwise don't mark any pack as preferred to avoid
		 * interfering with expiration logic below
		 */
		ctx.preferred_pack_idx = -1;
	}

	if (ctx.preferred_pack_idx > -1) {
		struct packed_git *preferred = ctx.info[ctx.preferred_pack_idx].p;
		if (!preferred->num_objects) {
			error(_("cannot select preferred pack %s with no objects"),
			      preferred->pack_name);
			result = 1;
			goto cleanup;
		}
	}

	ctx.entries = get_sorted_entries(ctx.m, ctx.info, ctx.nr, &ctx.entries_nr,
					 ctx.preferred_pack_idx);

	ctx.large_offsets_needed = 0;
	for (i = 0; i < ctx.entries_nr; i++) {
		if (ctx.entries[i].offset > 0x7fffffff)
			ctx.num_large_offsets++;
		if (ctx.entries[i].offset > 0xffffffff)
			ctx.large_offsets_needed = 1;
	}

	QSORT(ctx.info, ctx.nr, pack_info_compare);

	if (packs_to_drop && packs_to_drop->nr) {
		int drop_index = 0;
		int missing_drops = 0;

		for (i = 0; i < ctx.nr && drop_index < packs_to_drop->nr; i++) {
			int cmp = strcmp(ctx.info[i].pack_name,
					 packs_to_drop->items[drop_index].string);

			if (!cmp) {
				drop_index++;
				ctx.info[i].expired = 1;
			} else if (cmp > 0) {
				error(_("did not see pack-file %s to drop"),
				      packs_to_drop->items[drop_index].string);
				drop_index++;
				missing_drops++;
				i--;
			} else {
				ctx.info[i].expired = 0;
			}
		}

		if (missing_drops) {
			result = 1;
			goto cleanup;
		}
	}

	/*
	 * pack_perm stores a permutation between pack-int-ids from the
	 * previous multi-pack-index to the new one we are writing:
	 *
	 * pack_perm[old_id] = new_id
	 */
	ALLOC_ARRAY(ctx.pack_perm, ctx.nr);
	for (i = 0; i < ctx.nr; i++) {
		if (ctx.info[i].expired) {
			dropped_packs++;
			ctx.pack_perm[ctx.info[i].orig_pack_int_id] = PACK_EXPIRED;
		} else {
			ctx.pack_perm[ctx.info[i].orig_pack_int_id] = i - dropped_packs;
		}
	}

	for (i = 0; i < ctx.nr; i++) {
		if (!ctx.info[i].expired)
			pack_name_concat_len += strlen(ctx.info[i].pack_name) + 1;
	}

	/* Check that the preferred pack wasn't expired (if given). */
	if (preferred_pack_name) {
		struct pack_info *preferred = bsearch(preferred_pack_name,
						      ctx.info, ctx.nr,
						      sizeof(*ctx.info),
						      idx_or_pack_name_cmp);
		if (preferred) {
			uint32_t perm = ctx.pack_perm[preferred->orig_pack_int_id];
			if (perm == PACK_EXPIRED)
				warning(_("preferred pack '%s' is expired"),
					preferred_pack_name);
		}
	}

	if (pack_name_concat_len % MIDX_CHUNK_ALIGNMENT)
		pack_name_concat_len += MIDX_CHUNK_ALIGNMENT -
					(pack_name_concat_len % MIDX_CHUNK_ALIGNMENT);

	hold_lock_file_for_update(&lk, midx_name.buf, LOCK_DIE_ON_ERROR);
	f = hashfd(get_lock_file_fd(&lk), get_lock_file_path(&lk));

	if (ctx.nr - dropped_packs == 0) {
		error(_("no pack files to index."));
		result = 1;
		goto cleanup;
	}

	if (!ctx.entries_nr) {
		if (flags & MIDX_WRITE_BITMAP)
			warning(_("refusing to write multi-pack .bitmap without any objects"));
		flags &= ~(MIDX_WRITE_REV_INDEX | MIDX_WRITE_BITMAP);
	}

	cf = init_chunkfile(f);

	add_chunk(cf, MIDX_CHUNKID_PACKNAMES, pack_name_concat_len,
		  write_midx_pack_names);
	add_chunk(cf, MIDX_CHUNKID_OIDFANOUT, MIDX_CHUNK_FANOUT_SIZE,
		  write_midx_oid_fanout);
	add_chunk(cf, MIDX_CHUNKID_OIDLOOKUP,
		  (size_t)ctx.entries_nr * the_hash_algo->rawsz,
		  write_midx_oid_lookup);
	add_chunk(cf, MIDX_CHUNKID_OBJECTOFFSETS,
		  (size_t)ctx.entries_nr * MIDX_CHUNK_OFFSET_WIDTH,
		  write_midx_object_offsets);

	if (ctx.large_offsets_needed)
		add_chunk(cf, MIDX_CHUNKID_LARGEOFFSETS,
			(size_t)ctx.num_large_offsets * MIDX_CHUNK_LARGE_OFFSET_WIDTH,
			write_midx_large_offsets);

	if (flags & (MIDX_WRITE_REV_INDEX | MIDX_WRITE_BITMAP)) {
		ctx.pack_order = midx_pack_order(&ctx);
		add_chunk(cf, MIDX_CHUNKID_REVINDEX,
			  ctx.entries_nr * sizeof(uint32_t),
			  write_midx_revindex);
	}

	write_midx_header(f, get_num_chunks(cf), ctx.nr - dropped_packs);
	write_chunkfile(cf, &ctx);

	finalize_hashfile(f, midx_hash, FSYNC_COMPONENT_PACK_METADATA,
			  CSUM_FSYNC | CSUM_HASH_IN_STREAM);
	free_chunkfile(cf);

	if (flags & MIDX_WRITE_REV_INDEX &&
	    git_env_bool("GIT_TEST_MIDX_WRITE_REV", 0))
		write_midx_reverse_index(midx_name.buf, midx_hash, &ctx);

	if (flags & MIDX_WRITE_BITMAP) {
		struct packing_data pdata;
		struct commit **commits;
		uint32_t commits_nr;

		if (!ctx.entries_nr)
			BUG("cannot write a bitmap without any objects");

		prepare_midx_packing_data(&pdata, &ctx);

		commits = find_commits_for_midx_bitmap(&commits_nr, refs_snapshot, &ctx);

		/*
		 * The previous steps translated the information from
		 * 'entries' into information suitable for constructing
		 * bitmaps. We no longer need that array, so clear it to
		 * reduce memory pressure.
		 */
		FREE_AND_NULL(ctx.entries);
		ctx.entries_nr = 0;

		if (write_midx_bitmap(midx_name.buf, midx_hash, &pdata,
				      commits, commits_nr, ctx.pack_order,
				      flags) < 0) {
			error(_("could not write multi-pack bitmap"));
			result = 1;
			goto cleanup;
		}
	}
	/*
	 * NOTE: Do not use ctx.entries beyond this point, since it might
	 * have been freed in the previous if block.
	 */

	if (ctx.m)
		close_object_store(the_repository->objects);

	if (commit_lock_file(&lk) < 0)
		die_errno(_("could not write multi-pack-index"));

	clear_midx_files_ext(object_dir, ".bitmap", midx_hash);
	clear_midx_files_ext(object_dir, ".rev", midx_hash);

cleanup:
	for (i = 0; i < ctx.nr; i++) {
		if (ctx.info[i].p) {
			close_pack(ctx.info[i].p);
			free(ctx.info[i].p);
		}
		free(ctx.info[i].pack_name);
	}

	free(ctx.info);
	free(ctx.entries);
	free(ctx.pack_perm);
	free(ctx.pack_order);
	strbuf_release(&midx_name);

	return result;
}

int write_midx_file(const char *object_dir,
		    const char *preferred_pack_name,
		    const char *refs_snapshot,
		    unsigned flags)
{
	return write_midx_internal(object_dir, NULL, NULL, preferred_pack_name,
				   refs_snapshot, flags);
}

int write_midx_file_only(const char *object_dir,
			 struct string_list *packs_to_include,
			 const char *preferred_pack_name,
			 const char *refs_snapshot,
			 unsigned flags)
{
	return write_midx_internal(object_dir, packs_to_include, NULL,
				   preferred_pack_name, refs_snapshot, flags);
}

struct clear_midx_data {
	char *keep;
	const char *ext;
};

static void clear_midx_file_ext(const char *full_path, size_t full_path_len,
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

static void clear_midx_files_ext(const char *object_dir, const char *ext,
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

	for (i = 0; i < 255; i++) {
		uint32_t oid_fanout1 = ntohl(m->chunk_oid_fanout[i]);
		uint32_t oid_fanout2 = ntohl(m->chunk_oid_fanout[i + 1]);

		if (oid_fanout1 > oid_fanout2)
			midx_report(_("oid fanout out of order: fanout[%d] = %"PRIx32" > %"PRIx32" = fanout[%d]"),
				    i, oid_fanout1, oid_fanout2, i + 1);
	}

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

int expire_midx_packs(struct repository *r, const char *object_dir, unsigned flags)
{
	uint32_t i, *count, result = 0;
	struct string_list packs_to_drop = STRING_LIST_INIT_DUP;
	struct multi_pack_index *m = lookup_multi_pack_index(r, object_dir);
	struct progress *progress = NULL;

	if (!m)
		return 0;

	CALLOC_ARRAY(count, m->num_packs);

	if (flags & MIDX_PROGRESS)
		progress = start_delayed_progress(_("Counting referenced objects"),
					  m->num_objects);
	for (i = 0; i < m->num_objects; i++) {
		int pack_int_id = nth_midxed_pack_int_id(m, i);
		count[pack_int_id]++;
		display_progress(progress, i + 1);
	}
	stop_progress(&progress);

	if (flags & MIDX_PROGRESS)
		progress = start_delayed_progress(_("Finding and deleting unreferenced packfiles"),
					  m->num_packs);
	for (i = 0; i < m->num_packs; i++) {
		char *pack_name;
		display_progress(progress, i + 1);

		if (count[i])
			continue;

		if (prepare_midx_pack(r, m, i))
			continue;

		if (m->packs[i]->pack_keep || m->packs[i]->is_cruft)
			continue;

		pack_name = xstrdup(m->packs[i]->pack_name);
		close_pack(m->packs[i]);

		string_list_insert(&packs_to_drop, m->pack_names[i]);
		unlink_pack_path(pack_name, 0);
		free(pack_name);
	}
	stop_progress(&progress);

	free(count);

	if (packs_to_drop.nr)
		result = write_midx_internal(object_dir, NULL, &packs_to_drop, NULL, NULL, flags);

	string_list_clear(&packs_to_drop, 0);

	return result;
}

struct repack_info {
	timestamp_t mtime;
	uint32_t referenced_objects;
	uint32_t pack_int_id;
};

static int compare_by_mtime(const void *a_, const void *b_)
{
	const struct repack_info *a, *b;

	a = (const struct repack_info *)a_;
	b = (const struct repack_info *)b_;

	if (a->mtime < b->mtime)
		return -1;
	if (a->mtime > b->mtime)
		return 1;
	return 0;
}

static int fill_included_packs_all(struct repository *r,
				   struct multi_pack_index *m,
				   unsigned char *include_pack)
{
	uint32_t i, count = 0;
	int pack_kept_objects = 0;

	repo_config_get_bool(r, "repack.packkeptobjects", &pack_kept_objects);

	for (i = 0; i < m->num_packs; i++) {
		if (prepare_midx_pack(r, m, i))
			continue;
		if (!pack_kept_objects && m->packs[i]->pack_keep)
			continue;
		if (m->packs[i]->is_cruft)
			continue;

		include_pack[i] = 1;
		count++;
	}

	return count < 2;
}

static int fill_included_packs_batch(struct repository *r,
				     struct multi_pack_index *m,
				     unsigned char *include_pack,
				     size_t batch_size)
{
	uint32_t i, packs_to_repack;
	size_t total_size;
	struct repack_info *pack_info;
	int pack_kept_objects = 0;

	CALLOC_ARRAY(pack_info, m->num_packs);

	repo_config_get_bool(r, "repack.packkeptobjects", &pack_kept_objects);

	for (i = 0; i < m->num_packs; i++) {
		pack_info[i].pack_int_id = i;

		if (prepare_midx_pack(r, m, i))
			continue;

		pack_info[i].mtime = m->packs[i]->mtime;
	}

	for (i = 0; i < m->num_objects; i++) {
		uint32_t pack_int_id = nth_midxed_pack_int_id(m, i);
		pack_info[pack_int_id].referenced_objects++;
	}

	QSORT(pack_info, m->num_packs, compare_by_mtime);

	total_size = 0;
	packs_to_repack = 0;
	for (i = 0; total_size < batch_size && i < m->num_packs; i++) {
		int pack_int_id = pack_info[i].pack_int_id;
		struct packed_git *p = m->packs[pack_int_id];
		size_t expected_size;

		if (!p)
			continue;
		if (!pack_kept_objects && p->pack_keep)
			continue;
		if (p->is_cruft)
			continue;
		if (open_pack_index(p) || !p->num_objects)
			continue;

		expected_size = (size_t)(p->pack_size
					 * pack_info[i].referenced_objects);
		expected_size /= p->num_objects;

		if (expected_size >= batch_size)
			continue;

		packs_to_repack++;
		total_size += expected_size;
		include_pack[pack_int_id] = 1;
	}

	free(pack_info);

	if (packs_to_repack < 2)
		return 1;

	return 0;
}

int midx_repack(struct repository *r, const char *object_dir, size_t batch_size, unsigned flags)
{
	int result = 0;
	uint32_t i;
	unsigned char *include_pack;
	struct child_process cmd = CHILD_PROCESS_INIT;
	FILE *cmd_in;
	struct strbuf base_name = STRBUF_INIT;
	struct multi_pack_index *m = lookup_multi_pack_index(r, object_dir);

	/*
	 * When updating the default for these configuration
	 * variables in builtin/repack.c, these must be adjusted
	 * to match.
	 */
	int delta_base_offset = 1;
	int use_delta_islands = 0;

	if (!m)
		return 0;

	CALLOC_ARRAY(include_pack, m->num_packs);

	if (batch_size) {
		if (fill_included_packs_batch(r, m, include_pack, batch_size))
			goto cleanup;
	} else if (fill_included_packs_all(r, m, include_pack))
		goto cleanup;

	repo_config_get_bool(r, "repack.usedeltabaseoffset", &delta_base_offset);
	repo_config_get_bool(r, "repack.usedeltaislands", &use_delta_islands);

	strvec_push(&cmd.args, "pack-objects");

	strbuf_addstr(&base_name, object_dir);
	strbuf_addstr(&base_name, "/pack/pack");
	strvec_push(&cmd.args, base_name.buf);

	if (delta_base_offset)
		strvec_push(&cmd.args, "--delta-base-offset");
	if (use_delta_islands)
		strvec_push(&cmd.args, "--delta-islands");

	if (flags & MIDX_PROGRESS)
		strvec_push(&cmd.args, "--progress");
	else
		strvec_push(&cmd.args, "-q");

	strbuf_release(&base_name);

	cmd.git_cmd = 1;
	cmd.in = cmd.out = -1;

	if (start_command(&cmd)) {
		error(_("could not start pack-objects"));
		result = 1;
		goto cleanup;
	}

	cmd_in = xfdopen(cmd.in, "w");

	for (i = 0; i < m->num_objects; i++) {
		struct object_id oid;
		uint32_t pack_int_id = nth_midxed_pack_int_id(m, i);

		if (!include_pack[pack_int_id])
			continue;

		nth_midxed_object_oid(&oid, m, i);
		fprintf(cmd_in, "%s\n", oid_to_hex(&oid));
	}
	fclose(cmd_in);

	if (finish_command(&cmd)) {
		error(_("could not finish pack-objects"));
		result = 1;
		goto cleanup;
	}

	result = write_midx_internal(object_dir, NULL, NULL, NULL, NULL, flags);

cleanup:
	free(include_pack);
	return result;
}
