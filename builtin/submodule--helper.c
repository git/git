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
#include "remote.h"
#include "refs.h"
#include "connect.h"

static char *get_default_remote(void)
{
	char *dest = NULL, *ret;
	unsigned char sha1[20];
	struct strbuf sb = STRBUF_INIT;
	const char *refname = resolve_ref_unsafe("HEAD", 0, sha1, NULL);

	if (!refname)
		die(_("No such ref: %s"), "HEAD");

	/* detached HEAD */
	if (!strcmp(refname, "HEAD"))
		return xstrdup("origin");

	if (!skip_prefix(refname, "refs/heads/", &refname))
		die(_("Expecting a full ref name, got %s"), refname);

	strbuf_addf(&sb, "branch.%s.remote", refname);
	if (git_config_get_string(sb.buf, &dest))
		ret = xstrdup("origin");
	else
		ret = dest;

	strbuf_release(&sb);
	return ret;
}

static int starts_with_dot_slash(const char *str)
{
	return str[0] == '.' && is_dir_sep(str[1]);
}

static int starts_with_dot_dot_slash(const char *str)
{
	return str[0] == '.' && str[1] == '.' && is_dir_sep(str[2]);
}

/*
 * Returns 1 if it was the last chop before ':'.
 */
static int chop_last_dir(char **remoteurl, int is_relative)
{
	char *rfind = find_last_dir_sep(*remoteurl);
	if (rfind) {
		*rfind = '\0';
		return 0;
	}

	rfind = strrchr(*remoteurl, ':');
	if (rfind) {
		*rfind = '\0';
		return 1;
	}

	if (is_relative || !strcmp(".", *remoteurl))
		die(_("cannot strip one component off url '%s'"),
			*remoteurl);

	free(*remoteurl);
	*remoteurl = xstrdup(".");
	return 0;
}

/*
 * The `url` argument is the URL that navigates to the submodule origin
 * repo. When relative, this URL is relative to the superproject origin
 * URL repo. The `up_path` argument, if specified, is the relative
 * path that navigates from the submodule working tree to the superproject
 * working tree. Returns the origin URL of the submodule.
 *
 * Return either an absolute URL or filesystem path (if the superproject
 * origin URL is an absolute URL or filesystem path, respectively) or a
 * relative file system path (if the superproject origin URL is a relative
 * file system path).
 *
 * When the output is a relative file system path, the path is either
 * relative to the submodule working tree, if up_path is specified, or to
 * the superproject working tree otherwise.
 *
 * NEEDSWORK: This works incorrectly on the domain and protocol part.
 * remote_url      url              outcome          expectation
 * http://a.com/b  ../c             http://a.com/c   as is
 * http://a.com/b/ ../c             http://a.com/c   same as previous line, but
 *                                                   ignore trailing slash in url
 * http://a.com/b  ../../c          http://c         error out
 * http://a.com/b  ../../../c       http:/c          error out
 * http://a.com/b  ../../../../c    http:c           error out
 * http://a.com/b  ../../../../../c    .:c           error out
 * NEEDSWORK: Given how chop_last_dir() works, this function is broken
 * when a local part has a colon in its path component, too.
 */
static char *relative_url(const char *remote_url,
				const char *url,
				const char *up_path)
{
	int is_relative = 0;
	int colonsep = 0;
	char *out;
	char *remoteurl = xstrdup(remote_url);
	struct strbuf sb = STRBUF_INIT;
	size_t len = strlen(remoteurl);

	if (is_dir_sep(remoteurl[len-1]))
		remoteurl[len-1] = '\0';

	if (!url_is_local_not_ssh(remoteurl) || is_absolute_path(remoteurl))
		is_relative = 0;
	else {
		is_relative = 1;
		/*
		 * Prepend a './' to ensure all relative
		 * remoteurls start with './' or '../'
		 */
		if (!starts_with_dot_slash(remoteurl) &&
		    !starts_with_dot_dot_slash(remoteurl)) {
			strbuf_reset(&sb);
			strbuf_addf(&sb, "./%s", remoteurl);
			free(remoteurl);
			remoteurl = strbuf_detach(&sb, NULL);
		}
	}
	/*
	 * When the url starts with '../', remove that and the
	 * last directory in remoteurl.
	 */
	while (url) {
		if (starts_with_dot_dot_slash(url)) {
			url += 3;
			colonsep |= chop_last_dir(&remoteurl, is_relative);
		} else if (starts_with_dot_slash(url))
			url += 2;
		else
			break;
	}
	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s%s%s", remoteurl, colonsep ? ":" : "/", url);
	if (ends_with(url, "/"))
		strbuf_setlen(&sb, sb.len - 1);
	free(remoteurl);

	if (starts_with_dot_slash(sb.buf))
		out = xstrdup(sb.buf + 2);
	else
		out = xstrdup(sb.buf);
	strbuf_reset(&sb);

	if (!up_path || !is_relative)
		return out;

	strbuf_addf(&sb, "%s%s", up_path, out);
	free(out);
	return strbuf_detach(&sb, NULL);
}

static int resolve_relative_url(int argc, const char **argv, const char *prefix)
{
	char *remoteurl = NULL;
	char *remote = get_default_remote();
	const char *up_path = NULL;
	char *res;
	const char *url;
	struct strbuf sb = STRBUF_INIT;

	if (argc != 2 && argc != 3)
		die("resolve-relative-url only accepts one or two arguments");

	url = argv[1];
	strbuf_addf(&sb, "remote.%s.url", remote);
	free(remote);

	if (git_config_get_string(sb.buf, &remoteurl))
		/* the repository is its own authoritative upstream */
		remoteurl = xgetcwd();

	if (argc == 3)
		up_path = argv[2];

	res = relative_url(remoteurl, url, up_path);
	puts(res);
	free(res);
	free(remoteurl);
	return 0;
}

static int resolve_relative_url_test(int argc, const char **argv, const char *prefix)
{
	char *remoteurl, *res;
	const char *up_path, *url;

	if (argc != 4)
		die("resolve-relative-url-test only accepts three arguments: <up_path> <remoteurl> <url>");

	up_path = argv[1];
	remoteurl = xstrdup(argv[2]);
	url = argv[3];

	if (!strcmp(up_path, "(null)"))
		up_path = NULL;

	res = relative_url(remoteurl, url, up_path);
	puts(res);
	free(res);
	free(remoteurl);
	return 0;
}

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

static void module_list_active(struct module_list *list)
{
	int i;
	struct module_list active_modules = MODULE_LIST_INIT;

	gitmodules_config();

	for (i = 0; i < list->nr; i++) {
		const struct cache_entry *ce = list->entries[i];

		if (!is_submodule_initialized(ce->name))
			continue;

		ALLOC_GROW(active_modules.entries,
			   active_modules.nr + 1,
			   active_modules.alloc);
		active_modules.entries[active_modules.nr++] = ce;
	}

	free(list->entries);
	*list = active_modules;
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

	if (module_list_compute(argc, argv, prefix, &pathspec, &list) < 0)
		return 1;

	for (i = 0; i < list.nr; i++) {
		const struct cache_entry *ce = list.entries[i];

		if (ce_stage(ce))
			printf("%06o %s U\t", ce->ce_mode, sha1_to_hex(null_sha1));
		else
			printf("%06o %s %d\t", ce->ce_mode,
			       oid_to_hex(&ce->oid), ce_stage(ce));

		utf8_fprintf(stdout, "%s\n", ce->name);
	}
	return 0;
}

static void init_submodule(const char *path, const char *prefix, int quiet)
{
	const struct submodule *sub;
	struct strbuf sb = STRBUF_INIT;
	char *upd = NULL, *url = NULL, *displaypath;

	/* Only loads from .gitmodules, no overlay with .git/config */
	gitmodules_config();

	if (prefix && get_super_prefix())
		die("BUG: cannot have prefix and superprefix");
	else if (prefix)
		displaypath = xstrdup(relative_path(path, prefix, &sb));
	else if (get_super_prefix()) {
		strbuf_addf(&sb, "%s%s", get_super_prefix(), path);
		displaypath = strbuf_detach(&sb, NULL);
	} else
		displaypath = xstrdup(path);

	sub = submodule_from_path(null_sha1, path);

	if (!sub)
		die(_("No url found for submodule path '%s' in .gitmodules"),
			displaypath);

	/*
	 * NEEDSWORK: In a multi-working-tree world, this needs to be
	 * set in the per-worktree config.
	 *
	 * Set active flag for the submodule being initialized
	 */
	if (!is_submodule_initialized(path)) {
		strbuf_reset(&sb);
		strbuf_addf(&sb, "submodule.%s.active", sub->name);
		git_config_set_gently(sb.buf, "true");
	}

	/*
	 * Copy url setting when it is not set yet.
	 * To look up the url in .git/config, we must not fall back to
	 * .gitmodules, so look it up directly.
	 */
	strbuf_reset(&sb);
	strbuf_addf(&sb, "submodule.%s.url", sub->name);
	if (git_config_get_string(sb.buf, &url)) {
		url = xstrdup(sub->url);

		if (!url)
			die(_("No url found for submodule path '%s' in .gitmodules"),
				displaypath);

		/* Possibly a url relative to parent */
		if (starts_with_dot_dot_slash(url) ||
		    starts_with_dot_slash(url)) {
			char *remoteurl, *relurl;
			char *remote = get_default_remote();
			struct strbuf remotesb = STRBUF_INIT;
			strbuf_addf(&remotesb, "remote.%s.url", remote);
			free(remote);

			if (git_config_get_string(remotesb.buf, &remoteurl)) {
				warning(_("could not lookup configuration '%s'. Assuming this repository is its own authoritative upstream."), remotesb.buf);
				remoteurl = xgetcwd();
			}
			relurl = relative_url(remoteurl, url, NULL);
			strbuf_release(&remotesb);
			free(remoteurl);
			free(url);
			url = relurl;
		}

		if (git_config_set_gently(sb.buf, url))
			die(_("Failed to register url for submodule path '%s'"),
			    displaypath);
		if (!quiet)
			fprintf(stderr,
				_("Submodule '%s' (%s) registered for path '%s'\n"),
				sub->name, url, displaypath);
	}

	/* Copy "update" setting when it is not set yet */
	strbuf_reset(&sb);
	strbuf_addf(&sb, "submodule.%s.update", sub->name);
	if (git_config_get_string(sb.buf, &upd) &&
	    sub->update_strategy.type != SM_UPDATE_UNSPECIFIED) {
		if (sub->update_strategy.type == SM_UPDATE_COMMAND) {
			fprintf(stderr, _("warning: command update mode suggested for submodule '%s'\n"),
				sub->name);
			upd = xstrdup("none");
		} else
			upd = xstrdup(submodule_strategy_to_string(&sub->update_strategy));

		if (git_config_set_gently(sb.buf, upd))
			die(_("Failed to register update mode for submodule path '%s'"), displaypath);
	}
	strbuf_release(&sb);
	free(displaypath);
	free(url);
	free(upd);
}

static int module_init(int argc, const char **argv, const char *prefix)
{
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;
	int i;

	struct option module_init_options[] = {
		OPT__QUIET(&quiet, N_("Suppress output for initializing a submodule")),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule--helper init [<path>]"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_init_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(argc, argv, prefix, &pathspec, &list) < 0)
		return 1;

	/*
	 * If there are no path args and submodule.active is set then,
	 * by default, only initialize 'active' modules.
	 */
	if (!argc && git_config_get_value_multi("submodule.active"))
		module_list_active(&list);

	for (i = 0; i < list.nr; i++)
		init_submodule(list.entries[i]->name, prefix, quiet);

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

static int clone_submodule(const char *path, const char *gitdir, const char *url,
			   const char *depth, struct string_list *reference,
			   int quiet, int progress)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	argv_array_push(&cp.args, "clone");
	argv_array_push(&cp.args, "--no-checkout");
	if (quiet)
		argv_array_push(&cp.args, "--quiet");
	if (progress)
		argv_array_push(&cp.args, "--progress");
	if (depth && *depth)
		argv_array_pushl(&cp.args, "--depth", depth, NULL);
	if (reference->nr) {
		struct string_list_item *item;
		for_each_string_list_item(item, reference)
			argv_array_pushl(&cp.args, "--reference",
					 item->string, NULL);
	}
	if (gitdir && *gitdir)
		argv_array_pushl(&cp.args, "--separate-git-dir", gitdir, NULL);

	argv_array_push(&cp.args, url);
	argv_array_push(&cp.args, path);

	cp.git_cmd = 1;
	prepare_submodule_repo_env(&cp.env_array);
	cp.no_stdin = 1;

	return run_command(&cp);
}

struct submodule_alternate_setup {
	const char *submodule_name;
	enum SUBMODULE_ALTERNATE_ERROR_MODE {
		SUBMODULE_ALTERNATE_ERROR_DIE,
		SUBMODULE_ALTERNATE_ERROR_INFO,
		SUBMODULE_ALTERNATE_ERROR_IGNORE
	} error_mode;
	struct string_list *reference;
};
#define SUBMODULE_ALTERNATE_SETUP_INIT { NULL, \
	SUBMODULE_ALTERNATE_ERROR_IGNORE, NULL }

static int add_possible_reference_from_superproject(
		struct alternate_object_database *alt, void *sas_cb)
{
	struct submodule_alternate_setup *sas = sas_cb;

	/*
	 * If the alternate object store is another repository, try the
	 * standard layout with .git/(modules/<name>)+/objects
	 */
	if (ends_with(alt->path, "/objects")) {
		char *sm_alternate;
		struct strbuf sb = STRBUF_INIT;
		struct strbuf err = STRBUF_INIT;
		strbuf_add(&sb, alt->path, strlen(alt->path) - strlen("objects"));

		/*
		 * We need to end the new path with '/' to mark it as a dir,
		 * otherwise a submodule name containing '/' will be broken
		 * as the last part of a missing submodule reference would
		 * be taken as a file name.
		 */
		strbuf_addf(&sb, "modules/%s/", sas->submodule_name);

		sm_alternate = compute_alternate_path(sb.buf, &err);
		if (sm_alternate) {
			string_list_append(sas->reference, xstrdup(sb.buf));
			free(sm_alternate);
		} else {
			switch (sas->error_mode) {
			case SUBMODULE_ALTERNATE_ERROR_DIE:
				die(_("submodule '%s' cannot add alternate: %s"),
				    sas->submodule_name, err.buf);
			case SUBMODULE_ALTERNATE_ERROR_INFO:
				fprintf(stderr, _("submodule '%s' cannot add alternate: %s"),
					sas->submodule_name, err.buf);
			case SUBMODULE_ALTERNATE_ERROR_IGNORE:
				; /* nothing */
			}
		}
		strbuf_release(&sb);
	}

	return 0;
}

static void prepare_possible_alternates(const char *sm_name,
		struct string_list *reference)
{
	char *sm_alternate = NULL, *error_strategy = NULL;
	struct submodule_alternate_setup sas = SUBMODULE_ALTERNATE_SETUP_INIT;

	git_config_get_string("submodule.alternateLocation", &sm_alternate);
	if (!sm_alternate)
		return;

	git_config_get_string("submodule.alternateErrorStrategy", &error_strategy);

	if (!error_strategy)
		error_strategy = xstrdup("die");

	sas.submodule_name = sm_name;
	sas.reference = reference;
	if (!strcmp(error_strategy, "die"))
		sas.error_mode = SUBMODULE_ALTERNATE_ERROR_DIE;
	else if (!strcmp(error_strategy, "info"))
		sas.error_mode = SUBMODULE_ALTERNATE_ERROR_INFO;
	else if (!strcmp(error_strategy, "ignore"))
		sas.error_mode = SUBMODULE_ALTERNATE_ERROR_IGNORE;
	else
		die(_("Value '%s' for submodule.alternateErrorStrategy is not recognized"), error_strategy);

	if (!strcmp(sm_alternate, "superproject"))
		foreach_alt_odb(add_possible_reference_from_superproject, &sas);
	else if (!strcmp(sm_alternate, "no"))
		; /* do nothing */
	else
		die(_("Value '%s' for submodule.alternateLocation is not recognized"), sm_alternate);

	free(sm_alternate);
	free(error_strategy);
}

static int module_clone(int argc, const char **argv, const char *prefix)
{
	const char *name = NULL, *url = NULL, *depth = NULL;
	int quiet = 0;
	int progress = 0;
	char *p, *path = NULL, *sm_gitdir;
	struct strbuf sb = STRBUF_INIT;
	struct string_list reference = STRING_LIST_INIT_NODUP;
	char *sm_alternate = NULL, *error_strategy = NULL;

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
		OPT_STRING_LIST(0, "reference", &reference,
			   N_("repo"),
			   N_("reference repository")),
		OPT_STRING(0, "depth", &depth,
			   N_("string"),
			   N_("depth for shallow clones")),
		OPT__QUIET(&quiet, "Suppress output for cloning a submodule"),
		OPT_BOOL(0, "progress", &progress,
			   N_("force cloning progress")),
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

	if (argc || !url || !path || !*path)
		usage_with_options(git_submodule_helper_usage,
				   module_clone_options);

	strbuf_addf(&sb, "%s/modules/%s", get_git_dir(), name);
	sm_gitdir = absolute_pathdup(sb.buf);
	strbuf_reset(&sb);

	if (!is_absolute_path(path)) {
		strbuf_addf(&sb, "%s/%s", get_git_work_tree(), path);
		path = strbuf_detach(&sb, NULL);
	} else
		path = xstrdup(path);

	if (!file_exists(sm_gitdir)) {
		if (safe_create_leading_directories_const(sm_gitdir) < 0)
			die(_("could not create directory '%s'"), sm_gitdir);

		prepare_possible_alternates(name, &reference);

		if (clone_submodule(path, sm_gitdir, url, depth, &reference,
				    quiet, progress))
			die(_("clone of '%s' into submodule path '%s' failed"),
			    url, path);
	} else {
		if (safe_create_leading_directories_const(path) < 0)
			die(_("could not create directory '%s'"), path);
		strbuf_addf(&sb, "%s/index", sm_gitdir);
		unlink_or_warn(sb.buf);
		strbuf_reset(&sb);
	}

	/* Connect module worktree and git dir */
	connect_work_tree_and_git_dir(path, sm_gitdir);

	p = git_pathdup_submodule(path, "config");
	if (!p)
		die(_("could not get submodule directory for '%s'"), path);

	/* setup alternateLocation and alternateErrorStrategy in the cloned submodule if needed */
	git_config_get_string("submodule.alternateLocation", &sm_alternate);
	if (sm_alternate)
		git_config_set_in_file(p, "submodule.alternateLocation",
					   sm_alternate);
	git_config_get_string("submodule.alternateErrorStrategy", &error_strategy);
	if (error_strategy)
		git_config_set_in_file(p, "submodule.alternateErrorStrategy",
					   error_strategy);

	free(sm_alternate);
	free(error_strategy);

	strbuf_release(&sb);
	free(sm_gitdir);
	free(path);
	free(p);
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
	int progress;
	int quiet;
	int recommend_shallow;
	struct string_list references;
	const char *depth;
	const char *recursive_prefix;
	const char *prefix;

	/* Machine-readable status lines to be consumed by git-submodule.sh */
	struct string_list projectlines;

	/* If we want to stop as fast as possible and return an error */
	unsigned quickstop : 1;

	/* failed clones to be retried again */
	const struct cache_entry **failed_clones;
	int failed_clones_nr, failed_clones_alloc;
};
#define SUBMODULE_UPDATE_CLONE_INIT {0, MODULE_LIST_INIT, 0, \
	SUBMODULE_UPDATE_STRATEGY_INIT, 0, 0, -1, STRING_LIST_INIT_DUP, \
	NULL, NULL, NULL, \
	STRING_LIST_INIT_DUP, 0, NULL, 0, 0}


static void next_submodule_warn_missing(struct submodule_update_clone *suc,
		struct strbuf *out, const char *displaypath)
{
	/*
	 * Only mention uninitialized submodules when their
	 * paths have been specified.
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
}

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
	int needs_cloning = 0;

	if (ce_stage(ce)) {
		if (suc->recursive_prefix)
			strbuf_addf(&sb, "%s/%s", suc->recursive_prefix, ce->name);
		else
			strbuf_addstr(&sb, ce->name);
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

	if (!sub) {
		next_submodule_warn_missing(suc, out, displaypath);
		goto cleanup;
	}

	if (suc->update.type == SM_UPDATE_NONE
	    || (suc->update.type == SM_UPDATE_UNSPECIFIED
		&& sub->update_strategy.type == SM_UPDATE_NONE)) {
		strbuf_addf(out, _("Skipping submodule '%s'"), displaypath);
		strbuf_addch(out, '\n');
		goto cleanup;
	}

	/* Check if the submodule has been initialized. */
	if (!is_submodule_initialized(ce->name)) {
		next_submodule_warn_missing(suc, out, displaypath);
		goto cleanup;
	}

	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/.git", ce->name);
	needs_cloning = !file_exists(sb.buf);

	strbuf_reset(&sb);
	strbuf_addf(&sb, "%06o %s %d %d\t%s\n", ce->ce_mode,
			oid_to_hex(&ce->oid), ce_stage(ce),
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
	if (suc->progress)
		argv_array_push(&child->args, "--progress");
	if (suc->quiet)
		argv_array_push(&child->args, "--quiet");
	if (suc->prefix)
		argv_array_pushl(&child->args, "--prefix", suc->prefix, NULL);
	if (suc->recommend_shallow && sub->recommend_shallow == 1)
		argv_array_push(&child->args, "--depth=1");
	argv_array_pushl(&child->args, "--path", sub->path, NULL);
	argv_array_pushl(&child->args, "--name", sub->name, NULL);
	argv_array_pushl(&child->args, "--url", sub->url, NULL);
	if (suc->references.nr) {
		struct string_list_item *item;
		for_each_string_list_item(item, &suc->references)
			argv_array_pushl(&child->args, "--reference", item->string, NULL);
	}
	if (suc->depth)
		argv_array_push(&child->args, suc->depth);

cleanup:
	strbuf_reset(&displaypath_sb);
	strbuf_reset(&sb);

	return needs_cloning;
}

static int update_clone_get_next_task(struct child_process *child,
				      struct strbuf *err,
				      void *suc_cb,
				      void **idx_task_cb)
{
	struct submodule_update_clone *suc = suc_cb;
	const struct cache_entry *ce;
	int index;

	for (; suc->current < suc->list.nr; suc->current++) {
		ce = suc->list.entries[suc->current];
		if (prepare_to_clone_next_submodule(ce, child, suc, err)) {
			int *p = xmalloc(sizeof(*p));
			*p = suc->current;
			*idx_task_cb = p;
			suc->current++;
			return 1;
		}
	}

	/*
	 * The loop above tried cloning each submodule once, now try the
	 * stragglers again, which we can imagine as an extension of the
	 * entry list.
	 */
	index = suc->current - suc->list.nr;
	if (index < suc->failed_clones_nr) {
		int *p;
		ce = suc->failed_clones[index];
		if (!prepare_to_clone_next_submodule(ce, child, suc, err)) {
			suc->current ++;
			strbuf_addstr(err, "BUG: submodule considered for "
					   "cloning, doesn't need cloning "
					   "any more?\n");
			return 0;
		}
		p = xmalloc(sizeof(*p));
		*p = suc->current;
		*idx_task_cb = p;
		suc->current ++;
		return 1;
	}

	return 0;
}

static int update_clone_start_failure(struct strbuf *err,
				      void *suc_cb,
				      void *idx_task_cb)
{
	struct submodule_update_clone *suc = suc_cb;
	suc->quickstop = 1;
	return 1;
}

static int update_clone_task_finished(int result,
				      struct strbuf *err,
				      void *suc_cb,
				      void *idx_task_cb)
{
	const struct cache_entry *ce;
	struct submodule_update_clone *suc = suc_cb;

	int *idxP = *(int**)idx_task_cb;
	int idx = *idxP;
	free(idxP);

	if (!result)
		return 0;

	if (idx < suc->list.nr) {
		ce  = suc->list.entries[idx];
		strbuf_addf(err, _("Failed to clone '%s'. Retry scheduled"),
			    ce->name);
		strbuf_addch(err, '\n');
		ALLOC_GROW(suc->failed_clones,
			   suc->failed_clones_nr + 1,
			   suc->failed_clones_alloc);
		suc->failed_clones[suc->failed_clones_nr++] = ce;
		return 0;
	} else {
		idx -= suc->list.nr;
		ce  = suc->failed_clones[idx];
		strbuf_addf(err, _("Failed to clone '%s' a second time, aborting"),
			    ce->name);
		strbuf_addch(err, '\n');
		suc->quickstop = 1;
		return 1;
	}

	return 0;
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
		OPT_STRING_LIST(0, "reference", &suc.references, N_("repo"),
			   N_("reference repository")),
		OPT_STRING(0, "depth", &suc.depth, "<depth>",
			   N_("Create a shallow clone truncated to the "
			      "specified number of revisions")),
		OPT_INTEGER('j', "jobs", &max_jobs,
			    N_("parallel jobs")),
		OPT_BOOL(0, "recommend-shallow", &suc.recommend_shallow,
			    N_("whether the initial clone should follow the shallow recommendation")),
		OPT__QUIET(&suc.quiet, N_("don't print cloning progress")),
		OPT_BOOL(0, "progress", &suc.progress,
			    N_("force cloning progress")),
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

static int resolve_relative_path(int argc, const char **argv, const char *prefix)
{
	struct strbuf sb = STRBUF_INIT;
	if (argc != 3)
		die("submodule--helper relative-path takes exactly 2 arguments, got %d", argc);

	printf("%s", relative_path(argv[1], argv[2], &sb));
	strbuf_release(&sb);
	return 0;
}

static const char *remote_submodule_branch(const char *path)
{
	const struct submodule *sub;
	gitmodules_config();
	git_config(submodule_config, NULL);

	sub = submodule_from_path(null_sha1, path);
	if (!sub)
		return NULL;

	if (!sub->branch)
		return "master";

	if (!strcmp(sub->branch, ".")) {
		unsigned char sha1[20];
		const char *refname = resolve_ref_unsafe("HEAD", 0, sha1, NULL);

		if (!refname)
			die(_("No such ref: %s"), "HEAD");

		/* detached HEAD */
		if (!strcmp(refname, "HEAD"))
			die(_("Submodule (%s) branch configured to inherit "
			      "branch from superproject, but the superproject "
			      "is not on any branch"), sub->name);

		if (!skip_prefix(refname, "refs/heads/", &refname))
			die(_("Expecting a full ref name, got %s"), refname);
		return refname;
	}

	return sub->branch;
}

static int resolve_remote_submodule_branch(int argc, const char **argv,
		const char *prefix)
{
	const char *ret;
	struct strbuf sb = STRBUF_INIT;
	if (argc != 2)
		die("submodule--helper remote-branch takes exactly one arguments, got %d", argc);

	ret = remote_submodule_branch(argv[1]);
	if (!ret)
		die("submodule %s doesn't exist", argv[1]);

	printf("%s", ret);
	strbuf_release(&sb);
	return 0;
}

static int absorb_git_dirs(int argc, const char **argv, const char *prefix)
{
	int i;
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;
	unsigned flags = ABSORB_GITDIR_RECURSE_SUBMODULES;

	struct option embed_gitdir_options[] = {
		OPT_STRING(0, "prefix", &prefix,
			   N_("path"),
			   N_("path into the working tree")),
		OPT_BIT(0, "--recursive", &flags, N_("recurse into submodules"),
			ABSORB_GITDIR_RECURSE_SUBMODULES),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule--helper embed-git-dir [<path>...]"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, embed_gitdir_options,
			     git_submodule_helper_usage, 0);

	gitmodules_config();
	git_config(submodule_config, NULL);

	if (module_list_compute(argc, argv, prefix, &pathspec, &list) < 0)
		return 1;

	for (i = 0; i < list.nr; i++)
		absorb_git_dir_into_superproject(prefix,
				list.entries[i]->name, flags);

	return 0;
}

static int is_active(int argc, const char **argv, const char *prefix)
{
	if (argc != 2)
		die("submodule--helper is-active takes exactly 1 arguments");

	gitmodules_config();

	return !is_submodule_initialized(argv[1]);
}

#define SUPPORT_SUPER_PREFIX (1<<0)

struct cmd_struct {
	const char *cmd;
	int (*fn)(int, const char **, const char *);
	unsigned option;
};

static struct cmd_struct commands[] = {
	{"list", module_list, 0},
	{"name", module_name, 0},
	{"clone", module_clone, 0},
	{"update-clone", update_clone, 0},
	{"relative-path", resolve_relative_path, 0},
	{"resolve-relative-url", resolve_relative_url, 0},
	{"resolve-relative-url-test", resolve_relative_url_test, 0},
	{"init", module_init, SUPPORT_SUPER_PREFIX},
	{"remote-branch", resolve_remote_submodule_branch, 0},
	{"absorb-git-dirs", absorb_git_dirs, SUPPORT_SUPER_PREFIX},
	{"is-active", is_active, 0},
};

int cmd_submodule__helper(int argc, const char **argv, const char *prefix)
{
	int i;
	if (argc < 2)
		die(_("submodule--helper subcommand must be "
		      "called with a subcommand"));

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (!strcmp(argv[1], commands[i].cmd)) {
			if (get_super_prefix() &&
			    !(commands[i].option & SUPPORT_SUPER_PREFIX))
				die(_("%s doesn't support --super-prefix"),
				    commands[i].cmd);
			return commands[i].fn(argc - 1, argv + 1, prefix);
		}
	}

	die(_("'%s' is not a valid submodule--helper "
	      "subcommand"), argv[1]);
}
