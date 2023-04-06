#include "test-tool.h"
#include "cache.h"
#include "hex.h"
#include "midx.h"
#include "repository.h"
#include "object-store.h"
#include "pack-bitmap.h"
#include "setup.h"

static int read_midx_file(const char *object_dir, int show_objects)
{
	uint32_t i;
	struct multi_pack_index *m;

	setup_git_directory();
	m = load_multi_pack_index(object_dir, 1);

	if (!m)
		return 1;

	printf("header: %08x %d %d %d %d\n",
	       m->signature,
	       m->version,
	       m->hash_len,
	       m->num_chunks,
	       m->num_packs);

	printf("chunks:");

	if (m->chunk_pack_names)
		printf(" pack-names");
	if (m->chunk_oid_fanout)
		printf(" oid-fanout");
	if (m->chunk_oid_lookup)
		printf(" oid-lookup");
	if (m->chunk_object_offsets)
		printf(" object-offsets");
	if (m->chunk_large_offsets)
		printf(" large-offsets");

	printf("\nnum_objects: %d\n", m->num_objects);

	printf("packs:\n");
	for (i = 0; i < m->num_packs; i++)
		printf("%s\n", m->pack_names[i]);

	printf("object-dir: %s\n", m->object_dir);

	if (show_objects) {
		struct object_id oid;
		struct pack_entry e;

		for (i = 0; i < m->num_objects; i++) {
			nth_midxed_object_oid(&oid, m, i);
			fill_midx_entry(the_repository, &oid, &e, m);

			printf("%s %"PRIu64"\t%s\n",
			       oid_to_hex(&oid), e.offset, e.p->pack_name);
		}
	}

	close_midx(m);

	return 0;
}

static int read_midx_checksum(const char *object_dir)
{
	struct multi_pack_index *m;

	setup_git_directory();
	m = load_multi_pack_index(object_dir, 1);
	if (!m)
		return 1;
	printf("%s\n", hash_to_hex(get_midx_checksum(m)));
	return 0;
}

static int read_midx_preferred_pack(const char *object_dir)
{
	struct multi_pack_index *midx = NULL;
	struct bitmap_index *bitmap = NULL;

	setup_git_directory();

	midx = load_multi_pack_index(object_dir, 1);
	if (!midx)
		return 1;

	bitmap = prepare_bitmap_git(the_repository);
	if (!bitmap)
		return 1;
	if (!bitmap_is_midx(bitmap)) {
		free_bitmap_index(bitmap);
		return 1;
	}

	printf("%s\n", midx->pack_names[midx_preferred_pack(bitmap)]);
	free_bitmap_index(bitmap);
	return 0;
}

int cmd__read_midx(int argc, const char **argv)
{
	if (!(argc == 2 || argc == 3))
		usage("read-midx [--show-objects|--checksum|--preferred-pack] <object-dir>");

	if (!strcmp(argv[1], "--show-objects"))
		return read_midx_file(argv[2], 1);
	else if (!strcmp(argv[1], "--checksum"))
		return read_midx_checksum(argv[2]);
	else if (!strcmp(argv[1], "--preferred-pack"))
		return read_midx_preferred_pack(argv[2]);
	return read_midx_file(argv[1], 0);
}
