#include "test-tool.h"
#include "lockfile.h"
#include "read-cache-ll.h"
#include "repository.h"
#include "setup.h"

int cmd__write_cache(int argc, const char **argv)
{
	struct lock_file index_lock = LOCK_INIT;
	int i, cnt = 1;
	if (argc == 2)
		cnt = strtol(argv[1], NULL, 0);
	setup_git_directory();
	repo_read_index(the_repository);
	for (i = 0; i < cnt; i++) {
		repo_hold_locked_index(the_repository, &index_lock,
				       LOCK_DIE_ON_ERROR);
		if (write_locked_index(the_repository->index, &index_lock, COMMIT_LOCK))
			die("unable to write index file");
	}

	return 0;
}
