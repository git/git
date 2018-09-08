#include "builtin.h"
#include "repository.h"
#include "cache.h"
#include "config.h"
#include "parse-options.h"
#include "quote.h"
#include "pathspec.h"
#include "dir.h"
#include "submodule.h"
#include "submodule-config.h"
#include "string-list.h"
#include "run-command.h"
#include "remote.h"
#include "refs.h"
#include "refspec.h"
#include "connect.h"
#include "revision.h"
#include "diffcore.h"
#include "diff.h"
#include "object-store.h"

#define OPT_QUIET (1 << 0)
#define OPT_CACHED (1 << 1)
#define OPT_RECURSIVE (1 << 2)
#define OPT_FORCE (1 << 3)

typedef void (*each_submodule_fn)(const struct cache_entry *list_item,
				  void *cb_data);

static char *get_default_remote(void)
{
	char *dest = NULL, *ret;
	struct strbuf sb = STRBUF_INIT;
	const char *refname = resolve_ref_unsafe("HEAD", 0, NULL, NULL);

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

static int print_default_remote(int argc, const char **argv, const char *prefix)
{
	char *remote;

	if (argc != 1)
		die(_("submodule--helper print-default-remote takes no arguments"));

	remote = get_default_remote();
	if (remote)
		printf("%s\n", remote);

	free(remote);
	return 0;
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

/* the result should be freed by the caller. */
static char *get_submodule_displaypath(const char *path, const char *prefix)
{
	const char *super_prefix = get_super_prefix();

	if (prefix && super_prefix) {
		BUG("cannot have prefix '%s' and superprefix '%s'",
		    prefix, super_prefix);
	} else if (prefix) {
		struct strbuf sb = STRBUF_INIT;
		char *displaypath = xstrdup(relative_path(path, prefix, &sb));
		strbuf_release(&sb);
		return displaypath;
	} else if (super_prefix) {
		return xstrfmt("%s%s", super_prefix, path);
	} else {
		return xstrdup(path);
	}
}

static char *compute_rev_name(const char *sub_path, const char* object_id)
{
	struct strbuf sb = STRBUF_INIT;
	const char ***d;

	static const char *describe_bare[] = { NULL };

	static const char *describe_tags[] = { "--tags", NULL };

	static const char *describe_contains[] = { "--contains", NULL };

	static const char *describe_all_always[] = { "--all", "--always", NULL };

	static const char **describe_argv[] = { describe_bare, describe_tags,
						describe_contains,
						describe_all_always, NULL };

	for (d = describe_argv; *d; d++) {
		struct child_process cp = CHILD_PROCESS_INIT;
		prepare_submodule_repo_env(&cp.env_array);
		cp.dir = sub_path;
		cp.git_cmd = 1;
		cp.no_stderr = 1;

		argv_array_push(&cp.args, "describe");
		argv_array_pushv(&cp.args, *d);
		argv_array_push(&cp.args, object_id);

		if (!capture_command(&cp, &sb, 0)) {
			strbuf_strip_suffix(&sb, "\n");
			return strbuf_detach(&sb, NULL);
		}
	}

	strbuf_release(&sb);
	return NULL;
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
		       PATHSPEC_PREFER_FULL,
		       prefix, argv);

	if (pathspec->nr)
		ps_matched = xcalloc(pathspec->nr, 1);

	if (read_cache() < 0)
		die(_("index file corrupt"));

	for (i = 0; i < active_nr; i++) {
		const struct cache_entry *ce = active_cache[i];

		if (!match_pathspec(&the_index, pathspec, ce->name, ce_namelen(ce),
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

	for (i = 0; i < list->nr; i++) {
		const struct cache_entry *ce = list->entries[i];

		if (!is_submodule_active(the_repository, ce->name))
			continue;

		ALLOC_GROW(active_modules.entries,
			   active_modules.nr + 1,
			   active_modules.alloc);
		active_modules.entries[active_modules.nr++] = ce;
	}

	free(list->entries);
	*list = active_modules;
}

static char *get_up_path(const char *path)
{
	int i;
	struct strbuf sb = STRBUF_INIT;

	for (i = count_slashes(path); i; i--)
		strbuf_addstr(&sb, "../");

	/*
	 * Check if 'path' ends with slash or not
	 * for having the same output for dir/sub_dir
	 * and dir/sub_dir/
	 */
	if (!is_dir_sep(path[strlen(path) - 1]))
		strbuf_addstr(&sb, "../");

	return strbuf_detach(&sb, NULL);
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

		fprintf(stdout, "%s\n", ce->name);
	}
	return 0;
}

static void for_each_listed_submodule(const struct module_list *list,
				      each_submodule_fn fn, void *cb_data)
{
	int i;
	for (i = 0; i < list->nr; i++)
		fn(list->entries[i], cb_data);
}

struct cb_foreach {
	int argc;
	const char **argv;
	const char *prefix;
	int quiet;
	int recursive;
};
#define CB_FOREACH_INIT { 0 }

static void runcommand_in_submodule_cb(const struct cache_entry *list_item,
				       void *cb_data)
{
	struct cb_foreach *info = cb_data;
	const char *path = list_item->name;
	const struct object_id *ce_oid = &list_item->oid;

	const struct submodule *sub;
	struct child_process cp = CHILD_PROCESS_INIT;
	char *displaypath;

	displaypath = get_submodule_displaypath(path, info->prefix);

	sub = submodule_from_path(the_repository, &null_oid, path);

	if (!sub)
		die(_("No url found for submodule path '%s' in .gitmodules"),
			displaypath);

	if (!is_submodule_populated_gently(path, NULL))
		goto cleanup;

	prepare_submodule_repo_env(&cp.env_array);

	/*
	 * For the purpose of executing <command> in the submodule,
	 * separate shell is used for the purpose of running the
	 * child process.
	 */
	cp.use_shell = 1;
	cp.dir = path;

	/*
	 * NEEDSWORK: the command currently has access to the variables $name,
	 * $sm_path, $displaypath, $sha1 and $toplevel only when the command
	 * contains a single argument. This is done for maintaining a faithful
	 * translation from shell script.
	 */
	if (info->argc == 1) {
		char *toplevel = xgetcwd();
		struct strbuf sb = STRBUF_INIT;

		argv_array_pushf(&cp.env_array, "name=%s", sub->name);
		argv_array_pushf(&cp.env_array, "sm_path=%s", path);
		argv_array_pushf(&cp.env_array, "displaypath=%s", displaypath);
		argv_array_pushf(&cp.env_array, "sha1=%s",
				oid_to_hex(ce_oid));
		argv_array_pushf(&cp.env_array, "toplevel=%s", toplevel);

		/*
		 * Since the path variable was accessible from the script
		 * before porting, it is also made available after porting.
		 * The environment variable "PATH" has a very special purpose
		 * on windows. And since environment variables are
		 * case-insensitive in windows, it interferes with the
		 * existing PATH variable. Hence, to avoid that, we expose
		 * path via the args argv_array and not via env_array.
		 */
		sq_quote_buf(&sb, path);
		argv_array_pushf(&cp.args, "path=%s; %s",
				 sb.buf, info->argv[0]);
		strbuf_release(&sb);
		free(toplevel);
	} else {
		argv_array_pushv(&cp.args, info->argv);
	}

	if (!info->quiet)
		printf(_("Entering '%s'\n"), displaypath);

	if (info->argv[0] && run_command(&cp))
		die(_("run_command returned non-zero status for %s\n."),
			displaypath);

	if (info->recursive) {
		struct child_process cpr = CHILD_PROCESS_INIT;

		cpr.git_cmd = 1;
		cpr.dir = path;
		prepare_submodule_repo_env(&cpr.env_array);

		argv_array_pushl(&cpr.args, "--super-prefix", NULL);
		argv_array_pushf(&cpr.args, "%s/", displaypath);
		argv_array_pushl(&cpr.args, "submodule--helper", "foreach", "--recursive",
				NULL);

		if (info->quiet)
			argv_array_push(&cpr.args, "--quiet");

		argv_array_pushv(&cpr.args, info->argv);

		if (run_command(&cpr))
			die(_("run_command returned non-zero status while "
				"recursing in the nested submodules of %s\n."),
				displaypath);
	}

cleanup:
	free(displaypath);
}

static int module_foreach(int argc, const char **argv, const char *prefix)
{
	struct cb_foreach info = CB_FOREACH_INIT;
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;

	struct option module_foreach_options[] = {
		OPT__QUIET(&info.quiet, N_("Suppress output of entering each submodule command")),
		OPT_BOOL(0, "recursive", &info.recursive,
			 N_("Recurse into nested submodules")),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule--helper foreach [--quiet] [--recursive] <command>"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_foreach_options,
			     git_submodule_helper_usage, PARSE_OPT_KEEP_UNKNOWN);

	if (module_list_compute(0, NULL, prefix, &pathspec, &list) < 0)
		return 1;

	info.argc = argc;
	info.argv = argv;
	info.prefix = prefix;

	for_each_listed_submodule(&list, runcommand_in_submodule_cb, &info);

	return 0;
}

struct init_cb {
	const char *prefix;
	unsigned int flags;
};

#define INIT_CB_INIT { NULL, 0 }

static void init_submodule(const char *path, const char *prefix,
			   unsigned int flags)
{
	const struct submodule *sub;
	struct strbuf sb = STRBUF_INIT;
	char *upd = NULL, *url = NULL, *displaypath;

	displaypath = get_submodule_displaypath(path, prefix);

	sub = submodule_from_path(the_repository, &null_oid, path);

	if (!sub)
		die(_("No url found for submodule path '%s' in .gitmodules"),
			displaypath);

	/*
	 * NEEDSWORK: In a multi-working-tree world, this needs to be
	 * set in the per-worktree config.
	 *
	 * Set active flag for the submodule being initialized
	 */
	if (!is_submodule_active(the_repository, path)) {
		strbuf_addf(&sb, "submodule.%s.active", sub->name);
		git_config_set_gently(sb.buf, "true");
		strbuf_reset(&sb);
	}

	/*
	 * Copy url setting when it is not set yet.
	 * To look up the url in .git/config, we must not fall back to
	 * .gitmodules, so look it up directly.
	 */
	strbuf_addf(&sb, "submodule.%s.url", sub->name);
	if (git_config_get_string(sb.buf, &url)) {
		if (!sub->url)
			die(_("No url found for submodule path '%s' in .gitmodules"),
				displaypath);

		url = xstrdup(sub->url);

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
		if (!(flags & OPT_QUIET))
			fprintf(stderr,
				_("Submodule '%s' (%s) registered for path '%s'\n"),
				sub->name, url, displaypath);
	}
	strbuf_reset(&sb);

	/* Copy "update" setting when it is not set yet */
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

static void init_submodule_cb(const struct cache_entry *list_item, void *cb_data)
{
	struct init_cb *info = cb_data;
	init_submodule(list_item->name, info->prefix, info->flags);
}

static int module_init(int argc, const char **argv, const char *prefix)
{
	struct init_cb info = INIT_CB_INIT;
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;

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

	info.prefix = prefix;
	if (quiet)
		info.flags |= OPT_QUIET;

	for_each_listed_submodule(&list, init_submodule_cb, &info);

	return 0;
}

struct status_cb {
	const char *prefix;
	unsigned int flags;
};

#define STATUS_CB_INIT { NULL, 0 }

static void print_status(unsigned int flags, char state, const char *path,
			 const struct object_id *oid, const char *displaypath)
{
	if (flags & OPT_QUIET)
		return;

	printf("%c%s %s", state, oid_to_hex(oid), displaypath);

	if (state == ' ' || state == '+') {
		const char *name = compute_rev_name(path, oid_to_hex(oid));

		if (name)
			printf(" (%s)", name);
	}

	printf("\n");
}

static int handle_submodule_head_ref(const char *refname,
				     const struct object_id *oid, int flags,
				     void *cb_data)
{
	struct object_id *output = cb_data;
	if (oid)
		oidcpy(output, oid);

	return 0;
}

static void status_submodule(const char *path, const struct object_id *ce_oid,
			     unsigned int ce_flags, const char *prefix,
			     unsigned int flags)
{
	char *displaypath;
	struct argv_array diff_files_args = ARGV_ARRAY_INIT;
	struct rev_info rev;
	int diff_files_result;

	if (!submodule_from_path(the_repository, &null_oid, path))
		die(_("no submodule mapping found in .gitmodules for path '%s'"),
		      path);

	displaypath = get_submodule_displaypath(path, prefix);

	if ((CE_STAGEMASK & ce_flags) >> CE_STAGESHIFT) {
		print_status(flags, 'U', path, &null_oid, displaypath);
		goto cleanup;
	}

	if (!is_submodule_active(the_repository, path)) {
		print_status(flags, '-', path, ce_oid, displaypath);
		goto cleanup;
	}

	argv_array_pushl(&diff_files_args, "diff-files",
			 "--ignore-submodules=dirty", "--quiet", "--",
			 path, NULL);

	git_config(git_diff_basic_config, NULL);
	init_revisions(&rev, prefix);
	rev.abbrev = 0;
	diff_files_args.argc = setup_revisions(diff_files_args.argc,
					       diff_files_args.argv,
					       &rev, NULL);
	diff_files_result = run_diff_files(&rev, 0);

	if (!diff_result_code(&rev.diffopt, diff_files_result)) {
		print_status(flags, ' ', path, ce_oid,
			     displaypath);
	} else if (!(flags & OPT_CACHED)) {
		struct object_id oid;
		struct ref_store *refs = get_submodule_ref_store(path);

		if (!refs) {
			print_status(flags, '-', path, ce_oid, displaypath);
			goto cleanup;
		}
		if (refs_head_ref(refs, handle_submodule_head_ref, &oid))
			die(_("could not resolve HEAD ref inside the "
			      "submodule '%s'"), path);

		print_status(flags, '+', path, &oid, displaypath);
	} else {
		print_status(flags, '+', path, ce_oid, displaypath);
	}

	if (flags & OPT_RECURSIVE) {
		struct child_process cpr = CHILD_PROCESS_INIT;

		cpr.git_cmd = 1;
		cpr.dir = path;
		prepare_submodule_repo_env(&cpr.env_array);

		argv_array_push(&cpr.args, "--super-prefix");
		argv_array_pushf(&cpr.args, "%s/", displaypath);
		argv_array_pushl(&cpr.args, "submodule--helper", "status",
				 "--recursive", NULL);

		if (flags & OPT_CACHED)
			argv_array_push(&cpr.args, "--cached");

		if (flags & OPT_QUIET)
			argv_array_push(&cpr.args, "--quiet");

		if (run_command(&cpr))
			die(_("failed to recurse into submodule '%s'"), path);
	}

cleanup:
	argv_array_clear(&diff_files_args);
	free(displaypath);
}

static void status_submodule_cb(const struct cache_entry *list_item,
				void *cb_data)
{
	struct status_cb *info = cb_data;
	status_submodule(list_item->name, &list_item->oid, list_item->ce_flags,
			 info->prefix, info->flags);
}

static int module_status(int argc, const char **argv, const char *prefix)
{
	struct status_cb info = STATUS_CB_INIT;
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;

	struct option module_status_options[] = {
		OPT__QUIET(&quiet, N_("Suppress submodule status output")),
		OPT_BIT(0, "cached", &info.flags, N_("Use commit stored in the index instead of the one stored in the submodule HEAD"), OPT_CACHED),
		OPT_BIT(0, "recursive", &info.flags, N_("recurse into nested submodules"), OPT_RECURSIVE),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule status [--quiet] [--cached] [--recursive] [<path>...]"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_status_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(argc, argv, prefix, &pathspec, &list) < 0)
		return 1;

	info.prefix = prefix;
	if (quiet)
		info.flags |= OPT_QUIET;

	for_each_listed_submodule(&list, status_submodule_cb, &info);

	return 0;
}

static int module_name(int argc, const char **argv, const char *prefix)
{
	const struct submodule *sub;

	if (argc != 2)
		usage(_("git submodule--helper name <path>"));

	sub = submodule_from_path(the_repository, &null_oid, argv[1]);

	if (!sub)
		die(_("no submodule mapping found in .gitmodules for path '%s'"),
		    argv[1]);

	printf("%s\n", sub->name);

	return 0;
}

struct sync_cb {
	const char *prefix;
	unsigned int flags;
};

#define SYNC_CB_INIT { NULL, 0 }

static void sync_submodule(const char *path, const char *prefix,
			   unsigned int flags)
{
	const struct submodule *sub;
	char *remote_key = NULL;
	char *sub_origin_url, *super_config_url, *displaypath;
	struct strbuf sb = STRBUF_INIT;
	struct child_process cp = CHILD_PROCESS_INIT;
	char *sub_config_path = NULL;

	if (!is_submodule_active(the_repository, path))
		return;

	sub = submodule_from_path(the_repository, &null_oid, path);

	if (sub && sub->url) {
		if (starts_with_dot_dot_slash(sub->url) ||
		    starts_with_dot_slash(sub->url)) {
			char *remote_url, *up_path;
			char *remote = get_default_remote();
			strbuf_addf(&sb, "remote.%s.url", remote);

			if (git_config_get_string(sb.buf, &remote_url))
				remote_url = xgetcwd();

			up_path = get_up_path(path);
			sub_origin_url = relative_url(remote_url, sub->url, up_path);
			super_config_url = relative_url(remote_url, sub->url, NULL);

			free(remote);
			free(up_path);
			free(remote_url);
		} else {
			sub_origin_url = xstrdup(sub->url);
			super_config_url = xstrdup(sub->url);
		}
	} else {
		sub_origin_url = xstrdup("");
		super_config_url = xstrdup("");
	}

	displaypath = get_submodule_displaypath(path, prefix);

	if (!(flags & OPT_QUIET))
		printf(_("Synchronizing submodule url for '%s'\n"),
			 displaypath);

	strbuf_reset(&sb);
	strbuf_addf(&sb, "submodule.%s.url", sub->name);
	if (git_config_set_gently(sb.buf, super_config_url))
		die(_("failed to register url for submodule path '%s'"),
		      displaypath);

	if (!is_submodule_populated_gently(path, NULL))
		goto cleanup;

	prepare_submodule_repo_env(&cp.env_array);
	cp.git_cmd = 1;
	cp.dir = path;
	argv_array_pushl(&cp.args, "submodule--helper",
			 "print-default-remote", NULL);

	strbuf_reset(&sb);
	if (capture_command(&cp, &sb, 0))
		die(_("failed to get the default remote for submodule '%s'"),
		      path);

	strbuf_strip_suffix(&sb, "\n");
	remote_key = xstrfmt("remote.%s.url", sb.buf);

	strbuf_reset(&sb);
	submodule_to_gitdir(&sb, path);
	strbuf_addstr(&sb, "/config");

	if (git_config_set_in_file_gently(sb.buf, remote_key, sub_origin_url))
		die(_("failed to update remote for submodule '%s'"),
		      path);

	if (flags & OPT_RECURSIVE) {
		struct child_process cpr = CHILD_PROCESS_INIT;

		cpr.git_cmd = 1;
		cpr.dir = path;
		prepare_submodule_repo_env(&cpr.env_array);

		argv_array_push(&cpr.args, "--super-prefix");
		argv_array_pushf(&cpr.args, "%s/", displaypath);
		argv_array_pushl(&cpr.args, "submodule--helper", "sync",
				 "--recursive", NULL);

		if (flags & OPT_QUIET)
			argv_array_push(&cpr.args, "--quiet");

		if (run_command(&cpr))
			die(_("failed to recurse into submodule '%s'"),
			      path);
	}

cleanup:
	free(super_config_url);
	free(sub_origin_url);
	strbuf_release(&sb);
	free(remote_key);
	free(displaypath);
	free(sub_config_path);
}

static void sync_submodule_cb(const struct cache_entry *list_item, void *cb_data)
{
	struct sync_cb *info = cb_data;
	sync_submodule(list_item->name, info->prefix, info->flags);
}

static int module_sync(int argc, const char **argv, const char *prefix)
{
	struct sync_cb info = SYNC_CB_INIT;
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;
	int recursive = 0;

	struct option module_sync_options[] = {
		OPT__QUIET(&quiet, N_("Suppress output of synchronizing submodule url")),
		OPT_BOOL(0, "recursive", &recursive,
			N_("Recurse into nested submodules")),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule--helper sync [--quiet] [--recursive] [<path>]"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_sync_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(argc, argv, prefix, &pathspec, &list) < 0)
		return 1;

	info.prefix = prefix;
	if (quiet)
		info.flags |= OPT_QUIET;
	if (recursive)
		info.flags |= OPT_RECURSIVE;

	for_each_listed_submodule(&list, sync_submodule_cb, &info);

	return 0;
}

struct deinit_cb {
	const char *prefix;
	unsigned int flags;
};
#define DEINIT_CB_INIT { NULL, 0 }

static void deinit_submodule(const char *path, const char *prefix,
			     unsigned int flags)
{
	const struct submodule *sub;
	char *displaypath = NULL;
	struct child_process cp_config = CHILD_PROCESS_INIT;
	struct strbuf sb_config = STRBUF_INIT;
	char *sub_git_dir = xstrfmt("%s/.git", path);

	sub = submodule_from_path(the_repository, &null_oid, path);

	if (!sub || !sub->name)
		goto cleanup;

	displaypath = get_submodule_displaypath(path, prefix);

	/* remove the submodule work tree (unless the user already did it) */
	if (is_directory(path)) {
		struct strbuf sb_rm = STRBUF_INIT;
		const char *format;

		/*
		 * protect submodules containing a .git directory
		 * NEEDSWORK: instead of dying, automatically call
		 * absorbgitdirs and (possibly) warn.
		 */
		if (is_directory(sub_git_dir))
			die(_("Submodule work tree '%s' contains a .git "
			      "directory (use 'rm -rf' if you really want "
			      "to remove it including all of its history)"),
			    displaypath);

		if (!(flags & OPT_FORCE)) {
			struct child_process cp_rm = CHILD_PROCESS_INIT;
			cp_rm.git_cmd = 1;
			argv_array_pushl(&cp_rm.args, "rm", "-qn",
					 path, NULL);

			if (run_command(&cp_rm))
				die(_("Submodule work tree '%s' contains local "
				      "modifications; use '-f' to discard them"),
				      displaypath);
		}

		strbuf_addstr(&sb_rm, path);

		if (!remove_dir_recursively(&sb_rm, 0))
			format = _("Cleared directory '%s'\n");
		else
			format = _("Could not remove submodule work tree '%s'\n");

		if (!(flags & OPT_QUIET))
			printf(format, displaypath);

		strbuf_release(&sb_rm);
	}

	if (mkdir(path, 0777))
		printf(_("could not create empty submodule directory %s"),
		      displaypath);

	cp_config.git_cmd = 1;
	argv_array_pushl(&cp_config.args, "config", "--get-regexp", NULL);
	argv_array_pushf(&cp_config.args, "submodule.%s\\.", sub->name);

	/* remove the .git/config entries (unless the user already did it) */
	if (!capture_command(&cp_config, &sb_config, 0) && sb_config.len) {
		char *sub_key = xstrfmt("submodule.%s", sub->name);
		/*
		 * remove the whole section so we have a clean state when
		 * the user later decides to init this submodule again
		 */
		git_config_rename_section_in_file(NULL, sub_key, NULL);
		if (!(flags & OPT_QUIET))
			printf(_("Submodule '%s' (%s) unregistered for path '%s'\n"),
				 sub->name, sub->url, displaypath);
		free(sub_key);
	}

cleanup:
	free(displaypath);
	free(sub_git_dir);
	strbuf_release(&sb_config);
}

static void deinit_submodule_cb(const struct cache_entry *list_item,
				void *cb_data)
{
	struct deinit_cb *info = cb_data;
	deinit_submodule(list_item->name, info->prefix, info->flags);
}

static int module_deinit(int argc, const char **argv, const char *prefix)
{
	struct deinit_cb info = DEINIT_CB_INIT;
	struct pathspec pathspec;
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;
	int force = 0;
	int all = 0;

	struct option module_deinit_options[] = {
		OPT__QUIET(&quiet, N_("Suppress submodule status output")),
		OPT__FORCE(&force, N_("Remove submodule working trees even if they contain local changes"), 0),
		OPT_BOOL(0, "all", &all, N_("Unregister all submodules")),
		OPT_END()
	};

	const char *const git_submodule_helper_usage[] = {
		N_("git submodule deinit [--quiet] [-f | --force] [--all | [--] [<path>...]]"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_deinit_options,
			     git_submodule_helper_usage, 0);

	if (all && argc) {
		error("pathspec and --all are incompatible");
		usage_with_options(git_submodule_helper_usage,
				   module_deinit_options);
	}

	if (!argc && !all)
		die(_("Use '--all' if you really want to deinitialize all submodules"));

	if (module_list_compute(argc, argv, prefix, &pathspec, &list) < 0)
		return 1;

	info.prefix = prefix;
	if (quiet)
		info.flags |= OPT_QUIET;
	if (force)
		info.flags |= OPT_FORCE;

	for_each_listed_submodule(&list, deinit_submodule_cb, &info);

	return 0;
}

static int clone_submodule(const char *path, const char *gitdir, const char *url,
			   const char *depth, struct string_list *reference, int dissociate,
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
	if (dissociate)
		argv_array_push(&cp.args, "--dissociate");
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
	int dissociate = 0;
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
		OPT_BOOL(0, "dissociate", &dissociate,
			   N_("use --reference only while cloning")),
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

		if (clone_submodule(path, sm_gitdir, url, depth, &reference, dissociate,
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

	connect_work_tree_and_git_dir(path, sm_gitdir, 0);

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
	int dissociate;
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
	SUBMODULE_UPDATE_STRATEGY_INIT, 0, 0, -1, STRING_LIST_INIT_DUP, 0, \
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
	const char *url = NULL;
	const char *update_string;
	enum submodule_update_type update_type;
	char *key;
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

	sub = submodule_from_path(the_repository, &null_oid, ce->name);

	if (suc->recursive_prefix)
		displaypath = relative_path(suc->recursive_prefix,
					    ce->name, &displaypath_sb);
	else
		displaypath = ce->name;

	if (!sub) {
		next_submodule_warn_missing(suc, out, displaypath);
		goto cleanup;
	}

	key = xstrfmt("submodule.%s.update", sub->name);
	if (!repo_config_get_string_const(the_repository, key, &update_string)) {
		update_type = parse_submodule_update_type(update_string);
	} else {
		update_type = sub->update_strategy.type;
	}
	free(key);

	if (suc->update.type == SM_UPDATE_NONE
	    || (suc->update.type == SM_UPDATE_UNSPECIFIED
		&& update_type == SM_UPDATE_NONE)) {
		strbuf_addf(out, _("Skipping submodule '%s'"), displaypath);
		strbuf_addch(out, '\n');
		goto cleanup;
	}

	/* Check if the submodule has been initialized. */
	if (!is_submodule_active(the_repository, ce->name)) {
		next_submodule_warn_missing(suc, out, displaypath);
		goto cleanup;
	}

	strbuf_reset(&sb);
	strbuf_addf(&sb, "submodule.%s.url", sub->name);
	if (repo_config_get_string_const(the_repository, sb.buf, &url))
		url = sub->url;

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
	argv_array_pushl(&child->args, "--url", url, NULL);
	if (suc->references.nr) {
		struct string_list_item *item;
		for_each_string_list_item(item, &suc->references)
			argv_array_pushl(&child->args, "--reference", item->string, NULL);
	}
	if (suc->dissociate)
		argv_array_push(&child->args, "--dissociate");
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

	int *idxP = idx_task_cb;
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

static int git_update_clone_config(const char *var, const char *value,
				   void *cb)
{
	int *max_jobs = cb;
	if (!strcmp(var, "submodule.fetchjobs"))
		*max_jobs = parse_submodule_fetchjobs(var, value);
	return 0;
}

static int update_clone(int argc, const char **argv, const char *prefix)
{
	const char *update = NULL;
	int max_jobs = 1;
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
		OPT_BOOL(0, "dissociate", &suc.dissociate,
			   N_("use --reference only while cloning")),
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

	update_clone_config_from_gitmodules(&max_jobs);
	git_config(git_update_clone_config, &max_jobs);

	argc = parse_options(argc, argv, prefix, module_update_clone_options,
			     git_submodule_helper_usage, 0);

	if (update)
		if (parse_submodule_update_strategy(update, &suc.update) < 0)
			die(_("bad value for update parameter"));

	if (module_list_compute(argc, argv, prefix, &pathspec, &suc.list) < 0)
		return 1;

	if (pathspec.nr)
		suc.warn_if_uninitialized = 1;

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
		fprintf(stdout, "%s", item->string);

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
	const char *branch = NULL;
	char *key;

	sub = submodule_from_path(the_repository, &null_oid, path);
	if (!sub)
		return NULL;

	key = xstrfmt("submodule.%s.branch", sub->name);
	if (repo_config_get_string_const(the_repository, key, &branch))
		branch = sub->branch;
	free(key);

	if (!branch)
		return "master";

	if (!strcmp(branch, ".")) {
		const char *refname = resolve_ref_unsafe("HEAD", 0, NULL, NULL);

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

	return branch;
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

static int push_check(int argc, const char **argv, const char *prefix)
{
	struct remote *remote;
	const char *superproject_head;
	char *head;
	int detached_head = 0;
	struct object_id head_oid;

	if (argc < 3)
		die("submodule--helper push-check requires at least 2 arguments");

	/*
	 * superproject's resolved head ref.
	 * if HEAD then the superproject is in a detached head state, otherwise
	 * it will be the resolved head ref.
	 */
	superproject_head = argv[1];
	argv++;
	argc--;
	/* Get the submodule's head ref and determine if it is detached */
	head = resolve_refdup("HEAD", 0, &head_oid, NULL);
	if (!head)
		die(_("Failed to resolve HEAD as a valid ref."));
	if (!strcmp(head, "HEAD"))
		detached_head = 1;

	/*
	 * The remote must be configured.
	 * This is to avoid pushing to the exact same URL as the parent.
	 */
	remote = pushremote_get(argv[1]);
	if (!remote || remote->origin == REMOTE_UNCONFIGURED)
		die("remote '%s' not configured", argv[1]);

	/* Check the refspec */
	if (argc > 2) {
		int i;
		struct ref *local_refs = get_local_heads();
		struct refspec refspec = REFSPEC_INIT_PUSH;

		refspec_appendn(&refspec, argv + 2, argc - 2);

		for (i = 0; i < refspec.nr; i++) {
			const struct refspec_item *rs = &refspec.items[i];

			if (rs->pattern || rs->matching)
				continue;

			/* LHS must match a single ref */
			switch (count_refspec_match(rs->src, local_refs, NULL)) {
			case 1:
				break;
			case 0:
				/*
				 * If LHS matches 'HEAD' then we need to ensure
				 * that it matches the same named branch
				 * checked out in the superproject.
				 */
				if (!strcmp(rs->src, "HEAD")) {
					if (!detached_head &&
					    !strcmp(head, superproject_head))
						break;
					die("HEAD does not match the named branch in the superproject");
				}
				/* fallthrough */
			default:
				die("src refspec '%s' must name a ref",
				    rs->src);
			}
		}
		refspec_clear(&refspec);
	}
	free(head);

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
		die("submodule--helper is-active takes exactly 1 argument");

	return !is_submodule_active(the_repository, argv[1]);
}

/*
 * Exit non-zero if any of the submodule names given on the command line is
 * invalid. If no names are given, filter stdin to print only valid names
 * (which is primarily intended for testing).
 */
static int check_name(int argc, const char **argv, const char *prefix)
{
	if (argc > 1) {
		while (*++argv) {
			if (check_submodule_name(*argv) < 0)
				return 1;
		}
	} else {
		struct strbuf buf = STRBUF_INIT;
		while (strbuf_getline(&buf, stdin) != EOF) {
			if (!check_submodule_name(buf.buf))
				printf("%s\n", buf.buf);
		}
		strbuf_release(&buf);
	}
	return 0;
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
	{"foreach", module_foreach, SUPPORT_SUPER_PREFIX},
	{"init", module_init, SUPPORT_SUPER_PREFIX},
	{"status", module_status, SUPPORT_SUPER_PREFIX},
	{"print-default-remote", print_default_remote, 0},
	{"sync", module_sync, SUPPORT_SUPER_PREFIX},
	{"deinit", module_deinit, 0},
	{"remote-branch", resolve_remote_submodule_branch, 0},
	{"push-check", push_check, 0},
	{"absorb-git-dirs", absorb_git_dirs, SUPPORT_SUPER_PREFIX},
	{"is-active", is_active, 0},
	{"check-name", check_name, 0},
};

int cmd_submodule__helper(int argc, const char **argv, const char *prefix)
{
	int i;
	if (argc < 2 || !strcmp(argv[1], "-h"))
		usage("git submodule--helper <command>");

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
