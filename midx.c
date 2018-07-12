#include "cache.h"
#include "csum-file.h"
#include "dir.h"
#include "lockfile.h"
#include "packfile.h"
#include "object-store.h"
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

#define MIDX_MAX_CHUNKS 1
#define MIDX_CHUNK_ALIGNMENT 4
#define MIDX_CHUNKID_PACKNAMES 0x504e414d /* "PNAM" */
#define MIDX_CHUNKLOOKUP_WIDTH (sizeof(uint32_t) + sizeof(uint64_t))

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

	hold_lock_file_for_update(&lk, midx_name, LOCK_DIE_ON_ERROR);
	f = hashfd(lk.tempfile->fd, lk.tempfile->filename.buf);
	FREE_AND_NULL(midx_name);

	cur_chunk = 0;
	num_chunks = 1;

	written = write_midx_header(f, num_chunks, packs.nr);

	chunk_ids[cur_chunk] = MIDX_CHUNKID_PACKNAMES;
	chunk_offsets[cur_chunk] = written + (num_chunks + 1) * MIDX_CHUNKLOOKUP_WIDTH;

	cur_chunk++;
	chunk_ids[cur_chunk] = 0;
	chunk_offsets[cur_chunk] = chunk_offsets[cur_chunk - 1] + packs.pack_name_concat_len;

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
	return 0;
}
