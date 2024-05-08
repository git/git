#include "test-tool.h"
#include "config.h"
#include "read-cache-ll.h"
#include "repository.h"
#include "setup.h"

int cmd__read_cache(int argc, const char **argv)
{
	int i, cnt = 1;
	const char *name = NULL;

	initialize_repository(the_repository);

	if (argc > 1 && skip_prefix(argv[1], "--print-and-refresh=", &name)) {
		argc--;
		argv++;
	}

	if (argc == 2)
		cnt = strtol(argv[1], NULL, 0);
	setup_git_directory();
	git_config(git_default_config, NULL);

	for (i = 0; i < cnt; i++) {
		repo_read_index(the_repository);
		if (name) {
			int pos;

			refresh_index(the_repository->index, REFRESH_QUIET,
				      NULL, NULL, NULL);
			pos = index_name_pos(the_repository->index, name, strlen(name));
			if (pos < 0)
				die("%s not in index", name);
			printf("%s is%s up to date\n", name,
			       ce_uptodate(the_repository->index->cache[pos]) ? "" : " not");
			write_file(name, "%d\n", i);
		}
		discard_index(the_repository->index);
	}
	return 0;
}
