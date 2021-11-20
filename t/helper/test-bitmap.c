#include "test-tool.h"
#include "cache.h"
#include "pack-bitmap.h"

static int bitmap_list_commits(void)
{
	return test_bitmap_commits(the_repository);
}

static int bitmap_dump_hashes(void)
{
	return test_bitmap_hashes(the_repository);
}

int cmd__bitmap(int argc, const char **argv)
{
	setup_git_directory();

	if (argc != 2)
		goto usage;

	if (!strcmp(argv[1], "list-commits"))
		return bitmap_list_commits();
	if (!strcmp(argv[1], "dump-hashes"))
		return bitmap_dump_hashes();

usage:
	usage("\ttest-tool bitmap list-commits\n"
	      "\ttest-tool bitmap dump-hashes");

	return -1;
}
