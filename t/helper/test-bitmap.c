#include "test-tool.h"
#include "cache.h"
#include "pack-bitmap.h"

static int bitmap_list_commits(void)
{
	return test_bitmap_commits(the_repository);
}

int cmd__bitmap(int argc, const char **argv)
{
	setup_git_directory();

	if (argc != 2)
		goto usage;

	if (!strcmp(argv[1], "list-commits"))
		return bitmap_list_commits();

usage:
	usage("\ttest-tool bitmap list-commits");

	return -1;
}
