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

#define MIDX_SIGNATURE 0x4d494458 /* "MIDX" */
#define MIDX_VERSION 1
#define MIDX_BYTE_FILE_VERSION 4
#define MIDX_BYTE_HASH_VERSION 5
#define MIDX_BYTE_NUM_CHUNKS 6
#define MIDX_BYTE_NUM_PACKS 8
#define MIDX_HASH_VERSION 1
#define MIDX_HEADER_SIZE 12
#define MIDX_HASH_LEN 20
#define MIDX_MIN_SIZE (MIDX_HEADER_SIZE + MIDX_HASH_LEN)

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

static char *get_midx_filename(const char *object_dir)
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

	FLEX_ALLOC_MEM(m, object_dir, object_dir, strlen(object_dir));
	m->fd = fd;
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
	m->hash_len = MIDX_HASH_LEN;

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
	close(m->fd);
	m->fd = -1;

	for (i = 0; i < m->num_packs; i++) {
		if (m->packs[i]) {
			close_pack(m->packs[i]);
			free(m->packs[i]);
		}
	}
	FREE_AND_NULL(m->packs);
	FREE_AND_NULL(m->pack_names);
}

int prepare_midx_pack(struct multi_pack_index *m, uint32_t pack_int_id)
{
	struct strbuf pack_name = STRBUF_INIT;

	if (pack_int_id >= m->num_packs)
		die(_("bad pack-int-id: %u (%u total packs)"),
		    pack_int_id, m->num_packs);

	if (m->packs[pack_int_id])
		return 0;

	strbuf_addf(&pack_name, "%s/pack/%s", m->object_dir,
		    m->pack_names[pack_int_id]);

	m->packs[pack_int_id] = add_packed_git(pack_name.buf, pack_name.len, m->local);
	strbuf_release(&pack_name);
	return !m->packs[pack_int_id];
}

int bsearch_midx(const struct object_id *oid, struct multi_pack_index *m, uint32_t *result)
{
	return bsearch_hash(oid->hash, m->chunk_oid_fanout, m->chunk_oid_lookup,
			    MIDX_HASH_LEN, result);
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

static int nth_midxed_pack_entry(struct multi_pack_index *m, struct pack_entry *e, uint32_t pos)
{
	uint32_t pack_int_id;
	struct packed_git *p;

	if (pos >= m->num_objects)
		return 0;

	pack_int_id = nth_midxed_pack_int_id(m, pos);

	if (prepare_midx_pack(m, pack_int_id))
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

int fill_midx_entry(const struct object_id *oid, struct pack_entry *e, struct multi_pack_index *m)
{
	uint32_t pos;

	if (!bsearch_midx(oid, m, &pos))
		return 0;

	return nth_midxed_pack_entry(m, e, pos);
}

int midx_contains_pack(struct multi_pack_index *m, const char *idx_name)
{
	uint32_t first = 0, last = m->num_packs;

	while (first < last) {
		uint32_t mid = first + (last - first) / 2;
		const char *current;
		int cmp;

		current = m->pack_names[mid];
		cmp = strcmp(idx_name, current);
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
	int config_value;
	static int env_value = -1;

	if (env_value < 0)
		env_value = git_env_bool(GIT_TEST_MULTI_PACK_INDEX, 0);

	if (!env_value &&
	    (repo_config_get_bool(r, "core.multipackindex", &config_value) ||
	    !config_value))
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

struct pack_list {
	struct packed_git **list;
	char **names;
	uint32_t nr;
	uint32_t alloc_list;
	uint32_t alloc_names;
	size_t pack_name_concat_len;
	struct multi_pack_index *m;
};

static void add_pack_to_midx(const char *full_path, size_t full_path_len,
			     const char *file_name, void *data)
{
	struct pack_list *packs = (struct pack_list *)data;

	if (ends_with(file_name, ".idx")) {
		if (packs->m && midx_contains_pack(packs->m, file_name))
			return;

		ALLOC_GROW(packs->list, packs->nr + 1, packs->alloc_list);
		ALLOC_GROW(packs->names, packs->nr + 1, packs->alloc_names);

		packs->list[packs->nr] = add_packed_git(full_path,
							full_path_len,
							0);

		if (!packs->list[packs->nr]) {
			warning(_("failed to add packfile '%s'"),
				full_path);
			return;
		}

		if (open_pack_index(packs->list[packs->nr])) {
			warning(_("failed to open pack-index '%s'"),
				full_path);
			close_pack(packs->list[packs->nr]);
			FREE_AND_NULL(packs->list[packs->nr]);
			return;
		}

		packs->names[packs->nr] = xstrdup(file_name);
		packs->pack_name_concat_len += strlen(file_name) + 1;
		packs->nr++;
	}
}

struct pack_pair {
	uint32_t pack_int_id;
	char *pack_name;
};

static int pack_pair_compare(const void *_a, const void *_b)
{
	struct pack_pair *a = (struct pack_pair *)_a;
	struct pack_pair *b = (struct pack_pair *)_b;
	return strcmp(a->pack_name, b->pack_name);
}

static void sort_packs_by_name(char **pack_names, uint32_t nr_packs, uint32_t *perm)
{
	uint32_t i;
	struct pack_pair *pairs;

	ALLOC_ARRAY(pairs, nr_packs);

	for (i = 0; i < nr_packs; i++) {
		pairs[i].pack_int_id = i;
		pairs[i].pack_name = pack_names[i];
	}

	QSORT(pairs, nr_packs, pack_pair_compare);

	for (i = 0; i < nr_packs; i++) {
		pack_names[i] = pairs[i].pack_name;
		perm[pairs[i].pack_int_id] = i;
	}

	free(pairs);
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
				      uint32_t *pack_perm,
				      struct pack_midx_entry *e,
				      uint32_t pos)
{
	if (pos >= m->num_objects)
		return 1;

	nth_midxed_object_oid(&e->oid, m, pos);
	e->pack_int_id = pack_perm[nth_midxed_pack_int_id(m, pos)];
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
	if (!nth_packed_object_oid(&entry->oid, p, cur_object))
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
						  struct packed_git **p,
						  uint32_t *perm,
						  uint32_t nr_packs,
						  uint32_t *nr_objects)
{
	uint32_t cur_fanout, cur_pack, cur_object;
	uint32_t alloc_fanout, alloc_objects, total_objects = 0;
	struct pack_midx_entry *entries_by_fanout = NULL;
	struct pack_midx_entry *deduplicated_entries = NULL;
	uint32_t start_pack = m ? m->num_packs : 0;

	for (cur_pack = start_pack; cur_pack < nr_packs; cur_pack++)
		total_objects += p[cur_pack]->num_objects;

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
				nth_midxed_pack_midx_entry(m, perm,
							   &entries_by_fanout[nr_fanout],
							   cur_object);
				nr_fanout++;
			}
		}

		for (cur_pack = start_pack; cur_pack < nr_packs; cur_pack++) {
			uint32_t start = 0, end;

			if (cur_fanout)
				start = get_pack_fanout(p[cur_pack], cur_fanout - 1);
			end = get_pack_fanout(p[cur_pack], cur_fanout);

			for (cur_object = start; cur_object < end; cur_object++) {
				ALLOC_GROW(entries_by_fanout, nr_fanout + 1, alloc_fanout);
				fill_pack_entry(perm[cur_pack], p[cur_pack], cur_object, &entries_by_fanout[nr_fanout]);
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
				    char **pack_names,
				    uint32_t num_packs)
{
	uint32_t i;
	unsigned char padding[MIDX_CHUNK_ALIGNMENT];
	size_t written = 0;

	for (i = 0; i < num_packs; i++) {
		size_t writelen = strlen(pack_names[i]) + 1;

		if (i && strcmp(pack_names[i], pack_names[i - 1]) <= 0)
			BUG("incorrect pack-file order: %s before %s",
			    pack_names[i - 1],
			    pack_names[i]);

		hashwrite(f, pack_names[i], writelen);
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
					struct pack_midx_entry *objects, uint32_t nr_objects)
{
	struct pack_midx_entry *list = objects;
	uint32_t i, nr_large_offset = 0;
	size_t written = 0;

	for (i = 0; i < nr_objects; i++) {
		struct pack_midx_entry *obj = list++;

		hashwrite_be32(f, obj->pack_int_id);

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

int write_midx_file(const char *object_dir)
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
	int large_offsets_needed = 0;

	midx_name = get_midx_filename(object_dir);
	if (safe_create_leading_directories(midx_name)) {
		UNLEAK(midx_name);
		die_errno(_("unable to create leading directories of %s"),
			  midx_name);
	}

	packs.m = load_multi_pack_index(object_dir, 1);

	packs.nr = 0;
	packs.alloc_list = packs.m ? packs.m->num_packs : 16;
	packs.alloc_names = packs.alloc_list;
	packs.list = NULL;
	packs.names = NULL;
	packs.pack_name_concat_len = 0;
	ALLOC_ARRAY(packs.list, packs.alloc_list);
	ALLOC_ARRAY(packs.names, packs.alloc_names);

	if (packs.m) {
		for (i = 0; i < packs.m->num_packs; i++) {
			ALLOC_GROW(packs.list, packs.nr + 1, packs.alloc_list);
			ALLOC_GROW(packs.names, packs.nr + 1, packs.alloc_names);

			packs.list[packs.nr] = NULL;
			packs.names[packs.nr] = xstrdup(packs.m->pack_names[i]);
			packs.pack_name_concat_len += strlen(packs.names[packs.nr]) + 1;
			packs.nr++;
		}
	}

	for_each_file_in_pack_dir(object_dir, add_pack_to_midx, &packs);

	if (packs.m && packs.nr == packs.m->num_packs)
		goto cleanup;

	if (packs.pack_name_concat_len % MIDX_CHUNK_ALIGNMENT)
		packs.pack_name_concat_len += MIDX_CHUNK_ALIGNMENT -
					      (packs.pack_name_concat_len % MIDX_CHUNK_ALIGNMENT);

	ALLOC_ARRAY(pack_perm, packs.nr);
	sort_packs_by_name(packs.names, packs.nr, pack_perm);

	entries = get_sorted_entries(packs.m, packs.list, pack_perm, packs.nr, &nr_entries);

	for (i = 0; i < nr_entries; i++) {
		if (entries[i].offset > 0x7fffffff)
			num_large_offsets++;
		if (entries[i].offset > 0xffffffff)
			large_offsets_needed = 1;
	}

	hold_lock_file_for_update(&lk, midx_name, LOCK_DIE_ON_ERROR);
	f = hashfd(lk.tempfile->fd, lk.tempfile->filename.buf);
	FREE_AND_NULL(midx_name);

	if (packs.m)
		close_midx(packs.m);

	cur_chunk = 0;
	num_chunks = large_offsets_needed ? 5 : 4;

	written = write_midx_header(f, num_chunks, packs.nr);

	chunk_ids[cur_chunk] = MIDX_CHUNKID_PACKNAMES;
	chunk_offsets[cur_chunk] = written + (num_chunks + 1) * MIDX_CHUNKLOOKUP_WIDTH;

	cur_chunk++;
	chunk_ids[cur_chunk] = MIDX_CHUNKID_OIDFANOUT;
	chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] + packs.pack_name_concat_len;

	cur_chunk++;
	chunk_ids[cur_chunk] = MIDX_CHUNKID_OIDLOOKUP;
	chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] + MIDX_CHUNK_FANOUT_SIZE;

	cur_chunk++;
	chunk_ids[cur_chunk] = MIDX_CHUNKID_OBJECTOFFSETS;
	chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] + nr_entries * MIDX_HASH_LEN;

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

	for (i = 0; i < num_chunks; i++) {
		if (written != chunk_offsets[i])
			BUG("incorrect chunk offset (%"PRIu64" != %"PRIu64") for chunk id %"PRIx32,
			    chunk_offsets[i],
			    written,
			    chunk_ids[i]);

		switch (chunk_ids[i]) {
			case MIDX_CHUNKID_PACKNAMES:
				written += write_midx_pack_names(f, packs.names, packs.nr);
				break;

			case MIDX_CHUNKID_OIDFANOUT:
				written += write_midx_oid_fanout(f, entries, nr_entries);
				break;

			case MIDX_CHUNKID_OIDLOOKUP:
				written += write_midx_oid_lookup(f, MIDX_HASH_LEN, entries, nr_entries);
				break;

			case MIDX_CHUNKID_OBJECTOFFSETS:
				written += write_midx_object_offsets(f, large_offsets_needed, entries, nr_entries);
				break;

			case MIDX_CHUNKID_LARGEOFFSETS:
				written += write_midx_large_offsets(f, num_large_offsets, entries, nr_entries);
				break;

			default:
				BUG("trying to write unknown chunk id %"PRIx32,
				    chunk_ids[i]);
		}
	}

	if (written != chunk_offsets[num_chunks])
		BUG("incorrect final offset %"PRIu64" != %"PRIu64,
		    written,
		    chunk_offsets[num_chunks]);

	finalize_hashfile(f, NULL, CSUM_FSYNC | CSUM_HASH_IN_STREAM);
	commit_lock_file(&lk);

cleanup:
	for (i = 0; i < packs.nr; i++) {
		if (packs.list[i]) {
			close_pack(packs.list[i]);
			free(packs.list[i]);
		}
		free(packs.names[i]);
	}

	free(packs.list);
	free(packs.names);
	free(entries);
	free(pack_perm);
	free(midx_name);
	return 0;
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

int verify_midx_file(const char *object_dir)
{
	uint32_t i;
	struct progress *progress;
	struct multi_pack_index *m = load_multi_pack_index(object_dir, 1);
	verify_midx_error = 0;

	if (!m)
		return 0;

	for (i = 0; i < m->num_packs; i++) {
		if (prepare_midx_pack(m, i))
			midx_report("failed to load pack in position %d", i);
	}

	for (i = 0; i < 255; i++) {
		uint32_t oid_fanout1 = ntohl(m->chunk_oid_fanout[i]);
		uint32_t oid_fanout2 = ntohl(m->chunk_oid_fanout[i + 1]);

		if (oid_fanout1 > oid_fanout2)
			midx_report(_("oid fanout out of order: fanout[%d] = %"PRIx32" > %"PRIx32" = fanout[%d]"),
				    i, oid_fanout1, oid_fanout2, i + 1);
	}

	for (i = 0; i < m->num_objects - 1; i++) {
		struct object_id oid1, oid2;

		nth_midxed_object_oid(&oid1, m, i);
		nth_midxed_object_oid(&oid2, m, i + 1);

		if (oidcmp(&oid1, &oid2) >= 0)
			midx_report(_("oid lookup out of order: oid[%d] = %s >= %s = oid[%d]"),
				    i, oid_to_hex(&oid1), oid_to_hex(&oid2), i + 1);
	}

	progress = start_progress(_("Verifying object offsets"), m->num_objects);
	for (i = 0; i < m->num_objects; i++) {
		struct object_id oid;
		struct pack_entry e;
		off_t m_offset, p_offset;

		nth_midxed_object_oid(&oid, m, i);
		if (!fill_midx_entry(&oid, &e, m)) {
			midx_report(_("failed to load pack entry for oid[%d] = %s"),
				    i, oid_to_hex(&oid));
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
				    i, oid_to_hex(&oid), m_offset, p_offset);

		display_progress(progress, i + 1);
	}
	stop_progress(&progress);

	return verify_midx_error;
}
