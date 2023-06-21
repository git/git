#include "git-compat-util.h"
#include "gettext.h"
#include "pack-mtimes.h"
#include "object-file.h"
#include "object-store.h"
#include "packfile.h"

static char *pack_mtimes_filename(struct packed_git *p)
{
	size_t len;
	if (!strip_suffix(p->pack_name, ".pack", &len))
		BUG("pack_name does not end in .pack");
	return xstrfmt("%.*s.mtimes", (int)len, p->pack_name);
}

#define MTIMES_HEADER_SIZE (12)

struct mtimes_header {
	uint32_t signature;
	uint32_t version;
	uint32_t hash_id;
};

static int load_pack_mtimes_file(char *mtimes_file,
				 uint32_t num_objects,
				 const uint32_t **data_p, size_t *len_p)
{
	int fd, ret = 0;
	struct stat st;
	uint32_t *data = NULL;
	size_t mtimes_size, expected_size;
	struct mtimes_header header;

	fd = git_open(mtimes_file);

	if (fd < 0) {
		ret = -1;
		goto cleanup;
	}
	if (fstat(fd, &st)) {
		ret = error_errno(_("failed to read %s"), mtimes_file);
		goto cleanup;
	}

	mtimes_size = xsize_t(st.st_size);

	if (mtimes_size < MTIMES_HEADER_SIZE) {
		ret = error(_("mtimes file %s is too small"), mtimes_file);
		goto cleanup;
	}

	data = xmmap(NULL, mtimes_size, PROT_READ, MAP_PRIVATE, fd, 0);

	header.signature = ntohl(data[0]);
	header.version = ntohl(data[1]);
	header.hash_id = ntohl(data[2]);

	if (header.signature != MTIMES_SIGNATURE) {
		ret = error(_("mtimes file %s has unknown signature"), mtimes_file);
		goto cleanup;
	}

	if (header.version != 1) {
		ret = error(_("mtimes file %s has unsupported version %"PRIu32),
			    mtimes_file, header.version);
		goto cleanup;
	}

	if (!(header.hash_id == 1 || header.hash_id == 2)) {
		ret = error(_("mtimes file %s has unsupported hash id %"PRIu32),
			    mtimes_file, header.hash_id);
		goto cleanup;
	}


	expected_size = MTIMES_HEADER_SIZE;
	expected_size = st_add(expected_size, st_mult(sizeof(uint32_t), num_objects));
	expected_size = st_add(expected_size, 2 * (header.hash_id == 1 ? GIT_SHA1_RAWSZ : GIT_SHA256_RAWSZ));

	if (mtimes_size != expected_size) {
		ret = error(_("mtimes file %s is corrupt"), mtimes_file);
		goto cleanup;
	}

cleanup:
	if (ret) {
		if (data)
			munmap(data, mtimes_size);
	} else {
		*len_p = mtimes_size;
		*data_p = data;
	}

	if (fd >= 0)
		close(fd);
	return ret;
}

int load_pack_mtimes(struct packed_git *p)
{
	char *mtimes_name = NULL;
	int ret = 0;

	if (!p->is_cruft)
		return ret; /* not a cruft pack */
	if (p->mtimes_map)
		return ret; /* already loaded */

	ret = open_pack_index(p);
	if (ret < 0)
		goto cleanup;

	mtimes_name = pack_mtimes_filename(p);
	ret = load_pack_mtimes_file(mtimes_name,
				    p->num_objects,
				    &p->mtimes_map,
				    &p->mtimes_size);
cleanup:
	free(mtimes_name);
	return ret;
}

uint32_t nth_packed_mtime(struct packed_git *p, uint32_t pos)
{
	if (!p->mtimes_map)
		BUG("pack .mtimes file not loaded for %s", p->pack_name);
	if (p->num_objects <= pos)
		BUG("pack .mtimes out-of-bounds (%"PRIu32" vs %"PRIu32")",
		    pos, p->num_objects);

	return get_be32(p->mtimes_map + pos + 3);
}
