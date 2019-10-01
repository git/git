#include "test-tool.h"
#include "cache.h"
#include "config.h"

int cmd__read_cache(int argc, const char **argv)
{
	int i, cnt = 1;
	const char *name = NULL;

	if (argc > 1 && skip_prefix(argv[1], "--print-and-refresh=", &name)) {
		argc--;
		argv++;
	}

	if (argc == 2)
		cnt = strtol(argv[1], NULL, 0);
	setup_git_directory();
	git_config(git_default_config, NULL);
	for (i = 0; i < cnt; i++) {
		read_cache();
		if (name) {
			int pos;

			refresh_index(&the_index, REFRESH_QUIET,
				      NULL, NULL, NULL);
			pos = index_name_pos(&the_index, name, strlen(name));
			if (pos < 0)
				die("%s not in index", name);
			printf("%s is%s up to date\n", name,
			       ce_uptodate(the_index.cache[pos]) ? "" : " not");
			write_file(name, "%d\n", i);
		}
		discard_cache();
	}
	return 0;
}
