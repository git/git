#include "cache.h"
#include "config.h"
#include "csum-file.h"
#include "dir.h"
#include "lockfile.h"
#include "packfile.h"
#include "object-store.h"
#include "sha1-lookup.h"
#include "midx.h"
#include "progress.h"
#include "trace2.h"
#include "run-command.h"
#include "repository.h"

#define MIDX_SIGNATURE 0x4d494458 /* "MIDX" */
#define MIDX_VERSION 1
#define MIDX_BYTE_FILE_VERSION 4
#define MIDX_BYTE_HASH_VERSION 5
#define MIDX_BYTE_NUM_CHUNKS 6
#define MIDX_BYTE_NUM_PACKS 8
#define MIDX_HASH_VERSION 1
#define MIDX_HEADER_SIZE 12
#define MIDX_MIN_SIZE (MIDX_HEADER_SIZE + the_hash_algo->rawsz)

#define MIDX_MAX_CHUNKS 5
#define MIDX_CHUNK_ALIGNMENT 4
#define MIDX_CHUNKID_PACKNAMES 0x504e414d /* "PNAM" */
#define MIDX_CHUNKID_OIDFANOUT 0x4f494446 /* "OIDF" */
#define MIDX_CHUNKID_OIDLOOKUP 0x4f49444c /* "OIDL" */
#define MIDX_CHUNKID_OBJECTOFFSETS 0x4f4f4646 /* "OOFF" */
#define MIDX_CHUNKID_LARGEOFFSETS 0x4c4f4646 /* "LOFF" */
#define MIDX_CHUNKLOOKUP_WIDTH (sizeof(uint32_t) + sizeof(uint64_t))
#define MIDX_CHUNK_FANOUT_SIZE (sizeof(uint32_t) * 256)
#define MIDX_CHUNK_OFFSET_WIDTH (2 * sizeof(uint32_t))
#define MIDX_CHUNK_LARGE_OFFSET_WIDTH (sizeof(uint64_t))
#define MIDX_LARGE_OFFSET_NEEDED 0x80000000

#define PACK_EXPIRED UINT_MAX

char *get_midx_filename(const char *object_dir)
{
	return xstrfmt("%s/pack/multi-pack-index", object_dir);
}

struct multi_pack_index *load_multi_pack_index(const char *object_dir, int local)
{
	struct multi_pack_index *m = NULL;
	int fd;
	struct stat st;
	size_t midx_size;
	void *midx_map = NULL;
	uint32_t hash_version;
	char *midx_name = get_midx_filename(object_dir);
	uint32_t i;
	const char *cur_pack_name;

	fd = git_open(midx_name);

	if (fd < 0)
		goto cleanup_fail;
	if (fstat(fd, &st)) {
		error_errno(_("failed to read %s"), midx_name);
		goto cleanup_fail;
	}

	midx_size = xsize_t(st.st_size);

	if (midx_size < MIDX_MIN_SIZE) {
		error(_("multi-pack-index file %s is too small"), midx_name);
		goto cleanup_fail;
	}

	FREE_AND_NULL(midx_name);

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
	if (hash_version != MIDX_HASH_VERSION)
		die(_("hash version %u does not match"), hash_version);
	m->hash_len = the_hash_algo->rawsz;

	m->num_chunks = m->data[MIDX_BYTE_NUM_CHUNKS];

	m->num_packs = get_be32(m->data + MIDX_BYTE_NUM_PACKS);

	for (i = 0; i < m->num_chunks; i++) {
		uint32_t chunk_id = get_be32(m->data + MIDX_HEADER_SIZE +
					     MIDX_CHUNKLOOKUP_WIDTH * i);
		uint64_t chunk_offset = get_be64(m->data + MIDX_HEADER_SIZE + 4 +
						 MIDX_CHUNKLOOKUP_WIDTH * i);

		if (chunk_offset >= m->data_len)
			die(_("invalid chunk offset (too large)"));

		switch (chunk_id) {
			case MIDX_CHUNKID_PACKNAMES:
				m->chunk_pack_names = m->data + chunk_offset;
				break;

			case MIDX_CHUNKID_OIDFANOUT:
				m->chunk_oid_fanout = (uint32_t *)(m->data + chunk_offset);
				break;

			case MIDX_CHUNKID_OIDLOOKUP:
				m->chunk_oid_lookup = m->data + chunk_offset;
				break;

			case MIDX_CHUNKID_OBJECTOFFSETS:
				m->chunk_object_offsets = m->data + chunk_offset;
				break;

			case MIDX_CHUNKID_LARGEOFFSETS:
				m->chunk_large_offsets = m->data + chunk_offset;
				break;

			case 0:
				die(_("terminating multi-pack-index chunk id appears earlier than expected"));
				break;

			default:
				/*
				 * Do nothing on unrecognized chunks, allowing future
				 * extensions to add optional chunks.
				 */
				break;
		}
	}

	if (!m->chunk_pack_names)
		die(_("multi-pack-index missing required pack-name chunk"));
	if (!m->chunk_oid_fanout)
		die(_("multi-pack-index missing required OID fanout chunk"));
	if (!m->chunk_oid_lookup)
		die(_("multi-pack-index missing required OID lookup chunk"));
	if (!m->chunk_object_offsets)
		die(_("multi-pack-index missing required object offsets chunk"));

	m->num_objects = ntohl(m->chunk_oid_fanout[255]);

	m->pack_names = xcalloc(m->num_packs, sizeof(*m->pack_names));
	m->packs = xcalloc(m->num_packs, sizeof(*m->packs));

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

	return m;

cleanup_fail:
	free(m);
	free(midx_name);
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

	munmap((unsigned char *)m->data, m->data_len);

	for (i = 0; i < m->num_packs; i++) {
		if (m->packs[i])
			m->packs[i]->multi_pack_index = 0;
	}
	FREE_AND_NULL(m->packs);
	FREE_AND_NULL(m->pack_names);
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

	hashcpy(oid->hash, m->chunk_oid_lookup + m->hash_len * n);
	return oid;
}

static off_t nth_midxed_offset(struct multi_pack_index *m, uint32_t pos)
{
	const unsigned char *offset_data;
	uint32_t offset32;

	offset_data = m->chunk_object_offsets + pos * MIDX_CHUNK_OFFSET_WIDTH;
	offset32 = get_be32(offset_data + sizeof(uint32_t));

	if (m->chunk_large_offsets && offset32 & MIDX_LARGE_OFFSET_NEEDED) {
		if (sizeof(off_t) < sizeof(uint64_t))
			die(_("multi-pack-index stores a 64-bit offset, but off_t is too small"));

		offset32 ^= MIDX_LARGE_OFFSET_NEEDED;
		return get_be64(m->chunk_large_offsets + sizeof(uint64_t) * offset32);
	}

	return offset32;
}

static uint32_t nth_midxed_pack_int_id(struct multi_pack_index *m, uint32_t pos)
{
	return get_be32(m->chunk_object_offsets + pos * MIDX_CHUNK_OFFSET_WIDTH);
}

static int nth_midxed_pack_entry(struct repository *r,
				 struct multi_pack_index *m,
				 struct pack_entry *e,
				 uint32_t pos)
{
	uint32_t pack_int_id;
	struct packed_git *p;

	if (pos >= m->num_objects)
		return 0;

	pack_int_id = nth_midxed_pack_int_id(m, pos);

	if (prepare_midx_pack(r, m, pack_int_id))
		die(_("error preparing packfile from multi-pack-index"));
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

	if (p->num_bad_objects) {
		uint32_t i;
		struct object_id oid;
		nth_midxed_object_oid(&oid, m, pos);
		for (i = 0; i < p->num_bad_objects; i++)
			if (hasheq(oid.hash,
				   p->bad_object_sha1 + the_hash_algo->rawsz * i))
				return 0;
	}

	e->offset = nth_midxed_offset(m, pos);
	e->p = p;

	return 1;
}

int fill_midx_entry(struct repository * r,
		    const struct object_id *oid,
		    struct pack_entry *e,
		    struct multi_pack_index *m)
{
	uint32_t pos;

	if (!bsearch_midx(oid, m, &pos))
		return 0;

	return nth_midxed_pack_entry(r, m, e, pos);
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
		m->next = r->objects->multi_pack_index;
		r->objects->multi_pack_index = m;
		return 1;
	}

	return 0;
}

static size_t write_midx_header(struct hashfile *f,
				unsigned char num_chunks,
				uint32_t num_packs)
{
	unsigned char byte_values[4];

	hashwrite_be32(f, MIDX_SIGNATURE);
	byte_values[0] = MIDX_VERSION;
	byte_values[1] = MIDX_HASH_VERSION;
	byte_values[2] = num_chunks;
	byte_values[3] = 0; /* unused */
	hashwrite(f, byte_values, sizeof(byte_values));
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

struct pack_list {
	struct pack_info *info;
	uint32_t nr;
	uint32_t alloc;
	struct multi_pack_index *m;
	struct progress *progress;
	unsigned pack_paths_checked;
};

static void add_pack_to_midx(const char *full_path, size_t full_path_len,
			     const char *file_name, void *data)
{
	struct pack_list *packs = (struct pack_list *)data;

	if (ends_with(file_name, ".idx")) {
		display_progress(packs->progress, ++packs->pack_paths_checked);
		if (packs->m && midx_contains_pack(packs->m, file_name))
			return;

		ALLOC_GROW(packs->info, packs->nr + 1, packs->alloc);

		packs->info[packs->nr].p = add_packed_git(full_path,
							  full_path_len,
							  0);

		if (!packs->info[packs->nr].p) {
			warning(_("failed to add packfile '%s'"),
				full_path);
			return;
		}

		if (open_pack_index(packs->info[packs->nr].p)) {
			warning(_("failed to open pack-index '%s'"),
				full_path);
			close_pack(packs->info[packs->nr].p);
			FREE_AND_NULL(packs->info[packs->nr].p);
			return;
		}

		packs->info[packs->nr].pack_name = xstrdup(file_name);
		packs->info[packs->nr].orig_pack_int_id = packs->nr;
		packs->info[packs->nr].expired = 0;
		packs->nr++;
	}
}

struct pack_midx_entry {
	struct object_id oid;
	uint32_t pack_int_id;
	time_t pack_mtime;
	uint64_t offset;
};

static int midx_oid_compare(const void *_a, const void *_b)
{
	const struct pack_midx_entry *a = (const struct pack_midx_entry *)_a;
	const struct pack_midx_entry *b = (const struct pack_midx_entry *)_b;
	int cmp = oidcmp(&a->oid, &b->oid);

	if (cmp)
		return cmp;

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
			    struct pack_midx_entry *entry)
{
	if (nth_packed_object_id(&entry->oid, p, cur_object) < 0)
		die(_("failed to locate object %d in packfile"), cur_object);

	entry->pack_int_id = pack_int_id;
	entry->pack_mtime = p->mtime;

	entry->offset = nth_packed_object_offset(p, cur_object);
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
						  uint32_t *nr_objects)
{
	uint32_t cur_fanout, cur_pack, cur_object;
	uint32_t alloc_fanout, alloc_objects, total_objects = 0;
	struct pack_midx_entry *entries_by_fanout = NULL;
	struct pack_midx_entry *deduplicated_entries = NULL;
	uint32_t start_pack = m ? m->num_packs : 0;

	for (cur_pack = start_pack; cur_pack < nr_packs; cur_pack++)
		total_objects += info[cur_pack].p->num_objects;

	/*
	 * As we de-duplicate by fanout value, we expect the fanout
	 * slices to be evenly distributed, with some noise. Hence,
	 * allocate slightly more than one 256th.
	 */
	alloc_objects = alloc_fanout = total_objects > 3200 ? total_objects / 200 : 16;

	ALLOC_ARRAY(entries_by_fanout, alloc_fanout);
	ALLOC_ARRAY(deduplicated_entries, alloc_objects);
	*nr_objects = 0;

	for (cur_fanout = 0; cur_fanout < 256; cur_fanout++) {
		uint32_t nr_fanout = 0;

		if (m) {
			uint32_t start = 0, end;

			if (cur_fanout)
				start = ntohl(m->chunk_oid_fanout[cur_fanout - 1]);
			end = ntohl(m->chunk_oid_fanout[cur_fanout]);

			for (cur_object = start; cur_object < end; cur_object++) {
				ALLOC_GROW(entries_by_fanout, nr_fanout + 1, alloc_fanout);
				nth_midxed_pack_midx_entry(m,
							   &entries_by_fanout[nr_fanout],
							   cur_object);
				nr_fanout++;
			}
		}

		for (cur_pack = start_pack; cur_pack < nr_packs; cur_pack++) {
			uint32_t start = 0, end;

			if (cur_fanout)
				start = get_pack_fanout(info[cur_pack].p, cur_fanout - 1);
			end = get_pack_fanout(info[cur_pack].p, cur_fanout);

			for (cur_object = start; cur_object < end; cur_object++) {
				ALLOC_GROW(entries_by_fanout, nr_fanout + 1, alloc_fanout);
				fill_pack_entry(cur_pack, info[cur_pack].p, cur_object, &entries_by_fanout[nr_fanout]);
				nr_fanout++;
			}
		}

		QSORT(entries_by_fanout, nr_fanout, midx_oid_compare);

		/*
		 * The batch is now sorted by OID and then mtime (descending).
		 * Take only the first duplicate.
		 */
		for (cur_object = 0; cur_object < nr_fanout; cur_object++) {
			if (cur_object && oideq(&entries_by_fanout[cur_object - 1].oid,
						&entries_by_fanout[cur_object].oid))
				continue;

			ALLOC_GROW(deduplicated_entries, *nr_objects + 1, alloc_objects);
			memcpy(&deduplicated_entries[*nr_objects],
			       &entries_by_fanout[cur_object],
			       sizeof(struct pack_midx_entry));
			(*nr_objects)++;
		}
	}

	free(entries_by_fanout);
	return deduplicated_entries;
}

static size_t write_midx_pack_names(struct hashfile *f,
				    struct pack_info *info,
				    uint32_t num_packs)
{
	uint32_t i;
	unsigned char padding[MIDX_CHUNK_ALIGNMENT];
	size_t written = 0;

	for (i = 0; i < num_packs; i++) {
		size_t writelen;

		if (info[i].expired)
			continue;

		if (i && strcmp(info[i].pack_name, info[i - 1].pack_name) <= 0)
			BUG("incorrect pack-file order: %s before %s",
			    info[i - 1].pack_name,
			    info[i].pack_name);

		writelen = strlen(info[i].pack_name) + 1;
		hashwrite(f, info[i].pack_name, writelen);
		written += writelen;
	}

	/* add padding to be aligned */
	i = MIDX_CHUNK_ALIGNMENT - (written % MIDX_CHUNK_ALIGNMENT);
	if (i < MIDX_CHUNK_ALIGNMENT) {
		memset(padding, 0, sizeof(padding));
		hashwrite(f, padding, i);
		written += i;
	}

	return written;
}

static size_t write_midx_oid_fanout(struct hashfile *f,
				    struct pack_midx_entry *objects,
				    uint32_t nr_objects)
{
	struct pack_midx_entry *list = objects;
	struct pack_midx_entry *last = objects + nr_objects;
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

	return MIDX_CHUNK_FANOUT_SIZE;
}

static size_t write_midx_oid_lookup(struct hashfile *f, unsigned char hash_len,
				    struct pack_midx_entry *objects,
				    uint32_t nr_objects)
{
	struct pack_midx_entry *list = objects;
	uint32_t i;
	size_t written = 0;

	for (i = 0; i < nr_objects; i++) {
		struct pack_midx_entry *obj = list++;

		if (i < nr_objects - 1) {
			struct pack_midx_entry *next = list;
			if (oidcmp(&obj->oid, &next->oid) >= 0)
				BUG("OIDs not in order: %s >= %s",
				    oid_to_hex(&obj->oid),
				    oid_to_hex(&next->oid));
		}

		hashwrite(f, obj->oid.hash, (int)hash_len);
		written += hash_len;
	}

	return written;
}

static size_t write_midx_object_offsets(struct hashfile *f, int large_offset_needed,
					uint32_t *perm,
					struct pack_midx_entry *objects, uint32_t nr_objects)
{
	struct pack_midx_entry *list = objects;
	uint32_t i, nr_large_offset = 0;
	size_t written = 0;

	for (i = 0; i < nr_objects; i++) {
		struct pack_midx_entry *obj = list++;

		if (perm[obj->pack_int_id] == PACK_EXPIRED)
			BUG("object %s is in an expired pack with int-id %d",
			    oid_to_hex(&obj->oid),
			    obj->pack_int_id);

		hashwrite_be32(f, perm[obj->pack_int_id]);

		if (large_offset_needed && obj->offset >> 31)
			hashwrite_be32(f, MIDX_LARGE_OFFSET_NEEDED | nr_large_offset++);
		else if (!large_offset_needed && obj->offset >> 32)
			BUG("object %s requires a large offset (%"PRIx64") but the MIDX is not writing large offsets!",
			    oid_to_hex(&obj->oid),
			    obj->offset);
		else
			hashwrite_be32(f, (uint32_t)obj->offset);

		written += MIDX_CHUNK_OFFSET_WIDTH;
	}

	return written;
}

static size_t write_midx_large_offsets(struct hashfile *f, uint32_t nr_large_offset,
				       struct pack_midx_entry *objects, uint32_t nr_objects)
{
	struct pack_midx_entry *list = objects, *end = objects + nr_objects;
	size_t written = 0;

	while (nr_large_offset) {
		struct pack_midx_entry *obj;
		uint64_t offset;

		if (list >= end)
			BUG("too many large-offset objects");

		obj = list++;
		offset = obj->offset;

		if (!(offset >> 31))
			continue;

		hashwrite_be32(f, offset >> 32);
		hashwrite_be32(f, offset & 0xffffffffUL);
		written += 2 * sizeof(uint32_t);

		nr_large_offset--;
	}

	return written;
}

static int write_midx_internal(const char *object_dir, struct multi_pack_index *m,
			       struct string_list *packs_to_drop, unsigned flags)
{
	unsigned char cur_chunk, num_chunks = 0;
	char *midx_name;
	uint32_t i;
	struct hashfile *f = NULL;
	struct lock_file lk;
	struct pack_list packs;
	uint32_t *pack_perm = NULL;
	uint64_t written = 0;
	uint32_t chunk_ids[MIDX_MAX_CHUNKS + 1];
	uint64_t chunk_offsets[MIDX_MAX_CHUNKS + 1];
	uint32_t nr_entries, num_large_offsets = 0;
	struct pack_midx_entry *entries = NULL;
	struct progress *progress = NULL;
	int large_offsets_needed = 0;
	int pack_name_concat_len = 0;
	int dropped_packs = 0;
	int result = 0;

	midx_name = get_midx_filename(object_dir);
	if (safe_create_leading_directories(midx_name)) {
		UNLEAK(midx_name);
		die_errno(_("unable to create leading directories of %s"),
			  midx_name);
	}

	if (m)
		packs.m = m;
	else
		packs.m = load_multi_pack_index(object_dir, 1);

	packs.nr = 0;
	packs.alloc = packs.m ? packs.m->num_packs : 16;
	packs.info = NULL;
	ALLOC_ARRAY(packs.info, packs.alloc);

	if (packs.m) {
		for (i = 0; i < packs.m->num_packs; i++) {
			ALLOC_GROW(packs.info, packs.nr + 1, packs.alloc);

			packs.info[packs.nr].orig_pack_int_id = i;
			packs.info[packs.nr].pack_name = xstrdup(packs.m->pack_names[i]);
			packs.info[packs.nr].p = NULL;
			packs.info[packs.nr].expired = 0;
			packs.nr++;
		}
	}

	packs.pack_paths_checked = 0;
	if (flags & MIDX_PROGRESS)
		packs.progress = start_delayed_progress(_("Adding packfiles to multi-pack-index"), 0);
	else
		packs.progress = NULL;

	for_each_file_in_pack_dir(object_dir, add_pack_to_midx, &packs);
	stop_progress(&packs.progress);

	if (packs.m && packs.nr == packs.m->num_packs && !packs_to_drop)
		goto cleanup;

	entries = get_sorted_entries(packs.m, packs.info, packs.nr, &nr_entries);

	for (i = 0; i < nr_entries; i++) {
		if (entries[i].offset > 0x7fffffff)
			num_large_offsets++;
		if (entries[i].offset > 0xffffffff)
			large_offsets_needed = 1;
	}

	QSORT(packs.info, packs.nr, pack_info_compare);

	if (packs_to_drop && packs_to_drop->nr) {
		int drop_index = 0;
		int missing_drops = 0;

		for (i = 0; i < packs.nr && drop_index < packs_to_drop->nr; i++) {
			int cmp = strcmp(packs.info[i].pack_name,
					 packs_to_drop->items[drop_index].string);

			if (!cmp) {
				drop_index++;
				packs.info[i].expired = 1;
			} else if (cmp > 0) {
				error(_("did not see pack-file %s to drop"),
				      packs_to_drop->items[drop_index].string);
				drop_index++;
				missing_drops++;
				i--;
			} else {
				packs.info[i].expired = 0;
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
	ALLOC_ARRAY(pack_perm, packs.nr);
	for (i = 0; i < packs.nr; i++) {
		if (packs.info[i].expired) {
			dropped_packs++;
			pack_perm[packs.info[i].orig_pack_int_id] = PACK_EXPIRED;
		} else {
			pack_perm[packs.info[i].orig_pack_int_id] = i - dropped_packs;
		}
	}

	for (i = 0; i < packs.nr; i++) {
		if (!packs.info[i].expired)
			pack_name_concat_len += strlen(packs.info[i].pack_name) + 1;
	}

	if (pack_name_concat_len % MIDX_CHUNK_ALIGNMENT)
		pack_name_concat_len += MIDX_CHUNK_ALIGNMENT -
					(pack_name_concat_len % MIDX_CHUNK_ALIGNMENT);

	hold_lock_file_for_update(&lk, midx_name, LOCK_DIE_ON_ERROR);
	f = hashfd(lk.tempfile->fd, lk.tempfile->filename.buf);
	FREE_AND_NULL(midx_name);

	if (packs.m)
		close_midx(packs.m);

	cur_chunk = 0;
	num_chunks = large_offsets_needed ? 5 : 4;

	if (packs.nr - dropped_packs == 0) {
		error(_("no pack files to index."));
		result = 1;
		goto cleanup;
	}

	written = write_midx_header(f, num_chunks, packs.nr - dropped_packs);

	chunk_ids[cur_chunk] = MIDX_CHUNKID_PACKNAMES;
	chunk_offsets[cur_chunk] = written + (num_chunks + 1) * MIDX_CHUNKLOOKUP_WIDTH;

	cur_chunk++;
	chunk_ids[cur_chunk] = MIDX_CHUNKID_OIDFANOUT;
	chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] + pack_name_concat_len;

	cur_chunk++;
	chunk_ids[cur_chunk] = MIDX_CHUNKID_OIDLOOKUP;
	chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] + MIDX_CHUNK_FANOUT_SIZE;

	cur_chunk++;
	chunk_ids[cur_chunk] = MIDX_CHUNKID_OBJECTOFFSETS;
	chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] + nr_entries * the_hash_algo->rawsz;

	cur_chunk++;
	chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] + nr_entries * MIDX_CHUNK_OFFSET_WIDTH;
	if (large_offsets_needed) {
		chunk_ids[cur_chunk] = MIDX_CHUNKID_LARGEOFFSETS;

		cur_chunk++;
		chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] +
					   num_large_offsets * MIDX_CHUNK_LARGE_OFFSET_WIDTH;
	}

	chunk_ids[cur_chunk] = 0;

	for (i = 0; i <= num_chunks; i++) {
		if (i && chunk_offsets[i] < chunk_offsets[i - 1])
			BUG("incorrect chunk offsets: %"PRIu64" before %"PRIu64,
			    chunk_offsets[i - 1],
			    chunk_offsets[i]);

		if (chunk_offsets[i] % MIDX_CHUNK_ALIGNMENT)
			BUG("chunk offset %"PRIu64" is not properly aligned",
			    chunk_offsets[i]);

		hashwrite_be32(f, chunk_ids[i]);
		hashwrite_be32(f, chunk_offsets[i] >> 32);
		hashwrite_be32(f, chunk_offsets[i]);

		written += MIDX_CHUNKLOOKUP_WIDTH;
	}

	if (flags & MIDX_PROGRESS)
		progress = start_delayed_progress(_("Writing chunks to multi-pack-index"),
					  num_chunks);
	for (i = 0; i < num_chunks; i++) {
		if (written != chunk_offsets[i])
			BUG("incorrect chunk offset (%"PRIu64" != %"PRIu64") for chunk id %"PRIx32,
			    chunk_offsets[i],
			    written,
			    chunk_ids[i]);

		switch (chunk_ids[i]) {
			case MIDX_CHUNKID_PACKNAMES:
				written += write_midx_pack_names(f, packs.info, packs.nr);
				break;

			case MIDX_CHUNKID_OIDFANOUT:
				written += write_midx_oid_fanout(f, entries, nr_entries);
				break;

			case MIDX_CHUNKID_OIDLOOKUP:
				written += write_midx_oid_lookup(f, the_hash_algo->rawsz, entries, nr_entries);
				break;

			case MIDX_CHUNKID_OBJECTOFFSETS:
				written += write_midx_object_offsets(f, large_offsets_needed, pack_perm, entries, nr_entries);
				break;

			case MIDX_CHUNKID_LARGEOFFSETS:
				written += write_midx_large_offsets(f, num_large_offsets, entries, nr_entries);
				break;

			default:
				BUG("trying to write unknown chunk id %"PRIx32,
				    chunk_ids[i]);
		}

		display_progress(progress, i + 1);
	}
	stop_progress(&progress);

	if (written != chunk_offsets[num_chunks])
		BUG("incorrect final offset %"PRIu64" != %"PRIu64,
		    written,
		    chunk_offsets[num_chunks]);

	finalize_hashfile(f, NULL, CSUM_FSYNC | CSUM_HASH_IN_STREAM);
	commit_lock_file(&lk);

cleanup:
	for (i = 0; i < packs.nr; i++) {
		if (packs.info[i].p) {
			close_pack(packs.info[i].p);
			free(packs.info[i].p);
		}
		free(packs.info[i].pack_name);
	}

	free(packs.info);
	free(entries);
	free(pack_perm);
	free(midx_name);
	return result;
}

int write_midx_file(const char *object_dir, unsigned flags)
{
	return write_midx_internal(object_dir, NULL, NULL, flags);
}

void clear_midx_file(struct repository *r)
{
	char *midx = get_midx_filename(r->objects->odb->path);

	if (r->objects && r->objects->multi_pack_index) {
		close_midx(r->objects->multi_pack_index);
		r->objects->multi_pack_index = NULL;
	}

	if (remove_path(midx)) {
		UNLEAK(midx);
		die(_("failed to clear multi-pack-index at %s"), midx);
	}

	free(midx);
}

static int verify_midx_error;

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

	if (!m)
		return 0;

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
		return verify_midx_error;
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

	free(pairs);

	return verify_midx_error;
}

int expire_midx_packs(struct repository *r, const char *object_dir, unsigned flags)
{
	uint32_t i, *count, result = 0;
	struct string_list packs_to_drop = STRING_LIST_INIT_DUP;
	struct multi_pack_index *m = load_multi_pack_index(object_dir, 1);
	struct progress *progress = NULL;

	if (!m)
		return 0;

	count = xcalloc(m->num_packs, sizeof(uint32_t));

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

		if (m->packs[i]->pack_keep)
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
		result = write_midx_internal(object_dir, m, &packs_to_drop, flags);

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
	struct repack_info *pack_info = xcalloc(m->num_packs, sizeof(struct repack_info));
	int pack_kept_objects = 0;

	repo_config_get_bool(r, "repack.packkeptobjects", &pack_kept_objects);

	for (i = 0; i < m->num_packs; i++) {
		pack_info[i].pack_int_id = i;

		if (prepare_midx_pack(r, m, i))
			continue;

		pack_info[i].mtime = m->packs[i]->mtime;
	}

	for (i = 0; batch_size && i < m->num_objects; i++) {
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

	if (total_size < batch_size || packs_to_repack < 2)
		return 1;

	return 0;
}

int midx_repack(struct repository *r, const char *object_dir, size_t batch_size, unsigned flags)
{
	int result = 0;
	uint32_t i;
	unsigned char *include_pack;
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf base_name = STRBUF_INIT;
	struct multi_pack_index *m = load_multi_pack_index(object_dir, 1);

	/*
	 * When updating the default for these configuration
	 * variables in builtin/repack.c, these must be adjusted
	 * to match.
	 */
	int delta_base_offset = 1;
	int use_delta_islands = 0;

	if (!m)
		return 0;

	include_pack = xcalloc(m->num_packs, sizeof(unsigned char));

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

	for (i = 0; i < m->num_objects; i++) {
		struct object_id oid;
		uint32_t pack_int_id = nth_midxed_pack_int_id(m, i);

		if (!include_pack[pack_int_id])
			continue;

		nth_midxed_object_oid(&oid, m, i);
		xwrite(cmd.in, oid_to_hex(&oid), the_hash_algo->hexsz);
		xwrite(cmd.in, "\n", 1);
	}
	close(cmd.in);

	if (finish_command(&cmd)) {
		error(_("could not finish pack-objects"));
		result = 1;
		goto cleanup;
	}

	result = write_midx_internal(object_dir, m, NULL, flags);
	m = NULL;

cleanup:
	if (m)
		close_midx(m);
	free(include_pack);
	return result;
}
