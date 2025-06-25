#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "git-compat-util.h"
#include "pack-bitmap.h"
#include "setup.h"

static int bitmap_list_commits(void)
{
	return test_bitmap_commits(the_repository);
}

static int bitmap_list_commits_with_offset(void)
{
	return test_bitmap_commits_with_offset(the_repository);
}

static int bitmap_dump_hashes(void)
{
	return test_bitmap_hashes(the_repository);
}

static int bitmap_dump_pseudo_merges(void)
{
	return test_bitmap_pseudo_merges(the_repository);
}

static int bitmap_dump_pseudo_merge_commits(uint32_t n)
{
	return test_bitmap_pseudo_merge_commits(the_repository, n);
}

static int bitmap_dump_pseudo_merge_objects(uint32_t n)
{
	return test_bitmap_pseudo_merge_objects(the_repository, n);
}

int cmd__bitmap(int argc, const char **argv)
{
	setup_git_directory();

	if (argc == 2 && !strcmp(argv[1], "list-commits"))
		return bitmap_list_commits();
	if (argc == 2 && !strcmp(argv[1], "list-commits-with-offset"))
		return bitmap_list_commits_with_offset();
	if (argc == 2 && !strcmp(argv[1], "dump-hashes"))
		return bitmap_dump_hashes();
	if (argc == 2 && !strcmp(argv[1], "dump-pseudo-merges"))
		return bitmap_dump_pseudo_merges();
	if (argc == 3 && !strcmp(argv[1], "dump-pseudo-merge-commits"))
		return bitmap_dump_pseudo_merge_commits(atoi(argv[2]));
	if (argc == 3 && !strcmp(argv[1], "dump-pseudo-merge-objects"))
		return bitmap_dump_pseudo_merge_objects(atoi(argv[2]));

	usage("\ttest-tool bitmap list-commits\n"
	      "\ttest-tool bitmap list-commits-with-offset\n"
	      "\ttest-tool bitmap dump-hashes\n"
	      "\ttest-tool bitmap dump-pseudo-merges\n"
	      "\ttest-tool bitmap dump-pseudo-merge-commits <n>\n"
	      "\ttest-tool bitmap dump-pseudo-merge-objects <n>");

	return -1;
}
