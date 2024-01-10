#include "test-tool.h"
#include "parse-options.h"
#include "pathspec.h"

int cmd__parse_pathspec_file(int argc, const char **argv)
{
	struct pathspec pathspec;
	char *pathspec_from_file = NULL;
	int pathspec_file_nul = 0, i;

	static const char *const usage[] = {
		"test-tool parse-pathspec-file --pathspec-from-file [--pathspec-file-nul]",
		NULL
	};

	struct option options[] = {
		OPT_PATHSPEC_FROM_FILE(&pathspec_from_file),
		OPT_PATHSPEC_FILE_NUL(&pathspec_file_nul),
		OPT_END()
	};

	parse_options(argc, argv, NULL, options, usage, 0);

	parse_pathspec_file(&pathspec, 0, 0, NULL, pathspec_from_file,
			    pathspec_file_nul);

	for (i = 0; i < pathspec.nr; i++)
		printf("%s\n", pathspec.items[i].original);

	clear_pathspec(&pathspec);
	free(pathspec_from_file);
	return 0;
}
