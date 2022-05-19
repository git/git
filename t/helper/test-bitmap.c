#include "test-tool.h"
#include "cache.h"
#include "pack-bitmap.h"

static int bitmap_list_cummits(void)
{
	return test_bitmap_cummits(the_repository);
}

static int bitmap_dump_hashes(void)
{
	return test_bitmap_hashes(the_repository);
}

int cmd__bitmap(int argc, const char **argv)
{
	setup_but_directory();

	if (argc != 2)
		goto usage;

	if (!strcmp(argv[1], "list-cummits"))
		return bitmap_list_cummits();
	if (!strcmp(argv[1], "dump-hashes"))
		return bitmap_dump_hashes();

usage:
	usage("\ttest-tool bitmap list-cummits\n"
	      "\ttest-tool bitmap dump-hashes");

	return -1;
}
