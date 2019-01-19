#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "lockfile.h"
#include "parse-options.h"
#include "repository.h"
#include "commit-graph.h"

static char const * const builtin_commit_graph_usage[] = {
	N_("git commit-graph [--object-dir <objdir>]"),
	N_("git commit-graph read [--object-dir <objdir>]"),
	N_("git commit-graph verify [--object-dir <objdir>]"),
	N_("git commit-graph write [--object-dir <objdir>] [--append] [--reachable|--stdin-packs|--stdin-commits]"),
	NULL
};

static const char * const builtin_commit_graph_verify_usage[] = {
	N_("git commit-graph verify [--object-dir <objdir>]"),
	NULL
};

static const char * const builtin_commit_graph_read_usage[] = {
	N_("git commit-graph read [--object-dir <objdir>]"),
	NULL
};

static const char * const builtin_commit_graph_write_usage[] = {
	N_("git commit-graph write [--object-dir <objdir>] [--append] [--reachable|--stdin-packs|--stdin-commits]"),
	NULL
};

static struct opts_commit_graph {
	const char *obj_dir;
	int reachable;
	int stdin_packs;
	int stdin_commits;
	int append;
} opts;


static int graph_verify(int argc, const char **argv)
{
	struct commit_graph *graph = NULL;
	char *graph_name;

	static struct option builtin_commit_graph_verify_options[] = {
		OPT_STRING(0, "object-dir", &opts.obj_dir,
			   N_("dir"),
			   N_("The object directory to store the graph")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL,
			     builtin_commit_graph_verify_options,
			     builtin_commit_graph_verify_usage, 0);

	if (!opts.obj_dir)
		opts.obj_dir = get_object_directory();

	graph_name = get_commit_graph_filename(opts.obj_dir);
	graph = load_commit_graph_one(graph_name);
	FREE_AND_NULL(graph_name);

	if (!graph)
		return 0;

	UNLEAK(graph);
	return verify_commit_graph(the_repository, graph);
}

static int graph_read(int argc, const char **argv)
{
	struct commit_graph *graph = NULL;
	char *graph_name;

	static struct option builtin_commit_graph_read_options[] = {
		OPT_STRING(0, "object-dir", &opts.obj_dir,
			N_("dir"),
			N_("The object directory to store the graph")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL,
			     builtin_commit_graph_read_options,
			     builtin_commit_graph_read_usage, 0);

	if (!opts.obj_dir)
		opts.obj_dir = get_object_directory();

	graph_name = get_commit_graph_filename(opts.obj_dir);
	graph = load_commit_graph_one(graph_name);

	if (!graph)
		die("graph file %s does not exist", graph_name);

	FREE_AND_NULL(graph_name);

	printf("header: %08x %d %d %d %d\n",
		ntohl(*(uint32_t*)graph->data),
		*(unsigned char*)(graph->data + 4),
		*(unsigned char*)(graph->data + 5),
		*(unsigned char*)(graph->data + 6),
		*(unsigned char*)(graph->data + 7));
	printf("num_commits: %u\n", graph->num_commits);
	printf("chunks:");

	if (graph->chunk_oid_fanout)
		printf(" oid_fanout");
	if (graph->chunk_oid_lookup)
		printf(" oid_lookup");
	if (graph->chunk_commit_data)
		printf(" commit_metadata");
	if (graph->chunk_extra_edges)
		printf(" extra_edges");
	printf("\n");

	UNLEAK(graph);

	return 0;
}

extern int read_replace_refs;

static int graph_write(int argc, const char **argv)
{
	struct string_list *pack_indexes = NULL;
	struct string_list *commit_hex = NULL;
	struct string_list lines;

	static struct option builtin_commit_graph_write_options[] = {
		OPT_STRING(0, "object-dir", &opts.obj_dir,
			N_("dir"),
			N_("The object directory to store the graph")),
		OPT_BOOL(0, "reachable", &opts.reachable,
			N_("start walk at all refs")),
		OPT_BOOL(0, "stdin-packs", &opts.stdin_packs,
			N_("scan pack-indexes listed by stdin for commits")),
		OPT_BOOL(0, "stdin-commits", &opts.stdin_commits,
			N_("start walk at commits listed by stdin")),
		OPT_BOOL(0, "append", &opts.append,
			N_("include all commits already in the commit-graph file")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL,
			     builtin_commit_graph_write_options,
			     builtin_commit_graph_write_usage, 0);

	if (opts.reachable + opts.stdin_packs + opts.stdin_commits > 1)
		die(_("use at most one of --reachable, --stdin-commits, or --stdin-packs"));
	if (!opts.obj_dir)
		opts.obj_dir = get_object_directory();

	read_replace_refs = 0;

	if (opts.reachable) {
		write_commit_graph_reachable(opts.obj_dir, opts.append, 1);
		return 0;
	}

	string_list_init(&lines, 0);
	if (opts.stdin_packs || opts.stdin_commits) {
		struct strbuf buf = STRBUF_INIT;

		while (strbuf_getline(&buf, stdin) != EOF)
			string_list_append(&lines, strbuf_detach(&buf, NULL));

		if (opts.stdin_packs)
			pack_indexes = &lines;
		if (opts.stdin_commits)
			commit_hex = &lines;

		UNLEAK(buf);
	}

	write_commit_graph(opts.obj_dir,
			   pack_indexes,
			   commit_hex,
			   opts.append,
			   1);

	UNLEAK(lines);
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
		if (!strcmp(argv[0], "read"))
			return graph_read(argc, argv);
		if (!strcmp(argv[0], "verify"))
			return graph_verify(argc, argv);
		if (!strcmp(argv[0], "write"))
			return graph_write(argc, argv);
	}

	usage_with_options(builtin_commit_graph_usage,
			   builtin_commit_graph_options);
}
