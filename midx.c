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
	uint32_t nr;
	uint32_t alloc_list;
};

static void add_pack_to_midx(const char *full_path, size_t full_path_len,
			     const char *file_name, void *data)
{
	struct pack_list *packs = (struct pack_list *)data;

	if (ends_with(file_name, ".idx")) {
		ALLOC_GROW(packs->list, packs->nr + 1, packs->alloc_list);

		packs->list[packs->nr] = add_packed_git(full_path,
							full_path_len,
							0);
		if (!packs->list[packs->nr]) {
			warning(_("failed to add packfile '%s'"),
				full_path);
			return;
		}

		packs->nr++;
	}
}

int write_midx_file(const char *object_dir)
{
	unsigned char num_chunks = 0;
	char *midx_name;
	uint32_t i;
	struct hashfile *f = NULL;
	struct lock_file lk;
	struct pack_list packs;

	midx_name = get_midx_filename(object_dir);
	if (safe_create_leading_directories(midx_name)) {
		UNLEAK(midx_name);
		die_errno(_("unable to create leading directories of %s"),
			  midx_name);
	}

	packs.nr = 0;
	packs.alloc_list = 16;
	packs.list = NULL;
	ALLOC_ARRAY(packs.list, packs.alloc_list);

	for_each_file_in_pack_dir(object_dir, add_pack_to_midx, &packs);

	hold_lock_file_for_update(&lk, midx_name, LOCK_DIE_ON_ERROR);
	f = hashfd(lk.tempfile->fd, lk.tempfile->filename.buf);
	FREE_AND_NULL(midx_name);

	write_midx_header(f, num_chunks, packs.nr);

	finalize_hashfile(f, NULL, CSUM_FSYNC | CSUM_HASH_IN_STREAM);
	commit_lock_file(&lk);

	for (i = 0; i < packs.nr; i++) {
		if (packs.list[i]) {
			close_pack(packs.list[i]);
			free(packs.list[i]);
		}
	}

	free(packs.list);
	return 0;
}
