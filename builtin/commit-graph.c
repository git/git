#include "builtin.h"
#include "commit.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "parse-options.h"
#include "commit-graph.h"
#include "odb.h"
#include "odb/source.h"
#include "odb/source-files.h"
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

struct opts_commit_graph {
	const char *obj_dir;
	int stdin_packs;
	int stdin_commits;
	int enable_changed_paths;
};

#define COMMIT_GRAPH_COMMON_OPTIONS(opts) \
	OPT_STRING(0, "object-dir", &(opts)->obj_dir, \
		   N_("dir"), \
		   N_("the object directory to store the graph"))

static int cmd_commit_graph_verify(int argc,
				   const char **argv,
				   const char *prefix,
				   struct repository *repo)
{
	struct opts_commit_graph opts = { 0 };
	struct commit_graph *graph = NULL;
	struct odb_files_dir *dir = NULL;
	char *graph_name;
	char *chain_name;
	enum { OPENED_NONE, OPENED_GRAPH, OPENED_CHAIN } opened = OPENED_NONE;
	enum commit_graph_verify_flags flags = isatty(2) ? COMMIT_GRAPH_VERIFY_PROGRESS : 0;
	int fd;
	struct stat st;
	int incomplete_chain = 0;
	int ret;
	struct option options[] = {
		OPT_BIT(0, "shallow", &flags,
			N_("if the commit-graph is split, only verify the tip file"),
			COMMIT_GRAPH_VERIFY_SHALLOW),
		OPT_BIT(0, "progress", &flags,
			N_("force progress reporting"), COMMIT_GRAPH_VERIFY_PROGRESS),
		COMMIT_GRAPH_COMMON_OPTIONS(&opts),
		OPT_END(),
	};

	trace2_cmd_mode("verify");

	argc = parse_options(argc, argv, prefix,
			     options,
			     builtin_commit_graph_verify_usage, 0);
	if (argc)
		usage_with_options(builtin_commit_graph_verify_usage, options);

	if (!opts.obj_dir)
		opts.obj_dir = repo_get_object_directory(repo);

	dir = odb_source_files_find_dir(repo->objects, opts.obj_dir);
	if (!dir)
		die(_("could not find object directory matching %s"), opts.obj_dir);

	graph_name = get_commit_graph_filename(dir->abspath);
	chain_name = get_commit_graph_chain_filename(dir->abspath);
	if (open_commit_graph(graph_name, &fd, &st))
		opened = OPENED_GRAPH;
	else if (errno != ENOENT)
		die_errno(_("Could not open commit-graph '%s'"), graph_name);
	else if (open_commit_graph_chain(chain_name, &fd, &st,
					 repo->hash_algo))
		opened = OPENED_CHAIN;
	else if (errno != ENOENT)
		die_errno(_("could not open commit-graph chain '%s'"), chain_name);

	FREE_AND_NULL(graph_name);
	FREE_AND_NULL(chain_name);

	if (opened == OPENED_NONE)
		return 0;
	else if (opened == OPENED_GRAPH)
		graph = load_commit_graph_one_fd_st(repo, dir->abspath, fd, &st);
	else
		graph = load_commit_graph_chain_fd_st(repo->objects, fd, &st,
						      &incomplete_chain);

	if (!graph)
		return 1;

	ret = commit_graph_verify(graph, flags);
	commit_graph_free(graph);

	if (incomplete_chain) {
		error("one or more commit-graph chain files could not be loaded");
		ret |= 1;
	}

	return ret;
}

struct commit_graph_write_config_data {
	struct commit_graph_write_options *write_opts;
	struct opts_commit_graph *opts;
};

static int write_option_parse_split(const struct option *opt, const char *arg,
				    int unset)
{
	struct commit_graph_write_config_data *data = opt->value;

	BUG_ON_OPT_NEG(unset);

	data->write_opts->flags |= COMMIT_GRAPH_WRITE_SPLIT;
	if (!arg)
		return 0;

	if (!strcmp(arg, "no-merge"))
		data->write_opts->split_flags = COMMIT_GRAPH_SPLIT_MERGE_PROHIBITED;
	else if (!strcmp(arg, "replace"))
		data->write_opts->split_flags = COMMIT_GRAPH_SPLIT_REPLACE;
	else
		die(_("unrecognized --split argument, %s"), arg);

	return 0;
}

static int read_one_commit(struct repository *repo,
			   struct oidset *commits,
			   struct progress *progress,
			   const char *hash)
{
	struct object *result;
	struct object_id oid;
	const char *end;

	if (parse_oid_hex_algop(hash, &oid, &end, repo->hash_algo))
		return error(_("unexpected non-hex object ID: %s"), hash);

	result = deref_tag(repo, parse_object(repo, &oid),
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
					 void *cb_data)
{
	struct commit_graph_write_config_data *data = cb_data;

	if (!strcmp(var, "commitgraph.maxnewfilters"))
		data->write_opts->max_new_filters = git_config_int(var, value, ctx->kvi);
	else if (!strcmp(var, "commitgraph.changedpaths"))
		data->opts->enable_changed_paths = git_config_bool(var, value) ? 1 : -1;
	/*
	 * No need to fall-back to 'git_default_config', since this was already
	 * called in 'cmd_commit_graph()'.
	 */
	return 0;
}

static int cmd_commit_graph_write(int argc,
				  const char **argv,
				  const char *prefix,
				  struct repository *repo)
{
	struct commit_graph_write_options write_opts = COMMIT_GRAPH_WRITE_OPTIONS_INIT;
	struct opts_commit_graph opts = {
		.enable_changed_paths = -1,
	};
	struct commit_graph_write_config_data config_data = {
		.write_opts = &write_opts,
		.opts = &opts,
	};
	struct string_list pack_indexes = STRING_LIST_INIT_DUP;
	struct strbuf buf = STRBUF_INIT;
	struct oidset commits = OIDSET_INIT;
	struct odb_files_dir *dir = NULL;
	int result = 0;
	struct progress *progress = NULL;
	struct option options[] = {
		OPT_BIT(0, "reachable", &write_opts.flags,
			N_("start walk at all refs"), COMMIT_GRAPH_WRITE_REACHABLE),
		OPT_BOOL(0, "stdin-packs", &opts.stdin_packs,
			N_("scan pack-indexes listed by stdin for commits")),
		OPT_BOOL(0, "stdin-commits", &opts.stdin_commits,
			N_("start walk at commits listed by stdin")),
		OPT_BIT(0, "append", &write_opts.flags,
			N_("include all commits already in the commit-graph file"),
			COMMIT_GRAPH_WRITE_APPEND),
		OPT_BOOL(0, "changed-paths", &opts.enable_changed_paths,
			N_("enable computation for changed paths")),
		OPT_CALLBACK_F(0, "split", &config_data, NULL,
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
		OPT_BIT(0, "progress", &write_opts.flags,
			N_("force progress reporting"), COMMIT_GRAPH_WRITE_PROGRESS),
		COMMIT_GRAPH_COMMON_OPTIONS(&opts),
		OPT_END(),
	};

	trace2_cmd_mode("write");

	write_opts.flags |= isatty(2) ? COMMIT_GRAPH_WRITE_PROGRESS : 0;

	repo_config(repo, git_commit_graph_write_config, &config_data);

	argc = parse_options(argc, argv, prefix,
			     options,
			     builtin_commit_graph_write_usage, 0);
	if (argc)
		usage_with_options(builtin_commit_graph_write_usage, options);

	if (!!(write_opts.flags & COMMIT_GRAPH_WRITE_REACHABLE) + opts.stdin_packs + opts.stdin_commits > 1)
		die(_("use at most one of --reachable, --stdin-commits, or --stdin-packs"));
	if (!opts.obj_dir)
		opts.obj_dir = repo_get_object_directory(repo);
	if (!opts.enable_changed_paths)
		write_opts.flags |= COMMIT_GRAPH_NO_WRITE_BLOOM_FILTERS;
	if (opts.enable_changed_paths == 1 ||
	    git_env_bool(GIT_TEST_COMMIT_GRAPH_CHANGED_PATHS, 0))
		write_opts.flags |= COMMIT_GRAPH_WRITE_BLOOM_FILTERS;

	dir = odb_source_files_find_dir(repo->objects, opts.obj_dir);
	if (!dir)
		die(_("could not find object directory matching %s"), opts.obj_dir);

	if (opts.stdin_packs) {
		struct strbuf packname = STRBUF_INIT;
		size_t dirlen;

		strbuf_addf(&packname, "%s/pack/", dir->abspath);
		dirlen = packname.len;

		while (strbuf_getline(&buf, stdin) != EOF) {
			strbuf_setlen(&packname, dirlen);
			strbuf_addbuf(&packname, &buf);
			string_list_append(&pack_indexes, packname.buf);
		}

		write_opts.pack_indexes = &pack_indexes;
		strbuf_release(&packname);
	} else if (opts.stdin_commits) {
		oidset_init(&commits, 0);
		if (write_opts.flags & COMMIT_GRAPH_WRITE_PROGRESS)
			progress = start_delayed_progress(
				repo,
				_("Collecting commits from input"), 0);

		while (strbuf_getline(&buf, stdin) != EOF) {
			if (read_one_commit(repo, &commits, progress, buf.buf)) {
				result = 1;
				stop_progress(&progress);
				goto cleanup;
			}
		}

		write_opts.commits = &commits;
		stop_progress(&progress);
	}

	if (commit_graph_write(repo, dir->abspath, &write_opts))
		result = 1;

cleanup:
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
	struct option options[] = {
		OPT_SUBCOMMAND("verify", &fn, cmd_commit_graph_verify),
		OPT_SUBCOMMAND("write", &fn, cmd_commit_graph_write),
		OPT_END(),
	};

	repo_config(repo, git_default_config, NULL);

	disable_replace_refs();
	save_commit_buffer = 0;

	argc = parse_options(argc, argv, prefix, options,
			     builtin_commit_graph_usage, 0);

	return fn(argc, argv, prefix, repo);
}
