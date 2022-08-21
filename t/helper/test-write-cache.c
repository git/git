#include "test-tool.h"
#include "cache.h"
#include "lockfile.h"

int cmd__write_cache(int argc, const char **argv)
{
	struct lock_file index_lock = LOCK_INIT;
	int i, cnt = 1;
	if (argc == 2)
		cnt = strtol(argv[1], NULL, 0);
	setup_git_directory();
	read_cache();
	for (i = 0; i < cnt; i++) {
		hold_locked_index(&index_lock, LOCK_DIE_ON_ERROR);
		if (write_locked_index(&the_index, &index_lock, COMMIT_LOCK))
			die("unable to write index file");
	}

	return 0;
}
