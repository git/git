#include "test-tool.h"
#include "hex.h"
#include "read-cache-ll.h"
#include "repository.h"
#include "setup.h"
#include "split-index.h"
#include "ewah/ewok.h"

static void show_bit(size_t pos, void *data UNUSED)
{
	printf(" %d", (int)pos);
}

int cmd__dump_split_index(int ac UNUSED, const char **av)
{
	struct split_index *si;
	int i;

	setup_git_directory();

	do_read_index(the_repository->index, av[1], 1);
	printf("own %s\n", oid_to_hex(&the_repository->index->oid));
	si = the_repository->index->split_index;
	if (!si) {
		printf("not a split index\n");
		return 0;
	}
	printf("base %s\n", oid_to_hex(&si->base_oid));
	for (i = 0; i < the_repository->index->cache_nr; i++) {
		struct cache_entry *ce = the_repository->index->cache[i];
		printf("%06o %s %d\t%s\n", ce->ce_mode,
		       oid_to_hex(&ce->oid), ce_stage(ce), ce->name);
	}
	printf("replacements:");
	if (si->replace_bitmap)
		ewah_each_bit(si->replace_bitmap, show_bit, NULL);
	printf("\ndeletions:");
	if (si->delete_bitmap)
		ewah_each_bit(si->delete_bitmap, show_bit, NULL);
	printf("\n");
	return 0;
}
