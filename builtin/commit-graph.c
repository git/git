#include "builtin.h"
#include "config.h"
#include "dir.h"
#include "lockfile.h"
#include "parse-options.h"
#include "repository.h"
#include "commit-graph.h"
#include "object-store.h"

static char const * const builtin_commit_graph_usage[] = {
	N_("git commit-graph verify [--object-dir <objdir>] [--shallow] [--[no-]progress]"),
	N_("git commit-graph write [--object-dir <objdir>] "
	   "[--split[=<strategy>]] "
	   "[--input=<reachable|stdin-packs|stdin-commits|append|graphed>] "
	   "[--[no-]progress] <split options>"),
	NULL
};

static const char * const builtin_commit_graph_verify_usage[] = {
	N_("git commit-graph verify [--object-dir <objdir>] [--shallow] [--[no-]progress]"),
	NULL
};

static const char * const builtin_commit_graph_write_usage[] = {
	N_("git commit-graph write [--object-dir <objdir>] "
	   "[--split[=<strategy>]] "
	   "[--input=<reachable|stdin-packs|stdin-commits|append|graphed>] "
	   "[--[no-]progress] <split options>"),
	NULL
};

enum commit_graph_input {
	COMMIT_GRAPH_INPUT_REACHABLE     = (1 << 1),
	COMMIT_GRAPH_INPUT_STDIN_PACKS   = (1 << 2),
	COMMIT_GRAPH_INPUT_STDIN_COMMITS = (1 << 3),
	COMMIT_GRAPH_INPUT_APPEND        = (1 << 4),
	COMMIT_GRAPH_INPUT_GRAPHED       = (1 << 5)
};

static struct opts_commit_graph {
	const char *obj_dir;
	enum commit_graph_input input;
	int split;
	int shallow;
	int progress;
} opts;

static struct object_directory *find_odb(struct repository *r,
					 const char *obj_dir)
{
	struct object_directory *odb;
	char *obj_dir_real = real_pathdup(obj_dir, 1);
	struct strbuf odb_path_real = STRBUF_INIT;

	prepare_alt_odb(r);
	for (odb = r->objects->odb; odb; odb = odb->next) {
		strbuf_realpath(&odb_path_real, odb->path, 1);
		if (!strcmp(obj_dir_real, odb_path_real.buf))
			break;
	}

	free(obj_dir_real);
	strbuf_release(&odb_path_real);

	if (!odb)
		die(_("could not find object directory matching %s"), obj_dir);
	return odb;
}

static int option_parse_input(const struct option *opt, const char *arg,
			      int unset)
{
	enum commit_graph_input *to = opt->value;
	if (unset || !strcmp(arg, "packs")) {
		*to = 0;
		return 0;
	}

	if (!strcmp(arg, "reachable"))
		*to |= COMMIT_GRAPH_INPUT_REACHABLE;
	else if (!strcmp(arg, "stdin-packs"))
		*to |= COMMIT_GRAPH_INPUT_STDIN_PACKS;
	else if (!strcmp(arg, "stdin-commits"))
		*to |= COMMIT_GRAPH_INPUT_STDIN_COMMITS;
	else if (!strcmp(arg, "append"))
		*to |= COMMIT_GRAPH_INPUT_APPEND;
	else if (!strcmp(arg, "graphed"))
		*to |= (COMMIT_GRAPH_INPUT_APPEND | COMMIT_GRAPH_INPUT_GRAPHED);
	else
		die(_("unrecognized --input source, %s"), arg);
	return 0;
}

static int graph_verify(int argc, const char **argv)
{
	struct commit_graph *graph = NULL;
	struct object_directory *odb = NULL;
	char *graph_name;
	int open_ok;
	int fd;
	struct stat st;
	int flags = 0;

	static struct option builtin_commit_graph_verify_options[] = {
		OPT_STRING(0, "object-dir", &opts.obj_dir,
			   N_("dir"),
			   N_("The object directory to store the graph")),
		OPT_BOOL(0, "shallow", &opts.shallow,
			 N_("if the commit-graph is split, only verify the tip file")),
		OPT_BOOL(0, "progress", &opts.progress, N_("force progress reporting")),
		OPT_END(),
	};

	trace2_cmd_mode("verify");

	opts.progress = isatty(2);
	argc = parse_options(argc, argv, NULL,
			     builtin_commit_graph_verify_options,
			     builtin_commit_graph_verify_usage, 0);

	if (!opts.obj_dir)
		opts.obj_dir = get_object_directory();
	if (opts.shallow)
		flags |= COMMIT_GRAPH_VERIFY_SHALLOW;
	if (opts.progress)
		flags |= COMMIT_GRAPH_WRITE_PROGRESS;

	odb = find_odb(the_repository, opts.obj_dir);
	graph_name = get_commit_graph_filename(odb);
	open_ok = open_commit_graph(graph_name, &fd, &st);
	if (!open_ok && errno != ENOENT)
		die_errno(_("Could not open commit-graph '%s'"), graph_name);

	FREE_AND_NULL(graph_name);

	if (open_ok)
		graph = load_commit_graph_one_fd_st(fd, &st, odb);
	else
		graph = read_commit_graph_one(the_repository, odb);

	/* Return failure if open_ok predicted success */
	if (!graph)
		return !!open_ok;

	UNLEAK(graph);
	return verify_commit_graph(the_repository, graph, flags);
}

extern int read_replace_refs;
static struct split_commit_graph_opts split_opts;

static int write_option_parse_split(const struct option *opt, const char *arg,
				    int unset)
{
	enum commit_graph_split_flags *flags = opt->value;

	opts.split = 1;
	if (!arg) {
		*flags = COMMIT_GRAPH_SPLIT_MERGE_AUTO;
		return 0;
	}

	if (!strcmp(arg, "merge-all"))
		*flags = COMMIT_GRAPH_SPLIT_MERGE_REQUIRED;
	else if (!strcmp(arg, "no-merge"))
		*flags = COMMIT_GRAPH_SPLIT_MERGE_PROHIBITED;
	else
		die(_("unrecognized --split argument, %s"), arg);

	return 0;
}

static int graph_write(int argc, const char **argv)
{
	struct string_list *pack_indexes = NULL;
	struct string_list *commit_hex = NULL;
	struct object_directory *odb = NULL;
	struct string_list lines;
	int result = 0;
	enum commit_graph_write_flags flags = 0;

	static struct option builtin_commit_graph_write_options[] = {
		OPT_STRING(0, "object-dir", &opts.obj_dir,
			N_("dir"),
			N_("The object directory to store the graph")),
		OPT_CALLBACK(0, "input", &opts.input, NULL,
			N_("include commits from this source in the graph"),
			option_parse_input),
		OPT_BIT(0, "reachable", &opts.input,
			N_("start walk at all refs"),
			COMMIT_GRAPH_INPUT_REACHABLE),
		OPT_BIT(0, "stdin-packs", &opts.input,
			N_("scan pack-indexes listed by stdin for commits"),
			COMMIT_GRAPH_INPUT_STDIN_PACKS),
		OPT_BIT(0, "stdin-commits", &opts.input,
			N_("start walk at commits listed by stdin"),
			COMMIT_GRAPH_INPUT_STDIN_COMMITS),
		OPT_BIT(0, "append", &opts.input,
			N_("include all commits already in the commit-graph file"),
			COMMIT_GRAPH_INPUT_APPEND),
		OPT_BOOL(0, "progress", &opts.progress, N_("force progress reporting")),
		OPT_CALLBACK_F(0, "split", &split_opts.flags, NULL,
			N_("allow writing an incremental commit-graph file"),
			PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			write_option_parse_split),
		OPT_INTEGER(0, "max-commits", &split_opts.max_commits,
			N_("maximum number of commits in a non-base split commit-graph")),
		OPT_INTEGER(0, "size-multiple", &split_opts.size_multiple,
			N_("maximum ratio between two levels of a split commit-graph")),
		OPT_EXPIRY_DATE(0, "expire-time", &split_opts.expire_time,
			N_("maximum number of commits in a non-base split commit-graph")),
		OPT_END(),
	};

	opts.progress = isatty(2);
	split_opts.size_multiple = 2;
	split_opts.max_commits = 0;
	split_opts.expire_time = 0;

	trace2_cmd_mode("write");

	argc = parse_options(argc, argv, NULL,
			     builtin_commit_graph_write_options,
			     builtin_commit_graph_write_usage, 0);

	if ((!!(opts.input & COMMIT_GRAPH_INPUT_REACHABLE) +
	     !!(opts.input & COMMIT_GRAPH_INPUT_STDIN_PACKS) +
	     !!(opts.input & COMMIT_GRAPH_INPUT_STDIN_COMMITS)) > 1)
		die(_("use at most one of --input=reachable, --input=stdin-commits, or --input=stdin-packs"));
	if (!opts.obj_dir)
		opts.obj_dir = get_object_directory();
	if (opts.input & COMMIT_GRAPH_INPUT_APPEND)
		flags |= COMMIT_GRAPH_WRITE_APPEND;
	if (opts.input & COMMIT_GRAPH_INPUT_GRAPHED)
		flags |= COMMIT_GRAPH_WRITE_NO_INPUT;
	if (opts.split)
		flags |= COMMIT_GRAPH_WRITE_SPLIT;
	if (opts.progress)
		flags |= COMMIT_GRAPH_WRITE_PROGRESS;

	read_replace_refs = 0;
	odb = find_odb(the_repository, opts.obj_dir);

	if (opts.input & COMMIT_GRAPH_INPUT_REACHABLE) {
		if (write_commit_graph_reachable(odb, flags, &split_opts))
			return 1;
		return 0;
	}

	string_list_init(&lines, 0);
	if (opts.input & (COMMIT_GRAPH_INPUT_STDIN_PACKS | COMMIT_GRAPH_INPUT_STDIN_COMMITS)) {
		struct strbuf buf = STRBUF_INIT;

		while (strbuf_getline(&buf, stdin) != EOF)
			string_list_append(&lines, strbuf_detach(&buf, NULL));

		if (opts.input & COMMIT_GRAPH_INPUT_STDIN_PACKS)
			pack_indexes = &lines;
		if (opts.input & COMMIT_GRAPH_INPUT_STDIN_COMMITS) {
			commit_hex = &lines;
			flags |= COMMIT_GRAPH_WRITE_CHECK_OIDS;
		}

		UNLEAK(buf);
	}

	if (write_commit_graph(odb,
			       pack_indexes,
			       commit_hex,
			       flags,
			       &split_opts))
		result = 1;

	UNLEAK(lines);
	return result;
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

	save_commit_buffer = 0;

	if (argc > 0) {
		if (!strcmp(argv[0], "verify"))
			return graph_verify(argc, argv);
		if (!strcmp(argv[0], "write"))
			return graph_write(argc, argv);
	}

	usage_with_options(builtin_commit_graph_usage,
			   builtin_commit_graph_options);
}
