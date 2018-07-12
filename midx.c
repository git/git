#include "cache.h"
#include "csum-file.h"
#include "lockfile.h"
#include "midx.h"

#define MIDX_SIGNATURE 0x4d494458 /* "MIDX" */
#define MIDX_VERSION 1
#define MIDX_HASH_VERSION 1
#define MIDX_HEADER_SIZE 12

static char *get_midx_filename(const char *object_dir)
{
	return xstrfmt("%s/pack/multi-pack-index", object_dir);
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

int write_midx_file(const char *object_dir)
{
	unsigned char num_chunks = 0;
	char *midx_name;
	struct hashfile *f = NULL;
	struct lock_file lk;

	midx_name = get_midx_filename(object_dir);
	if (safe_create_leading_directories(midx_name)) {
		UNLEAK(midx_name);
		die_errno(_("unable to create leading directories of %s"),
			  midx_name);
	}

	hold_lock_file_for_update(&lk, midx_name, LOCK_DIE_ON_ERROR);
	f = hashfd(lk.tempfile->fd, lk.tempfile->filename.buf);
	FREE_AND_NULL(midx_name);

	write_midx_header(f, num_chunks, 0);

	finalize_hashfile(f, NULL, CSUM_FSYNC | CSUM_HASH_IN_STREAM);
	commit_lock_file(&lk);

	return 0;
}
