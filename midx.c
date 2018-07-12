#include "cache.h"
#include "csum-file.h"
#include "dir.h"
#include "lockfile.h"
#include "packfile.h"
#include "object-store.h"
#include "packfile.h"
#include "midx.h"

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

#define MIDX_MAX_CHUNKS 3
#define MIDX_CHUNK_ALIGNMENT 4
#define MIDX_CHUNKID_PACKNAMES 0x504e414d /* "PNAM" */
#define MIDX_CHUNKID_OIDFANOUT 0x4f494446 /* "OIDF" */
#define MIDX_CHUNKID_OIDLOOKUP 0x4f49444c /* "OIDL" */
#define MIDX_CHUNKLOOKUP_WIDTH (sizeof(uint32_t) + sizeof(uint64_t))
#define MIDX_CHUNK_FANOUT_SIZE (sizeof(uint32_t) * 256)

static char *get_midx_filename(const char *object_dir)
{
	return xstrfmt("%s/pack/multi-pack-index", object_dir);
}

struct multi_pack_index *load_multi_pack_index(const char *object_dir)
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

	m->signature = get_be32(m->data);
	if (m->signature != MIDX_SIGNATURE) {
		error(_("multi-pack-index signature 0x%08x does not match signature 0x%08x"),
		      m->signature, MIDX_SIGNATURE);
		goto cleanup_fail;
	}

	m->version = m->data[MIDX_BYTE_FILE_VERSION];
	if (m->version != MIDX_VERSION) {
		error(_("multi-pack-index version %d not recognized"),
		      m->version);
		goto cleanup_fail;
	}

	hash_version = m->data[MIDX_BYTE_HASH_VERSION];
	if (hash_version != MIDX_HASH_VERSION) {
		error(_("hash version %u does not match"), hash_version);
		goto cleanup_fail;
	}
	m->hash_len = MIDX_HASH_LEN;

	m->num_chunks = m->data[MIDX_BYTE_NUM_CHUNKS];

	m->num_packs = get_be32(m->data + MIDX_BYTE_NUM_PACKS);

	for (i = 0; i < m->num_chunks; i++) {
		uint32_t chunk_id = get_be32(m->data + MIDX_HEADER_SIZE +
					     MIDX_CHUNKLOOKUP_WIDTH * i);
		uint64_t chunk_offset = get_be64(m->data + MIDX_HEADER_SIZE + 4 +
						 MIDX_CHUNKLOOKUP_WIDTH * i);

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

	m->num_objects = ntohl(m->chunk_oid_fanout[255]);

	m->pack_names = xcalloc(m->num_packs, sizeof(*m->pack_names));

	cur_pack_name = (const char *)m->chunk_pack_names;
	for (i = 0; i < m->num_packs; i++) {
		m->pack_names[i] = cur_pack_name;

		cur_pack_name += strlen(cur_pack_name) + 1;

		if (i && strcmp(m->pack_names[i], m->pack_names[i - 1]) <= 0) {
			error(_("multi-pack-index pack names out of order: '%s' before '%s'"),
			      m->pack_names[i - 1],
			      m->pack_names[i]);
			goto cleanup_fail;
		}
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
};

static void add_pack_to_midx(const char *full_path, size_t full_path_len,
			     const char *file_name, void *data)
{
	struct pack_list *packs = (struct pack_list *)data;

	if (ends_with(file_name, ".idx")) {
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
static struct pack_midx_entry *get_sorted_entries(struct packed_git **p,
						  uint32_t *perm,
						  uint32_t nr_packs,
						  uint32_t *nr_objects)
{
	uint32_t cur_fanout, cur_pack, cur_object;
	uint32_t alloc_fanout, alloc_objects, total_objects = 0;
	struct pack_midx_entry *entries_by_fanout = NULL;
	struct pack_midx_entry *deduplicated_entries = NULL;

	for (cur_pack = 0; cur_pack < nr_packs; cur_pack++)
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

		for (cur_pack = 0; cur_pack < nr_packs; cur_pack++) {
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
			if (cur_object && !oidcmp(&entries_by_fanout[cur_object - 1].oid,
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
	uint32_t nr_entries;
	struct pack_midx_entry *entries = NULL;

	midx_name = get_midx_filename(object_dir);
	if (safe_create_leading_directories(midx_name)) {
		UNLEAK(midx_name);
		die_errno(_("unable to create leading directories of %s"),
			  midx_name);
	}

	packs.nr = 0;
	packs.alloc_list = 16;
	packs.alloc_names = 16;
	packs.list = NULL;
	packs.pack_name_concat_len = 0;
	ALLOC_ARRAY(packs.list, packs.alloc_list);
	ALLOC_ARRAY(packs.names, packs.alloc_names);

	for_each_file_in_pack_dir(object_dir, add_pack_to_midx, &packs);

	if (packs.pack_name_concat_len % MIDX_CHUNK_ALIGNMENT)
		packs.pack_name_concat_len += MIDX_CHUNK_ALIGNMENT -
					      (packs.pack_name_concat_len % MIDX_CHUNK_ALIGNMENT);

	ALLOC_ARRAY(pack_perm, packs.nr);
	sort_packs_by_name(packs.names, packs.nr, pack_perm);

	entries = get_sorted_entries(packs.list, pack_perm, packs.nr, &nr_entries);

	hold_lock_file_for_update(&lk, midx_name, LOCK_DIE_ON_ERROR);
	f = hashfd(lk.tempfile->fd, lk.tempfile->filename.buf);
	FREE_AND_NULL(midx_name);

	cur_chunk = 0;
	num_chunks = 3;

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
	chunk_ids[cur_chunk] = 0;
	chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] + nr_entries * MIDX_HASH_LEN;

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
	return 0;
}
