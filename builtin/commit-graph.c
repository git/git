#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "commit.h"
#include "config.h"
#include "gettext.h"
#include "hex.h"
#include "parse-options.h"
#include "commit-graph.h"
#include "odb.h"
#include "progress.h"
#include "replace-object.h"
#include "strbuf.h"
#include "tag.h"
#include "trace2.h"

#define BUILTIN_COMMIT_GRAPH_VERIFY_USAGE \
	N_("git commit-graph verify [--object-dir <dir>] [--shallow] [--[no-]progress]")

#define BUILTIN_COMMIT_GRAPH_WRITE_USAGE \
	N_("git commit-graph write [--object-dir <dir>] [--append]\n" \
	   "                       [--split[=<strategy>]] [--reachable | --stdin-packs | --stdin-commits]\n" \
	   "                       [--changed-paths] [--[no-]max-new-filters <n>] [--[no-]progress]\n" \
	   "                       <split-options>")

static const char * const builtin_commit_graph_verify_usage[] = {
	BUILTIN_COMMIT_GRAPH_VERIFY_USAGE,
	NULL
};

static const char * const builtin_commit_graph_write_usage[] = {
	BUILTIN_COMMIT_GRAPH_WRITE_USAGE,
	NULL
};

static char const * const builtin_commit_graph_usage[] = {
	BUILTIN_COMMIT_GRAPH_VERIFY_USAGE,
	BUILTIN_COMMIT_GRAPH_WRITE_USAGE,
	NULL,
};

static struct opts_commit_graph {
	const char *obj_dir;
	int reachable;
	int stdin_packs;
	int stdin_commits;
	int append;
	int split;
	int shallow;
	int progress;
	int enable_changed_paths;
} opts;

static struct option common_opts[] = {
	OPT_STRING(0, "object-dir", &opts.obj_dir,
		   N_("dir"),
		   N_("the object directory to store the graph")),
	OPT_END()
};

static struct option *add_common_options(struct option *to)
{
	return parse_options_concat(common_opts, to);
}

static int graph_verify(int argc, const char **argv, const char *prefix,
			struct repository *repo UNUSED)
{
	struct commit_graph *graph = NULL;
	struct odb_source *source = NULL;
	char *graph_name;
	char *chain_name;
	enum { OPENED_NONE, OPENED_GRAPH, OPENED_CHAIN } opened = OPENED_NONE;
	int fd;
	struct stat st;
	int flags = 0;
	int incomplete_chain = 0;
	int ret;

	static struct option builtin_commit_graph_verify_options[] = {
		OPT_BOOL(0, "shallow", &opts.shallow,
			 N_("if the commit-graph is split, only verify the tip file")),
		OPT_BOOL(0, "progress", &opts.progress,
			 N_("force progress reporting")),
		OPT_END(),
	};
	struct option *options = add_common_options(builtin_commit_graph_verify_options);

	trace2_cmd_mode("verify");

	opts.progress = isatty(2);
	argc = parse_options(argc, argv, prefix,
			     options,
			     builtin_commit_graph_verify_usage, 0);
	if (argc)
		usage_with_options(builtin_commit_graph_verify_usage, options);

	if (!opts.obj_dir)
		opts.obj_dir = repo_get_object_directory(the_repository);
	if (opts.shallow)
		flags |= COMMIT_GRAPH_VERIFY_SHALLOW;
	if (opts.progress)
		flags |= COMMIT_GRAPH_WRITE_PROGRESS;

	source = odb_find_source(the_repository->objects, opts.obj_dir);
	graph_name = get_commit_graph_filename(source);
	chain_name = get_commit_graph_chain_filename(source);
	if (open_commit_graph(graph_name, &fd, &st))
		opened = OPENED_GRAPH;
	else if (errno != ENOENT)
		die_errno(_("Could not open commit-graph '%s'"), graph_name);
	else if (open_commit_graph_chain(chain_name, &fd, &st))
		opened = OPENED_CHAIN;
	else if (errno != ENOENT)
		die_errno(_("could not open commit-graph chain '%s'"), chain_name);

	FREE_AND_NULL(graph_name);
	FREE_AND_NULL(chain_name);
	FREE_AND_NULL(options);

	if (opened == OPENED_NONE)
		return 0;
	else if (opened == OPENED_GRAPH)
		graph = load_commit_graph_one_fd_st(the_repository, fd, &st, source);
	else
		graph = load_commit_graph_chain_fd_st(the_repository, fd, &st,
						      &incomplete_chain);

	if (!graph)
		return 1;

	ret = verify_commit_graph(the_repository, graph, flags);
	free_commit_graph(graph);

	if (incomplete_chain) {
		error("one or more commit-graph chain files could not be loaded");
		ret |= 1;
	}

	return ret;
}

extern int read_replace_refs;
static struct commit_graph_opts write_opts;

static int write_option_parse_split(const struct option *opt, const char *arg,
				    int unset)
{
	enum commit_graph_split_flags *flags = opt->value;

	BUG_ON_OPT_NEG(unset);

	opts.split = 1;
	if (!arg)
		return 0;

	if (!strcmp(arg, "no-merge"))
		*flags = COMMIT_GRAPH_SPLIT_MERGE_PROHIBITED;
	else if (!strcmp(arg, "replace"))
		*flags = COMMIT_GRAPH_SPLIT_REPLACE;
	else
		die(_("unrecognized --split argument, %s"), arg);

	return 0;
}

static int read_one_commit(struct oidset *commits, struct progress *progress,
			   const char *hash)
{
	struct object *result;
	struct object_id oid;
	const char *end;

	if (parse_oid_hex(hash, &oid, &end))
		return error(_("unexpected non-hex object ID: %s"), hash);

	result = deref_tag(the_repository, parse_object(the_repository, &oid),
			   NULL, 0);
	if (!result)
		return error(_("invalid object: %s"), hash);
	else if (object_as_type(result, OBJ_COMMIT, 1))
		oidset_insert(commits, &result->oid);

	display_progress(progress, oidset_size(commits));

	return 0;
}

static int write_option_max_new_filters(const struct option *opt,
					const char *arg,
					int unset)
{
	int *to = opt->value;
	if (unset)
		*to = -1;
	else {
		const char *s;
		*to = strtol(arg, (char **)&s, 10);
		if (*s)
			return error(_("option `%s' expects a numerical value"),
				     "max-new-filters");
	}
	return 0;
}

static int git_commit_graph_write_config(const char *var, const char *value,
					 const struct config_context *ctx,
					 void *cb UNUSED)
{
	if (!strcmp(var, "commitgraph.maxnewfilters"))
		write_opts.max_new_filters = git_config_int(var, value, ctx->kvi);
	/*
	 * No need to fall-back to 'git_default_config', since this was already
	 * called in 'cmd_commit_graph()'.
	 */
	return 0;
}

static int graph_write(int argc, const char **argv, const char *prefix,
		       struct repository *repo UNUSED)
{
	struct string_list pack_indexes = STRING_LIST_INIT_DUP;
	struct strbuf buf = STRBUF_INIT;
	struct oidset commits = OIDSET_INIT;
	struct odb_source *source = NULL;
	int result = 0;
	enum commit_graph_write_flags flags = 0;
	struct progress *progress = NULL;

	static struct option builtin_commit_graph_write_options[] = {
		OPT_BOOL(0, "reachable", &opts.reachable,
			N_("start walk at all refs")),
		OPT_BOOL(0, "stdin-packs", &opts.stdin_packs,
			N_("scan pack-indexes listed by stdin for commits")),
		OPT_BOOL(0, "stdin-commits", &opts.stdin_commits,
			N_("start walk at commits listed by stdin")),
		OPT_BOOL(0, "append", &opts.append,
			N_("include all commits already in the commit-graph file")),
		OPT_BOOL(0, "changed-paths", &opts.enable_changed_paths,
			N_("enable computation for changed paths")),
		OPT_CALLBACK_F(0, "split", &write_opts.split_flags, NULL,
			N_("allow writing an incremental commit-graph file"),
			PARSE_OPT_OPTARG | PARSE_OPT_NONEG,
			write_option_parse_split),
		OPT_INTEGER(0, "max-commits", &write_opts.max_commits,
			N_("maximum number of commits in a non-base split commit-graph")),
		OPT_INTEGER(0, "size-multiple", &write_opts.size_multiple,
			N_("maximum ratio between two levels of a split commit-graph")),
		OPT_EXPIRY_DATE(0, "expire-time", &write_opts.expire_time,
			N_("only expire files older than a given date-time")),
		OPT_CALLBACK_F(0, "max-new-filters", &write_opts.max_new_filters,
			NULL, N_("maximum number of changed-path Bloom filters to compute"),
			0, write_option_max_new_filters),
		OPT_BOOL(0, "progress", &opts.progress,
			 N_("force progress reporting")),
		OPT_END(),
	};
	struct option *options = add_common_options(builtin_commit_graph_write_options);

	opts.progress = isatty(2);
	opts.enable_changed_paths = -1;
	write_opts.size_multiple = 2;
	write_opts.max_commits = 0;
	write_opts.expire_time = 0;
	write_opts.max_new_filters = -1;

	trace2_cmd_mode("write");

	git_config(git_commit_graph_write_config, &opts);

	argc = parse_options(argc, argv, prefix,
			     options,
			     builtin_commit_graph_write_usage, 0);
	if (argc)
		usage_with_options(builtin_commit_graph_write_usage, options);

	if (opts.reachable + opts.stdin_packs + opts.stdin_commits > 1)
		die(_("use at most one of --reachable, --stdin-commits, or --stdin-packs"));
	if (!opts.obj_dir)
		opts.obj_dir = repo_get_object_directory(the_repository);
	if (opts.append)
		flags |= COMMIT_GRAPH_WRITE_APPEND;
	if (opts.split)
		flags |= COMMIT_GRAPH_WRITE_SPLIT;
	if (opts.progress)
		flags |= COMMIT_GRAPH_WRITE_PROGRESS;
	if (!opts.enable_changed_paths)
		flags |= COMMIT_GRAPH_NO_WRITE_BLOOM_FILTERS;
	if (opts.enable_changed_paths == 1 ||
	    git_env_bool(GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS, 0))
		flags |= COMMIT_GRAPH_WRITE_BLOOM_FILTERS;

	source = odb_find_source(the_repository->objects, opts.obj_dir);

	if (opts.reachable) {
		if (write_commit_graph_reachable(source, flags, &write_opts))
			result = 1;
		goto cleanup;
	}

	if (opts.stdin_packs) {
		while (strbuf_getline(&buf, stdin) != EOF)
			string_list_append_nodup(&pack_indexes,
						 strbuf_detach(&buf, NULL));
	} else if (opts.stdin_commits) {
		oidset_init(&commits, 0);
		if (opts.progress)
			progress = start_delayed_progress(
				the_repository,
				_("Collecting commits from input"), 0);

		while (strbuf_getline(&buf, stdin) != EOF) {
			if (read_one_commit(&commits, progress, buf.buf)) {
				result = 1;
				stop_progress(&progress);
				goto cleanup;
			}
		}

		stop_progress(&progress);
	}

	if (write_commit_graph(source,
			       opts.stdin_packs ? &pack_indexes : NULL,
			       opts.stdin_commits ? &commits : NULL,
			       flags,
			       &write_opts))
		result = 1;

cleanup:
	FREE_AND_NULL(options);
	string_list_clear(&pack_indexes, 0);
	strbuf_release(&buf);
	oidset_clear(&commits);
	return result;
}

int cmd_commit_graph(int argc,
		     const char **argv,
		     const char *prefix,
		     struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option builtin_commit_graph_options[] = {
		OPT_SUBCOMMAND("verify", &fn, graph_verify),
		OPT_SUBCOMMAND("write", &fn, graph_write),
		OPT_END(),
	};
	struct option *options = parse_options_concat(builtin_commit_graph_options, common_opts);

	git_config(git_default_config, NULL);

	disable_replace_refs();
	save_commit_buffer = 0;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_commit_graph_usage, 0);
	FREE_AND_NULL(options);

	return fn(argc, argv, prefix, repo);
}
