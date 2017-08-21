#include "cache.h"
#include "lockfile.h"

static struct lock_file index_lock;

int cmd_main(int argc, const char **argv)
{
	int i, cnt = 1, lockfd;
	if (argc == 2)
		cnt = strtol(argv[1], NULL, 0);
	setup_git_directory();
	read_cache();
	for (i = 0; i < cnt; i++) {
		lockfd = hold_locked_index(&index_lock, LOCK_DIE_ON_ERROR);
		if (0 <= lockfd) {
			write_locked_index(&the_index, &index_lock, COMMIT_LOCK);
		} else {
			rollback_lock_file(&index_lock);
		}
	}

	return 0;
}
