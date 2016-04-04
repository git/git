#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "quote.h"
#include "pathspec.h"
#include "dir.h"
#include "utf8.h"
#include "submodule.h"
#include "submodule-config.h"
#include "string-list.h"
#include "run-command.h"

struct module_list {
	const struct cache_entry **entries;
	int alloc, nr;
};
#define MODULE_LIST_INIT { NULL, 0, 0 }

static int module_list_compute(int argc, const char **argv,
			       const char *prefix,
			       struct pathspec *pathspec,
			       struct module_list *list)
{
	int i, result = 0;
	char *ps_matched = NULL;
	parse_pathspec(pathspec, 0,
		       PATHSPEC_PREFER_FULL |
		       PATHSPEC_STRIP_SUBMODULE_SLASH_CHEAP,
		       prefix, argv);

	if (pathspec->nr)
		ps_matched = xcalloc(pathspec->nr, 1);

	if (read_cache() < 0)
		die(_("index file corrupt"));

	for (i = 0; i < active_nr; i++) {
		const struct cache_entry *ce = active_cache[i];

		if (!match_pathspec(pathspec, ce->name, ce_namelen(ce),
				    0, ps_matched, 1) ||
		    !S_ISGITLINK(ce->ce_mode))
			continue;

		ALLOC_GROW(list->entries, list->nr + 1, list->alloc);
		list->entries[list->nr++] = ce;
		while (i + 1 < active_nr &&
		       !strcmp(ce->name, active_cache[i + 1]->name))
			/*
			 * Skip entries with the same name in different stages
			 * to make sure an entry is returned only once.
			 */
			i++;
	}

	if (ps_matched && report_path_error(ps_matched, pathspec, prefix))
		result = -1;

	free(ps_matched);

	return result;
}

static int module_list(int argc, const char **argv, const char *prefix)
{
	int i;
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;

	struct option module_list_options[] = {
		OPT_STRING(0, "prefix", &prefix,
			   N_("path"),
			   N_("alternative anchor for relative paths")),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule--helper list [--prefix=<path>] [<path>...]"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_list_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(argc, argv, prefix, &pathspec, &list) < 0) {
		printf("#unmatched\n");
		return 1;
	}

	for (i = 0; i < list.nr; i++) {
		const struct cache_entry *ce = list.entries[i];

		if (ce_stage(ce))
			printf("%06o %s U\t", ce->ce_mode, sha1_to_hex(null_sha1));
		else
			printf("%06o %s %d\t", ce->ce_mode, sha1_to_hex(ce->sha1), ce_stage(ce));

		utf8_fprintf(stdout, "%s\n", ce->name);
	}
	return 0;
}

static int module_name(int argc, const char **argv, const char *prefix)
{
	const struct submodule *sub;

	if (argc != 2)
		usage(_("git submodule--helper name <path>"));

	gitmodules_config();
	sub = submodule_from_path(null_sha1, argv[1]);

	if (!sub)
		die(_("no submodule mapping found in .gitmodules for path '%s'"),
		    argv[1]);

	printf("%s\n", sub->name);

	return 0;
}

/*
 * Rules to sanitize configuration variables that are Ok to be passed into
 * submodule operations from the parent project using "-c". Should only
 * include keys which are both (a) safe and (b) necessary for proper
 * operation.
 */
static int submodule_config_ok(const char *var)
{
	if (starts_with(var, "credential."))
		return 1;
	return 0;
}

static int sanitize_submodule_config(const char *var, const char *value, void *data)
{
	struct strbuf *out = data;

	if (submodule_config_ok(var)) {
		if (out->len)
			strbuf_addch(out, ' ');

		if (value)
			sq_quotef(out, "%s=%s", var, value);
		else
			sq_quote_buf(out, var);
	}

	return 0;
}

static void prepare_submodule_repo_env(struct argv_array *out)
{
	const char * const *var;

	for (var = local_repo_env; *var; var++) {
		if (!strcmp(*var, CONFIG_DATA_ENVIRONMENT)) {
			struct strbuf sanitized_config = STRBUF_INIT;
			git_config_from_parameters(sanitize_submodule_config,
						   &sanitized_config);
			argv_array_pushf(out, "%s=%s", *var, sanitized_config.buf);
			strbuf_release(&sanitized_config);
		} else {
			argv_array_push(out, *var);
		}
	}

}

static int clone_submodule(const char *path, const char *gitdir, const char *url,
			   const char *depth, const char *reference, int quiet)
{
	struct child_process cp;
	child_process_init(&cp);

	argv_array_push(&cp.args, "clone");
	argv_array_push(&cp.args, "--no-checkout");
	if (quiet)
		argv_array_push(&cp.args, "--quiet");
	if (depth && *depth)
		argv_array_pushl(&cp.args, "--depth", depth, NULL);
	if (reference && *reference)
		argv_array_pushl(&cp.args, "--reference", reference, NULL);
	if (gitdir && *gitdir)
		argv_array_pushl(&cp.args, "--separate-git-dir", gitdir, NULL);

	argv_array_push(&cp.args, url);
	argv_array_push(&cp.args, path);

	cp.git_cmd = 1;
	prepare_submodule_repo_env(&cp.env_array);
	cp.no_stdin = 1;

	return run_command(&cp);
}

static int module_clone(int argc, const char **argv, const char *prefix)
{
	const char *path = NULL, *name = NULL, *url = NULL;
	const char *reference = NULL, *depth = NULL;
	int quiet = 0;
	FILE *submodule_dot_git;
	char *sm_gitdir, *cwd, *p;
	struct strbuf rel_path = STRBUF_INIT;
	struct strbuf sb = STRBUF_INIT;

	struct option module_clone_options[] = {
		OPT_STRING(0, "prefix", &prefix,
			   N_("path"),
			   N_("alternative anchor for relative paths")),
		OPT_STRING(0, "path", &path,
			   N_("path"),
			   N_("where the new submodule will be cloned to")),
		OPT_STRING(0, "name", &name,
			   N_("string"),
			   N_("name of the new submodule")),
		OPT_STRING(0, "url", &url,
			   N_("string"),
			   N_("url where to clone the submodule from")),
		OPT_STRING(0, "reference", &reference,
			   N_("string"),
			   N_("reference repository")),
		OPT_STRING(0, "depth", &depth,
			   N_("string"),
			   N_("depth for shallow clones")),
		OPT__QUIET(&quiet, "Suppress output for cloning a submodule"),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule--helper clone [--prefix=<path>] [--quiet] "
		   "[--reference <repository>] [--name <name>] [--depth <depth>] "
		   "--url <url> --path <path>"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_clone_options,
			     git_submodule_helper_usage, 0);

	if (argc || !url || !path)
		usage_with_options(git_submodule_helper_usage,
				   module_clone_options);

	strbuf_addf(&sb, "%s/modules/%s", get_git_dir(), name);
	sm_gitdir = strbuf_detach(&sb, NULL);

	if (!file_exists(sm_gitdir)) {
		if (safe_create_leading_directories_const(sm_gitdir) < 0)
			die(_("could not create directory '%s'"), sm_gitdir);
		if (clone_submodule(path, sm_gitdir, url, depth, reference, quiet))
			die(_("clone of '%s' into submodule path '%s' failed"),
			    url, path);
	} else {
		if (safe_create_leading_directories_const(path) < 0)
			die(_("could not create directory '%s'"), path);
		strbuf_addf(&sb, "%s/index", sm_gitdir);
		unlink_or_warn(sb.buf);
		strbuf_reset(&sb);
	}

	/* Write a .git file in the submodule to redirect to the superproject. */
	if (safe_create_leading_directories_const(path) < 0)
		die(_("could not create directory '%s'"), path);

	if (path && *path)
		strbuf_addf(&sb, "%s/.git", path);
	else
		strbuf_addstr(&sb, ".git");

	if (safe_create_leading_directories_const(sb.buf) < 0)
		die(_("could not create leading directories of '%s'"), sb.buf);
	submodule_dot_git = fopen(sb.buf, "w");
	if (!submodule_dot_git)
		die_errno(_("cannot open file '%s'"), sb.buf);

	fprintf(submodule_dot_git, "gitdir: %s\n",
		relative_path(sm_gitdir, path, &rel_path));
	if (fclose(submodule_dot_git))
		die(_("could not close file %s"), sb.buf);
	strbuf_reset(&sb);
	strbuf_reset(&rel_path);

	cwd = xgetcwd();
	/* Redirect the worktree of the submodule in the superproject's config */
	if (!is_absolute_path(sm_gitdir)) {
		strbuf_addf(&sb, "%s/%s", cwd, sm_gitdir);
		free(sm_gitdir);
		sm_gitdir = strbuf_detach(&sb, NULL);
	}

	strbuf_addf(&sb, "%s/%s", cwd, path);
	p = git_pathdup_submodule(path, "config");
	if (!p)
		die(_("could not get submodule directory for '%s'"), path);
	git_config_set_in_file(p, "core.worktree",
			       relative_path(sb.buf, sm_gitdir, &rel_path));
	strbuf_release(&sb);
	strbuf_release(&rel_path);
	free(sm_gitdir);
	free(cwd);
	free(p);
	return 0;
}

static int module_sanitize_config(int argc, const char **argv, const char *prefix)
{
	struct strbuf sanitized_config = STRBUF_INIT;

	if (argc > 1)
		usage(_("git submodule--helper sanitize-config"));

	git_config_from_parameters(sanitize_submodule_config, &sanitized_config);
	if (sanitized_config.len)
		printf("%s\n", sanitized_config.buf);

	strbuf_release(&sanitized_config);

	return 0;
}

struct submodule_update_clone {
	/* index into 'list', the list of submodules to look into for cloning */
	int current;
	struct module_list list;
	unsigned warn_if_uninitialized : 1;

	/* update parameter passed via commandline */
	struct submodule_update_strategy update;

	/* configuration parameters which are passed on to the children */
	int quiet;
	const char *reference;
	const char *depth;
	const char *recursive_prefix;
	const char *prefix;

	/* Machine-readable status lines to be consumed by git-submodule.sh */
	struct string_list projectlines;

	/* If we want to stop as fast as possible and return an error */
	unsigned quickstop : 1;
};
#define SUBMODULE_UPDATE_CLONE_INIT {0, MODULE_LIST_INIT, 0, \
	SUBMODULE_UPDATE_STRATEGY_INIT, 0, NULL, NULL, NULL, NULL, \
	STRING_LIST_INIT_DUP, 0}

/**
 * Determine whether 'ce' needs to be cloned. If so, prepare the 'child' to
 * run the clone. Returns 1 if 'ce' needs to be cloned, 0 otherwise.
 */
static int prepare_to_clone_next_submodule(const struct cache_entry *ce,
					   struct child_process *child,
					   struct submodule_update_clone *suc,
					   struct strbuf *out)
{
	const struct submodule *sub = NULL;
	struct strbuf displaypath_sb = STRBUF_INIT;
	struct strbuf sb = STRBUF_INIT;
	const char *displaypath = NULL;
	char *url = NULL;
	int needs_cloning = 0;

	if (ce_stage(ce)) {
		if (suc->recursive_prefix)
			strbuf_addf(&sb, "%s/%s", suc->recursive_prefix, ce->name);
		else
			strbuf_addf(&sb, "%s", ce->name);
		strbuf_addf(out, _("Skipping unmerged submodule %s"), sb.buf);
		strbuf_addch(out, '\n');
		goto cleanup;
	}

	sub = submodule_from_path(null_sha1, ce->name);

	if (suc->recursive_prefix)
		displaypath = relative_path(suc->recursive_prefix,
					    ce->name, &displaypath_sb);
	else
		displaypath = ce->name;

	if (suc->update.type == SM_UPDATE_NONE
	    || (suc->update.type == SM_UPDATE_UNSPECIFIED
		&& sub->update_strategy.type == SM_UPDATE_NONE)) {
		strbuf_addf(out, _("Skipping submodule '%s'"), displaypath);
		strbuf_addch(out, '\n');
		goto cleanup;
	}

	/*
	 * Looking up the url in .git/config.
	 * We must not fall back to .gitmodules as we only want
	 * to process configured submodules.
	 */
	strbuf_reset(&sb);
	strbuf_addf(&sb, "submodule.%s.url", sub->name);
	git_config_get_string(sb.buf, &url);
	if (!url) {
		/*
		 * Only mention uninitialized submodules when their
		 * path have been specified
		 */
		if (suc->warn_if_uninitialized) {
			strbuf_addf(out,
				_("Submodule path '%s' not initialized"),
				displaypath);
			strbuf_addch(out, '\n');
			strbuf_addstr(out,
				_("Maybe you want to use 'update --init'?"));
			strbuf_addch(out, '\n');
		}
		goto cleanup;
	}

	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/.git", ce->name);
	needs_cloning = !file_exists(sb.buf);

	strbuf_reset(&sb);
	strbuf_addf(&sb, "%06o %s %d %d\t%s\n", ce->ce_mode,
			sha1_to_hex(ce->sha1), ce_stage(ce),
			needs_cloning, ce->name);
	string_list_append(&suc->projectlines, sb.buf);

	if (!needs_cloning)
		goto cleanup;

	child->git_cmd = 1;
	child->no_stdin = 1;
	child->stdout_to_stderr = 1;
	child->err = -1;
	argv_array_push(&child->args, "submodule--helper");
	argv_array_push(&child->args, "clone");
	if (suc->quiet)
		argv_array_push(&child->args, "--quiet");
	if (suc->prefix)
		argv_array_pushl(&child->args, "--prefix", suc->prefix, NULL);
	argv_array_pushl(&child->args, "--path", sub->path, NULL);
	argv_array_pushl(&child->args, "--name", sub->name, NULL);
	argv_array_pushl(&child->args, "--url", url, NULL);
	if (suc->reference)
		argv_array_push(&child->args, suc->reference);
	if (suc->depth)
		argv_array_push(&child->args, suc->depth);

cleanup:
	free(url);
	strbuf_reset(&displaypath_sb);
	strbuf_reset(&sb);

	return needs_cloning;
}

static int update_clone_get_next_task(struct child_process *child,
				      struct strbuf *err,
				      void *suc_cb,
				      void **void_task_cb)
{
	struct submodule_update_clone *suc = suc_cb;

	for (; suc->current < suc->list.nr; suc->current++) {
		const struct cache_entry *ce = suc->list.entries[suc->current];
		if (prepare_to_clone_next_submodule(ce, child, suc, err)) {
			suc->current++;
			return 1;
		}
	}
	return 0;
}

static int update_clone_start_failure(struct strbuf *err,
				      void *suc_cb,
				      void *void_task_cb)
{
	struct submodule_update_clone *suc = suc_cb;
	suc->quickstop = 1;
	return 1;
}

static int update_clone_task_finished(int result,
				      struct strbuf *err,
				      void *suc_cb,
				      void *void_task_cb)
{
	struct submodule_update_clone *suc = suc_cb;

	if (!result)
		return 0;

	suc->quickstop = 1;
	return 1;
}

static int update_clone(int argc, const char **argv, const char *prefix)
{
	const char *update = NULL;
	int max_jobs = -1;
	struct string_list_item *item;
	struct pathspec pathspec;
	struct submodule_update_clone suc = SUBMODULE_UPDATE_CLONE_INIT;

	struct option module_update_clone_options[] = {
		OPT_STRING(0, "prefix", &prefix,
			   N_("path"),
			   N_("path into the working tree")),
		OPT_STRING(0, "recursive-prefix", &suc.recursive_prefix,
			   N_("path"),
			   N_("path into the working tree, across nested "
			      "submodule boundaries")),
		OPT_STRING(0, "update", &update,
			   N_("string"),
			   N_("rebase, merge, checkout or none")),
		OPT_STRING(0, "reference", &suc.reference, N_("repo"),
			   N_("reference repository")),
		OPT_STRING(0, "depth", &suc.depth, "<depth>",
			   N_("Create a shallow clone truncated to the "
			      "specified number of revisions")),
		OPT_INTEGER('j', "jobs", &max_jobs,
			    N_("parallel jobs")),
		OPT__QUIET(&suc.quiet, N_("don't print cloning progress")),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule--helper update_clone [--prefix=<path>] [<path>...]"),
		NULL
	};
	suc.prefix = prefix;

	argc = parse_options(argc, argv, prefix, module_update_clone_options,
			     git_submodule_helper_usage, 0);

	if (update)
		if (parse_submodule_update_strategy(update, &suc.update) < 0)
			die(_("bad value for update parameter"));

	if (module_list_compute(argc, argv, prefix, &pathspec, &suc.list) < 0)
		return 1;

	if (pathspec.nr)
		suc.warn_if_uninitialized = 1;

	/* Overlay the parsed .gitmodules file with .git/config */
	gitmodules_config();
	git_config(submodule_config, NULL);

	if (max_jobs < 0)
		max_jobs = parallel_submodules();

	run_processes_parallel(max_jobs,
			       update_clone_get_next_task,
			       update_clone_start_failure,
			       update_clone_task_finished,
			       &suc);

	/*
	 * We saved the output and put it out all at once now.
	 * That means:
	 * - the listener does not have to interleave their (checkout)
	 *   work with our fetching.  The writes involved in a
	 *   checkout involve more straightforward sequential I/O.
	 * - the listener can avoid doing any work if fetching failed.
	 */
	if (suc.quickstop)
		return 1;

	for_each_string_list_item(item, &suc.projectlines)
		utf8_fprintf(stdout, "%s", item->string);

	return 0;
}

struct cmd_struct {
	const char *cmd;
	int (*fn)(int, const char **, const char *);
};

static struct cmd_struct commands[] = {
	{"list", module_list},
	{"name", module_name},
	{"clone", module_clone},
	{"sanitize-config", module_sanitize_config},
	{"update-clone", update_clone}
};

int cmd_submodule__helper(int argc, const char **argv, const char *prefix)
{
	int i;
	if (argc < 2)
		die(_("submodule--helper subcommand must be "
		      "called with a subcommand"));

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		if (!strcmp(argv[1], commands[i].cmd))
			return commands[i].fn(argc - 1, argv + 1, prefix);

	die(_("'%s' is not a valid submodule--helper "
	      "subcommand"), argv[1]);
}
