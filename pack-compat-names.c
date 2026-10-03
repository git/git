#include "git-compat-util.h"
#include "strbuf.h"
#include "gettext.h"
#include "pack-compat-names.h"
#include "packfile.h"
#include "chunk-format.h"
#include "hash.h"
#include "hex.h"
#include "object.h"
#include "odb.h"
#include "repository.h"

static char *compat_names_filename(struct packed_git *p)
{
	size_t len;

	if (!strip_suffix(p->pack_name, ".pack", &len))
		BUG("pack_name does not end in .pack");
	return xstrfmt("%.*s.compat", (int)len, p->pack_name);
}

void close_pack_compat_names(struct packed_git *p)
{
	if (!p->compat_names_map)
		return;
	munmap((void *)p->compat_names_map, p->compat_names_size);
	p->compat_names_map = NULL;
	p->compat_names_size = 0;
	p->compat_names_by_index = NULL;
	p->compat_names_sorted = NULL;
	p->compat_names_order = NULL;
	p->compat_names_nr = 0;
}

/*
 * Map the compatibility object names of the objects of the pack to the
 * positions of those objects in the pack index, and back.
 */
static int load_pack_compat_names(struct packed_git *p)
{
	const struct git_hash_algo *compat = p->repo->compat_hash_algo;
	struct compat_header *hdr;
	struct stat st;
	char *compat_name;
	void *data;
	size_t compat_size;
	int fd;

	if (p->compat_names_map)
		return 0;
	if (!compat || open_pack_index(p))
		return -1;

	compat_name = compat_names_filename(p);
	fd = git_open(compat_name);
	if (fd < 0 || fstat(fd, &st)) {
		if (fd >= 0)
			close(fd);
		free(compat_name);
		return -1;
	}
	compat_size = xsize_t(st.st_size);
	if (compat_size < COMPAT_HEADER_SIZE + p->repo->hash_algo->rawsz) {
		close(fd);
		error(_("compatibility names file %s is too small"),
		      compat_name);
		free(compat_name);
		return -1;
	}
	data = xmmap(NULL, compat_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	free(compat_name);

	hdr = data;
	if (ntohl(hdr->signature) != COMPAT_SIGNATURE ||
	    ntohl(hdr->version) != COMPAT_VERSION ||
	    ntohl(hdr->hash_version) != oid_version(p->repo->hash_algo) ||
	    ntohl(hdr->nr_objects) != p->num_objects ||
	    ntohl(hdr->rawsz) != compat->rawsz) {
		munmap(data, compat_size);
		return error(_("compatibility names file does not match its pack"));
	}

	/*
	 * The pack checksum of the packfile this file belongs to, followed
	 * by the checksum of this file itself.
	 */
	if (compat_size != COMPAT_HEADER_SIZE + st_mult(2, p->repo->hash_algo->rawsz) +
	    st_mult(p->num_objects,
		    st_add(st_mult(2, compat->rawsz), sizeof(uint32_t)))) {
		munmap(data, compat_size);
		return error(_("compatibility names file is corrupt"));
	}

	p->compat_names_map = data;
	p->compat_names_size = compat_size;
	p->compat_names_nr = p->num_objects;
	p->compat_names_by_index = (const unsigned char *)data + COMPAT_HEADER_SIZE;
	p->compat_names_sorted = p->compat_names_by_index +
		st_mult(p->num_objects, compat->rawsz);
	p->compat_names_order = (const uint32_t *)(p->compat_names_sorted +
		st_mult(p->num_objects, compat->rawsz));
	return 0;
}

int packed_object_compat_oid(struct packed_git *p, uint32_t nr,
			     struct object_id *compat_oid)
{
	const struct git_hash_algo *compat = p->repo->compat_hash_algo;

	if (!compat || load_pack_compat_names(p) || nr >= p->compat_names_nr)
		return -1;
	oidread(compat_oid,
		p->compat_names_by_index + st_mult(nr, compat->rawsz), compat);
	return 0;
}

/*
 * Find the position in the pack index of the object named `compat_oid` in the
 * compatibility object format.
 */
int packed_object_by_compat_oid(struct packed_git *p,
				const struct object_id *compat_oid,
				uint32_t *nr)
{
	const struct git_hash_algo *compat = p->repo->compat_hash_algo;
	uint32_t lo = 0, hi;

	if (!compat || load_pack_compat_names(p))
		return -1;

	hi = p->compat_names_nr;
	while (lo < hi) {
		uint32_t mi = lo + (hi - lo) / 2;
		int cmp = memcmp(p->compat_names_sorted + st_mult(mi, compat->rawsz),
				 compat_oid->hash, compat->rawsz);

		if (!cmp) {
			*nr = ntohl(p->compat_names_order[mi]);
			return 0;
		}
		if (cmp > 0)
			hi = mi;
		else
			lo = mi + 1;
	}
	return -1;
}