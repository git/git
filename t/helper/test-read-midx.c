#include "test-tool.h"
#include "cache.h"
#include "midx.h"
#include "repository.h"
#include "object-store.h"

static int read_midx_file(const char *object_dir)
{
	struct multi_pack_index *m = load_multi_pack_index(object_dir);

	if (!m)
		return 1;

	printf("header: %08x %d %d %d\n",
	       m->signature,
	       m->version,
	       m->num_chunks,
	       m->num_packs);

	printf("chunks:");

	if (m->chunk_pack_names)
		printf(" pack-names");

	printf("\n");

	printf("object-dir: %s\n", m->object_dir);

	return 0;
}

int cmd__read_midx(int argc, const char **argv)
{
	if (argc != 2)
		usage("read-midx <object-dir>");

	return read_midx_file(argv[1]);
}
