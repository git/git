#include "test-tool.h"
#include "read-cache-ll.h"
#include "repository.h"
#include "setup.h"

int cmd__dump_fsmonitor(int ac UNUSED, const char **av UNUSED)
{
	struct index_state *istate = the_repository->index;
	int i;

	setup_git_directory();
	if (do_read_index(istate, the_repository->index_file, 0) < 0)
		die("unable to read index file");
	if (!istate->fsmonitor_last_update) {
		printf("no fsmonitor\n");
		return 0;
	}
	printf("fsmonitor last update %s\n", istate->fsmonitor_last_update);

	for (i = 0; i < istate->cache_nr; i++)
		printf((istate->cache[i]->ce_flags & CE_FSMONITOR_VALID) ? "+" : "-");

	return 0;
}
