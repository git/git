#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "lockfile.h"
#include "parse-options.h"
#include "commit-graph.h"

static char const * const builtin_commit_graph_usage[] = {
	N_("git commit-graph [--object-dir <objdir>]"),
	N_("git commit-graph write [--object-dir <objdir>]"),
	NULL
};

static const char * const builtin_commit_graph_write_usage[] = {
	N_("git commit-graph write [--object-dir <objdir>]"),
	NULL
};

static struct opts_commit_graph {
	const char *obj_dir;
} opts;

static int graph_write(int argc, const char **argv)
{
	static struct option builtin_commit_graph_write_options[] = {
		OPT_STRING(0, "object-dir", &opts.obj_dir,
			N_("dir"),
			N_("The object directory to store the graph")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL,
			     builtin_commit_graph_write_options,
			     builtin_commit_graph_write_usage, 0);

	if (!opts.obj_dir)
		opts.obj_dir = get_object_directory();

	write_commit_graph(opts.obj_dir);
	return 0;
}

int cmd_commit_graph(int argc, const char **argv, const char *prefix)
{
	static struct option builtin_commit_graph_options[] = {
		OPT_STRING(0, "object-dir", &opts.obj_dir,
			N_("dir"),
			N_("The object directory to store the graph")),
		OPT_END(),
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(builtin_commit_graph_usage,
				   builtin_commit_graph_options);

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix,
			     builtin_commit_graph_options,
			     builtin_commit_graph_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc > 0) {
		if (!strcmp(argv[0], "write"))
			return graph_write(argc, argv);
	}

	usage_with_options(builtin_commit_graph_usage,
			   builtin_commit_graph_options);
}
