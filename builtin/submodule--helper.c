#define USE_THE_INDEX_VARIABLE
#include "builtin.h"
#include "abspath.h"
#include "alloc.h"
#include "environment.h"
#include "gettext.h"
#include "hex.h"
#include "repository.h"
#include "cache.h"
#include "config.h"
#include "parse-options.h"
#include "quote.h"
#include "pathspec.h"
#include "dir.h"
#include "setup.h"
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
#include "advice.h"
#include "branch.h"
#include "list-objects-filter-options.h"

#define OPT_QUIET (1 << 0)
#define OPT_CACHED (1 << 1)
#define OPT_RECURSIVE (1 << 2)
#define OPT_FORCE (1 << 3)

typedef void (*each_submodule_fn)(const struct cache_entry *list_item,
				  void *cb_data);

static int repo_get_default_remote(struct repository *repo, char **default_remote)
{
	char *dest = NULL;
	struct strbuf sb = STRBUF_INIT;
	struct ref_store *store = get_main_ref_store(repo);
	const char *refname = refs_resolve_ref_unsafe(store, "HEAD", 0, NULL,
						      NULL);

	if (!refname)
		return die_message(_("No such ref: %s"), "HEAD");

	/* detached HEAD */
	if (!strcmp(refname, "HEAD")) {
		*default_remote = xstrdup("origin");
		return 0;
	}

	if (!skip_prefix(refname, "refs/heads/", &refname))
		return die_message(_("Expecting a full ref name, got %s"),
				   refname);

	strbuf_addf(&sb, "branch.%s.remote", refname);
	if (repo_config_get_string(repo, sb.buf, &dest))
		*default_remote = xstrdup("origin");
	else
		*default_remote = dest;

	strbuf_release(&sb);
	return 0;
}

static int get_default_remote_submodule(const char *module_path, char **default_remote)
{
	struct repository subrepo;
	int ret;

	if (repo_submodule_init(&subrepo, the_repository, module_path,
				null_oid()) < 0)
		return die_message(_("could not get a repository handle for submodule '%s'"),
				   module_path);
	ret = repo_get_default_remote(&subrepo, default_remote);
	repo_clear(&subrepo);

	return ret;
}

static char *get_default_remote(void)
{
	char *default_remote;
	int code = repo_get_default_remote(the_repository, &default_remote);

	if (code)
		exit(code);

	return default_remote;
}

static char *resolve_relative_url(const char *rel_url, const char *up_path, int quiet)
{
	char *remoteurl, *resolved_url;
	char *remote = get_default_remote();
	struct strbuf remotesb = STRBUF_INIT;

	strbuf_addf(&remotesb, "remote.%s.url", remote);
	if (git_config_get_string(remotesb.buf, &remoteurl)) {
		if (!quiet)
			warning(_("could not look up configuration '%s'. "
				  "Assuming this repository is its own "
				  "authoritative upstream."),
				remotesb.buf);
		remoteurl = xgetcwd();
	}
	resolved_url = relative_url(remoteurl, rel_url, up_path);

	free(remote);
	free(remoteurl);
	strbuf_release(&remotesb);

	return resolved_url;
}

/* the result should be freed by the caller. */
static char *get_submodule_displaypath(const char *path, const char *prefix,
				       const char *super_prefix)
{
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
		prepare_submodule_repo_env(&cp.env);
		cp.dir = sub_path;
		cp.git_cmd = 1;
		cp.no_stderr = 1;

		strvec_push(&cp.args, "describe");
		strvec_pushv(&cp.args, *d);
		strvec_push(&cp.args, object_id);

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
#define MODULE_LIST_INIT { 0 }

static void module_list_release(struct module_list *ml)
{
	free(ml->entries);
}

static int module_list_compute(const char **argv,
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

	if (repo_read_index(the_repository) < 0)
		die(_("index file corrupt"));

	for (i = 0; i < the_index.cache_nr; i++) {
		const struct cache_entry *ce = the_index.cache[i];

		if (!match_pathspec(&the_index, pathspec, ce->name, ce_namelen(ce),
				    0, ps_matched, 1) ||
		    !S_ISGITLINK(ce->ce_mode))
			continue;

		ALLOC_GROW(list->entries, list->nr + 1, list->alloc);
		list->entries[list->nr++] = ce;
		while (i + 1 < the_index.cache_nr &&
		       !strcmp(ce->name, the_index.cache[i + 1]->name))
			/*
			 * Skip entries with the same name in different stages
			 * to make sure an entry is returned only once.
			 */
			i++;
	}

	if (ps_matched && report_path_error(ps_matched, pathspec))
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

	module_list_release(list);
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

static void for_each_listed_submodule(const struct module_list *list,
				      each_submodule_fn fn, void *cb_data)
{
	int i;

	for (i = 0; i < list->nr; i++)
		fn(list->entries[i], cb_data);
}

struct foreach_cb {
	int argc;
	const char **argv;
	const char *prefix;
	const char *super_prefix;
	int quiet;
	int recursive;
};
#define FOREACH_CB_INIT { 0 }

static void runcommand_in_submodule_cb(const struct cache_entry *list_item,
				       void *cb_data)
{
	struct foreach_cb *info = cb_data;
	const char *path = list_item->name;
	const struct object_id *ce_oid = &list_item->oid;
	const struct submodule *sub;
	struct child_process cp = CHILD_PROCESS_INIT;
	char *displaypath;

	displaypath = get_submodule_displaypath(path, info->prefix,
						info->super_prefix);

	sub = submodule_from_path(the_repository, null_oid(), path);

	if (!sub)
		die(_("No url found for submodule path '%s' in .gitmodules"),
			displaypath);

	if (!is_submodule_populated_gently(path, NULL))
		goto cleanup;

	prepare_submodule_repo_env(&cp.env);

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

		strvec_pushf(&cp.env, "name=%s", sub->name);
		strvec_pushf(&cp.env, "sm_path=%s", path);
		strvec_pushf(&cp.env, "displaypath=%s", displaypath);
		strvec_pushf(&cp.env, "sha1=%s",
			     oid_to_hex(ce_oid));
		strvec_pushf(&cp.env, "toplevel=%s", toplevel);

		/*
		 * Since the path variable was accessible from the script
		 * before porting, it is also made available after porting.
		 * The environment variable "PATH" has a very special purpose
		 * on windows. And since environment variables are
		 * case-insensitive in windows, it interferes with the
		 * existing PATH variable. Hence, to avoid that, we expose
		 * path via the args strvec and not via env.
		 */
		sq_quote_buf(&sb, path);
		strvec_pushf(&cp.args, "path=%s; %s",
			     sb.buf, info->argv[0]);
		strbuf_release(&sb);
		free(toplevel);
	} else {
		strvec_pushv(&cp.args, info->argv);
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
		prepare_submodule_repo_env(&cpr.env);

		strvec_pushl(&cpr.args, "submodule--helper", "foreach", "--recursive",
			     NULL);
		strvec_pushl(&cpr.args, "--super-prefix", NULL);
		strvec_pushf(&cpr.args, "%s/", displaypath);

		if (info->quiet)
			strvec_push(&cpr.args, "--quiet");

		strvec_push(&cpr.args, "--");
		strvec_pushv(&cpr.args, info->argv);

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
	struct foreach_cb info = FOREACH_CB_INIT;
	struct pathspec pathspec = { 0 };
	struct module_list list = MODULE_LIST_INIT;
	struct option module_foreach_options[] = {
		OPT__SUPER_PREFIX(&info.super_prefix),
		OPT__QUIET(&info.quiet, N_("suppress output of entering each submodule command")),
		OPT_BOOL(0, "recursive", &info.recursive,
			 N_("recurse into nested submodules")),
		OPT_END()
	};
	const char *const git_submodule_helper_usage[] = {
		N_("git submodule foreach [--quiet] [--recursive] [--] <command>"),
		NULL
	};
	int ret = 1;

	argc = parse_options(argc, argv, prefix, module_foreach_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(NULL, prefix, &pathspec, &list) < 0)
		goto cleanup;

	info.argc = argc;
	info.argv = argv;
	info.prefix = prefix;

	for_each_listed_submodule(&list, runcommand_in_submodule_cb, &info);

	ret = 0;
cleanup:
	module_list_release(&list);
	clear_pathspec(&pathspec);
	return ret;
}

static int starts_with_dot_slash(const char *const path)
{
	return path_match_flags(path, PATH_MATCH_STARTS_WITH_DOT_SLASH |
				PATH_MATCH_XPLATFORM);
}

static int starts_with_dot_dot_slash(const char *const path)
{
	return path_match_flags(path, PATH_MATCH_STARTS_WITH_DOT_DOT_SLASH |
				PATH_MATCH_XPLATFORM);
}

struct init_cb {
	const char *prefix;
	const char *super_prefix;
	unsigned int flags;
};
#define INIT_CB_INIT { 0 }

static void init_submodule(const char *path, const char *prefix,
			   const char *super_prefix,
			   unsigned int flags)
{
	const struct submodule *sub;
	struct strbuf sb = STRBUF_INIT;
	const char *upd;
	char *url = NULL, *displaypath;

	displaypath = get_submodule_displaypath(path, prefix, super_prefix);

	sub = submodule_from_path(the_repository, null_oid(), path);

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
			char *oldurl = url;

			url = resolve_relative_url(oldurl, NULL, 0);
			free(oldurl);
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
	if (git_config_get_string_tmp(sb.buf, &upd) &&
	    sub->update_strategy.type != SM_UPDATE_UNSPECIFIED) {
		if (sub->update_strategy.type == SM_UPDATE_COMMAND) {
			fprintf(stderr, _("warning: command update mode suggested for submodule '%s'\n"),
				sub->name);
			upd = "none";
		} else {
			upd = submodule_update_type_to_string(sub->update_strategy.type);
		}

		if (git_config_set_gently(sb.buf, upd))
			die(_("Failed to register update mode for submodule path '%s'"), displaypath);
	}
	strbuf_release(&sb);
	free(displaypath);
	free(url);
}

static void init_submodule_cb(const struct cache_entry *list_item, void *cb_data)
{
	struct init_cb *info = cb_data;

	init_submodule(list_item->name, info->prefix, info->super_prefix,
		       info->flags);
}

static int module_init(int argc, const char **argv, const char *prefix)
{
	struct init_cb info = INIT_CB_INIT;
	struct pathspec pathspec = { 0 };
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;
	struct option module_init_options[] = {
		OPT__QUIET(&quiet, N_("suppress output for initializing a submodule")),
		OPT_END()
	};
	const char *const git_submodule_helper_usage[] = {
		N_("git submodule init [<options>] [<path>]"),
		NULL
	};
	int ret = 1;

	argc = parse_options(argc, argv, prefix, module_init_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(argv, prefix, &pathspec, &list) < 0)
		goto cleanup;

	/*
	 * If there are no path args and submodule.active is set then,
	 * by default, only initialize 'active' modules.
	 */
	if (!argc && !git_config_get("submodule.active"))
		module_list_active(&list);

	info.prefix = prefix;
	if (quiet)
		info.flags |= OPT_QUIET;

	for_each_listed_submodule(&list, init_submodule_cb, &info);

	ret = 0;
cleanup:
	module_list_release(&list);
	clear_pathspec(&pathspec);
	return ret;
}

struct status_cb {
	const char *prefix;
	const char *super_prefix;
	unsigned int flags;
};
#define STATUS_CB_INIT { 0 }

static void print_status(unsigned int flags, char state, const char *path,
			 const struct object_id *oid, const char *displaypath)
{
	if (flags & OPT_QUIET)
		return;

	printf("%c%s %s", state, oid_to_hex(oid), displaypath);

	if (state == ' ' || state == '+') {
		char *name = compute_rev_name(path, oid_to_hex(oid));

		if (name)
			printf(" (%s)", name);
		free(name);
	}

	printf("\n");
}

static int handle_submodule_head_ref(const char *refname UNUSED,
				     const struct object_id *oid,
				     int flags UNUSED,
				     void *cb_data)
{
	struct object_id *output = cb_data;

	if (oid)
		oidcpy(output, oid);

	return 0;
}

static void status_submodule(const char *path, const struct object_id *ce_oid,
			     unsigned int ce_flags, const char *prefix,
			     const char *super_prefix, unsigned int flags)
{
	char *displaypath;
	struct strvec diff_files_args = STRVEC_INIT;
	struct rev_info rev = REV_INFO_INIT;
	int diff_files_result;
	struct strbuf buf = STRBUF_INIT;
	const char *git_dir;
	struct setup_revision_opt opt = {
		.free_removed_argv_elements = 1,
	};

	if (!submodule_from_path(the_repository, null_oid(), path))
		die(_("no submodule mapping found in .gitmodules for path '%s'"),
		      path);

	displaypath = get_submodule_displaypath(path, prefix, super_prefix);

	if ((CE_STAGEMASK & ce_flags) >> CE_STAGESHIFT) {
		print_status(flags, 'U', path, null_oid(), displaypath);
		goto cleanup;
	}

	strbuf_addf(&buf, "%s/.git", path);
	git_dir = read_gitfile(buf.buf);
	if (!git_dir)
		git_dir = buf.buf;

	if (!is_submodule_active(the_repository, path) ||
	    !is_git_directory(git_dir)) {
		print_status(flags, '-', path, ce_oid, displaypath);
		strbuf_release(&buf);
		goto cleanup;
	}
	strbuf_release(&buf);

	strvec_pushl(&diff_files_args, "diff-files",
		     "--ignore-submodules=dirty", "--quiet", "--",
		     path, NULL);

	git_config(git_diff_basic_config, NULL);

	repo_init_revisions(the_repository, &rev, NULL);
	rev.abbrev = 0;
	setup_revisions(diff_files_args.nr, diff_files_args.v, &rev, &opt);
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
		prepare_submodule_repo_env(&cpr.env);

		strvec_pushl(&cpr.args, "submodule--helper", "status",
			     "--recursive", NULL);
		strvec_push(&cpr.args, "--super-prefix");
		strvec_pushf(&cpr.args, "%s/", displaypath);

		if (flags & OPT_CACHED)
			strvec_push(&cpr.args, "--cached");

		if (flags & OPT_QUIET)
			strvec_push(&cpr.args, "--quiet");

		if (run_command(&cpr))
			die(_("failed to recurse into submodule '%s'"), path);
	}

cleanup:
	strvec_clear(&diff_files_args);
	free(displaypath);
	release_revisions(&rev);
}

static void status_submodule_cb(const struct cache_entry *list_item,
				void *cb_data)
{
	struct status_cb *info = cb_data;

	status_submodule(list_item->name, &list_item->oid, list_item->ce_flags,
			 info->prefix, info->super_prefix, info->flags);
}

static int module_status(int argc, const char **argv, const char *prefix)
{
	struct status_cb info = STATUS_CB_INIT;
	struct pathspec pathspec = { 0 };
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;
	struct option module_status_options[] = {
		OPT__SUPER_PREFIX(&info.super_prefix),
		OPT__QUIET(&quiet, N_("suppress submodule status output")),
		OPT_BIT(0, "cached", &info.flags, N_("use commit stored in the index instead of the one stored in the submodule HEAD"), OPT_CACHED),
		OPT_BIT(0, "recursive", &info.flags, N_("recurse into nested submodules"), OPT_RECURSIVE),
		OPT_END()
	};
	const char *const git_submodule_helper_usage[] = {
		N_("git submodule status [--quiet] [--cached] [--recursive] [<path>...]"),
		NULL
	};
	int ret = 1;

	argc = parse_options(argc, argv, prefix, module_status_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(argv, prefix, &pathspec, &list) < 0)
		goto cleanup;

	info.prefix = prefix;
	if (quiet)
		info.flags |= OPT_QUIET;

	for_each_listed_submodule(&list, status_submodule_cb, &info);

	ret = 0;
cleanup:
	module_list_release(&list);
	clear_pathspec(&pathspec);
	return ret;
}

struct module_cb {
	unsigned int mod_src;
	unsigned int mod_dst;
	struct object_id oid_src;
	struct object_id oid_dst;
	char status;
	char *sm_path;
};
#define MODULE_CB_INIT { 0 }

static void module_cb_release(struct module_cb *mcb)
{
	free(mcb->sm_path);
}

struct module_cb_list {
	struct module_cb **entries;
	int alloc, nr;
};
#define MODULE_CB_LIST_INIT { 0 }

static void module_cb_list_release(struct module_cb_list *mcbl)
{
	int i;

	for (i = 0; i < mcbl->nr; i++) {
		struct module_cb *mcb = mcbl->entries[i];

		module_cb_release(mcb);
		free(mcb);
	}
	free(mcbl->entries);
}

struct summary_cb {
	int argc;
	const char **argv;
	const char *prefix;
	const char *super_prefix;
	unsigned int cached: 1;
	unsigned int for_status: 1;
	unsigned int files: 1;
	int summary_limit;
};
#define SUMMARY_CB_INIT { 0 }

enum diff_cmd {
	DIFF_INDEX,
	DIFF_FILES
};

static char *verify_submodule_committish(const char *sm_path,
					 const char *committish)
{
	struct child_process cp_rev_parse = CHILD_PROCESS_INIT;
	struct strbuf result = STRBUF_INIT;

	cp_rev_parse.git_cmd = 1;
	cp_rev_parse.dir = sm_path;
	prepare_submodule_repo_env(&cp_rev_parse.env);
	strvec_pushl(&cp_rev_parse.args, "rev-parse", "-q", "--short", NULL);
	strvec_pushf(&cp_rev_parse.args, "%s^0", committish);
	strvec_push(&cp_rev_parse.args, "--");

	if (capture_command(&cp_rev_parse, &result, 0))
		return NULL;

	strbuf_trim_trailing_newline(&result);
	return strbuf_detach(&result, NULL);
}

static void print_submodule_summary(struct summary_cb *info, const char *errmsg,
				    int total_commits, const char *displaypath,
				    const char *src_abbrev, const char *dst_abbrev,
				    struct module_cb *p)
{
	if (p->status == 'T') {
		if (S_ISGITLINK(p->mod_dst))
			printf(_("* %s %s(blob)->%s(submodule)"),
				 displaypath, src_abbrev, dst_abbrev);
		else
			printf(_("* %s %s(submodule)->%s(blob)"),
				 displaypath, src_abbrev, dst_abbrev);
	} else {
		printf("* %s %s...%s",
			displaypath, src_abbrev, dst_abbrev);
	}

	if (total_commits < 0)
		printf(":\n");
	else
		printf(" (%d):\n", total_commits);

	if (errmsg) {
		printf(_("%s"), errmsg);
	} else if (total_commits > 0) {
		struct child_process cp_log = CHILD_PROCESS_INIT;

		cp_log.git_cmd = 1;
		cp_log.dir = p->sm_path;
		prepare_submodule_repo_env(&cp_log.env);
		strvec_pushl(&cp_log.args, "log", NULL);

		if (S_ISGITLINK(p->mod_src) && S_ISGITLINK(p->mod_dst)) {
			if (info->summary_limit > 0)
				strvec_pushf(&cp_log.args, "-%d",
					     info->summary_limit);

			strvec_pushl(&cp_log.args, "--pretty=  %m %s",
				     "--first-parent", NULL);
			strvec_pushf(&cp_log.args, "%s...%s",
				     src_abbrev, dst_abbrev);
		} else if (S_ISGITLINK(p->mod_dst)) {
			strvec_pushl(&cp_log.args, "--pretty=  > %s",
				     "-1", dst_abbrev, NULL);
		} else {
			strvec_pushl(&cp_log.args, "--pretty=  < %s",
				     "-1", src_abbrev, NULL);
		}
		run_command(&cp_log);
	}
	printf("\n");
}

static void generate_submodule_summary(struct summary_cb *info,
				       struct module_cb *p)
{
	char *displaypath, *src_abbrev = NULL, *dst_abbrev;
	int missing_src = 0, missing_dst = 0;
	struct strbuf errmsg = STRBUF_INIT;
	int total_commits = -1;

	if (!info->cached && oideq(&p->oid_dst, null_oid())) {
		if (S_ISGITLINK(p->mod_dst)) {
			struct ref_store *refs = get_submodule_ref_store(p->sm_path);

			if (refs)
				refs_head_ref(refs, handle_submodule_head_ref, &p->oid_dst);
		} else if (S_ISLNK(p->mod_dst) || S_ISREG(p->mod_dst)) {
			struct stat st;
			int fd = open(p->sm_path, O_RDONLY);

			if (fd < 0 || fstat(fd, &st) < 0 ||
			    index_fd(&the_index, &p->oid_dst, fd, &st, OBJ_BLOB,
				     p->sm_path, 0))
				error(_("couldn't hash object from '%s'"), p->sm_path);
		} else {
			/* for a submodule removal (mode:0000000), don't warn */
			if (p->mod_dst)
				warning(_("unexpected mode %o\n"), p->mod_dst);
		}
	}

	if (S_ISGITLINK(p->mod_src)) {
		if (p->status != 'D')
			src_abbrev = verify_submodule_committish(p->sm_path,
								 oid_to_hex(&p->oid_src));
		if (!src_abbrev) {
			missing_src = 1;
			/*
			 * As `rev-parse` failed, we fallback to getting
			 * the abbreviated hash using oid_src. We do
			 * this as we might still need the abbreviated
			 * hash in cases like a submodule type change, etc.
			 */
			src_abbrev = xstrndup(oid_to_hex(&p->oid_src), 7);
		}
	} else {
		/*
		 * The source does not point to a submodule.
		 * So, we fallback to getting the abbreviation using
		 * oid_src as we might still need the abbreviated
		 * hash in cases like submodule add, etc.
		 */
		src_abbrev = xstrndup(oid_to_hex(&p->oid_src), 7);
	}

	if (S_ISGITLINK(p->mod_dst)) {
		dst_abbrev = verify_submodule_committish(p->sm_path,
							 oid_to_hex(&p->oid_dst));
		if (!dst_abbrev) {
			missing_dst = 1;
			/*
			 * As `rev-parse` failed, we fallback to getting
			 * the abbreviated hash using oid_dst. We do
			 * this as we might still need the abbreviated
			 * hash in cases like a submodule type change, etc.
			 */
			dst_abbrev = xstrndup(oid_to_hex(&p->oid_dst), 7);
		}
	} else {
		/*
		 * The destination does not point to a submodule.
		 * So, we fallback to getting the abbreviation using
		 * oid_dst as we might still need the abbreviated
		 * hash in cases like a submodule removal, etc.
		 */
		dst_abbrev = xstrndup(oid_to_hex(&p->oid_dst), 7);
	}

	displaypath = get_submodule_displaypath(p->sm_path, info->prefix,
						info->super_prefix);

	if (!missing_src && !missing_dst) {
		struct child_process cp_rev_list = CHILD_PROCESS_INIT;
		struct strbuf sb_rev_list = STRBUF_INIT;

		strvec_pushl(&cp_rev_list.args, "rev-list",
			     "--first-parent", "--count", NULL);
		if (S_ISGITLINK(p->mod_src) && S_ISGITLINK(p->mod_dst))
			strvec_pushf(&cp_rev_list.args, "%s...%s",
				     src_abbrev, dst_abbrev);
		else
			strvec_push(&cp_rev_list.args, S_ISGITLINK(p->mod_src) ?
				    src_abbrev : dst_abbrev);
		strvec_push(&cp_rev_list.args, "--");

		cp_rev_list.git_cmd = 1;
		cp_rev_list.dir = p->sm_path;
		prepare_submodule_repo_env(&cp_rev_list.env);

		if (!capture_command(&cp_rev_list, &sb_rev_list, 0))
			total_commits = atoi(sb_rev_list.buf);

		strbuf_release(&sb_rev_list);
	} else {
		/*
		 * Don't give error msg for modification whose dst is not
		 * submodule, i.e., deleted or changed to blob
		 */
		if (S_ISGITLINK(p->mod_dst)) {
			if (missing_src && missing_dst) {
				strbuf_addf(&errmsg, "  Warn: %s doesn't contain commits %s and %s\n",
					    displaypath, oid_to_hex(&p->oid_src),
					    oid_to_hex(&p->oid_dst));
			} else {
				strbuf_addf(&errmsg, "  Warn: %s doesn't contain commit %s\n",
					    displaypath, missing_src ?
					    oid_to_hex(&p->oid_src) :
					    oid_to_hex(&p->oid_dst));
			}
		}
	}

	print_submodule_summary(info, errmsg.len ? errmsg.buf : NULL,
				total_commits, displaypath, src_abbrev,
				dst_abbrev, p);

	free(displaypath);
	free(src_abbrev);
	free(dst_abbrev);
	strbuf_release(&errmsg);
}

static void prepare_submodule_summary(struct summary_cb *info,
				      struct module_cb_list *list)
{
	int i;
	for (i = 0; i < list->nr; i++) {
		const struct submodule *sub;
		struct module_cb *p = list->entries[i];
		struct strbuf sm_gitdir = STRBUF_INIT;

		if (p->status == 'D' || p->status == 'T') {
			generate_submodule_summary(info, p);
			continue;
		}

		if (info->for_status && p->status != 'A' &&
		    (sub = submodule_from_path(the_repository,
					       null_oid(), p->sm_path))) {
			char *config_key = NULL;
			const char *value;
			int ignore_all = 0;

			config_key = xstrfmt("submodule.%s.ignore",
					     sub->name);
			if (!git_config_get_string_tmp(config_key, &value))
				ignore_all = !strcmp(value, "all");
			else if (sub->ignore)
				ignore_all = !strcmp(sub->ignore, "all");

			free(config_key);
			if (ignore_all)
				continue;
		}

		/* Also show added or modified modules which are checked out */
		strbuf_addstr(&sm_gitdir, p->sm_path);
		if (is_nonbare_repository_dir(&sm_gitdir))
			generate_submodule_summary(info, p);
		strbuf_release(&sm_gitdir);
	}
}

static void submodule_summary_callback(struct diff_queue_struct *q,
				       struct diff_options *options UNUSED,
				       void *data)
{
	int i;
	struct module_cb_list *list = data;
	for (i = 0; i < q->nr; i++) {
		struct diff_filepair *p = q->queue[i];
		struct module_cb *temp;

		if (!S_ISGITLINK(p->one->mode) && !S_ISGITLINK(p->two->mode))
			continue;
		temp = (struct module_cb*)malloc(sizeof(struct module_cb));
		temp->mod_src = p->one->mode;
		temp->mod_dst = p->two->mode;
		temp->oid_src = p->one->oid;
		temp->oid_dst = p->two->oid;
		temp->status = p->status;
		temp->sm_path = xstrdup(p->one->path);

		ALLOC_GROW(list->entries, list->nr + 1, list->alloc);
		list->entries[list->nr++] = temp;
	}
}

static const char *get_diff_cmd(enum diff_cmd diff_cmd)
{
	switch (diff_cmd) {
	case DIFF_INDEX: return "diff-index";
	case DIFF_FILES: return "diff-files";
	default: BUG("bad diff_cmd value %d", diff_cmd);
	}
}

static int compute_summary_module_list(struct object_id *head_oid,
				       struct summary_cb *info,
				       enum diff_cmd diff_cmd)
{
	struct strvec diff_args = STRVEC_INIT;
	struct rev_info rev;
	struct setup_revision_opt opt = {
		.free_removed_argv_elements = 1,
	};
	struct module_cb_list list = MODULE_CB_LIST_INIT;
	int ret = 0;

	strvec_push(&diff_args, get_diff_cmd(diff_cmd));
	if (info->cached)
		strvec_push(&diff_args, "--cached");
	strvec_pushl(&diff_args, "--ignore-submodules=dirty", "--raw", NULL);
	if (head_oid)
		strvec_push(&diff_args, oid_to_hex(head_oid));
	strvec_push(&diff_args, "--");
	if (info->argc)
		strvec_pushv(&diff_args, info->argv);

	git_config(git_diff_basic_config, NULL);
	repo_init_revisions(the_repository, &rev, info->prefix);
	rev.abbrev = 0;
	precompose_argv_prefix(diff_args.nr, diff_args.v, NULL);
	setup_revisions(diff_args.nr, diff_args.v, &rev, &opt);
	rev.diffopt.output_format = DIFF_FORMAT_NO_OUTPUT | DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = submodule_summary_callback;
	rev.diffopt.format_callback_data = &list;

	if (!info->cached) {
		if (diff_cmd == DIFF_INDEX)
			setup_work_tree();
		if (repo_read_index_preload(the_repository, &rev.diffopt.pathspec, 0) < 0) {
			perror("repo_read_index_preload");
			ret = -1;
			goto cleanup;
		}
	} else if (repo_read_index(the_repository) < 0) {
		perror("repo_read_cache");
		ret = -1;
		goto cleanup;
	}

	if (diff_cmd == DIFF_INDEX)
		run_diff_index(&rev, info->cached);
	else
		run_diff_files(&rev, 0);
	prepare_submodule_summary(info, &list);
cleanup:
	strvec_clear(&diff_args);
	release_revisions(&rev);
	module_cb_list_release(&list);
	return ret;
}

static int module_summary(int argc, const char **argv, const char *prefix)
{
	struct summary_cb info = SUMMARY_CB_INIT;
	int cached = 0;
	int for_status = 0;
	int files = 0;
	int summary_limit = -1;
	enum diff_cmd diff_cmd = DIFF_INDEX;
	struct object_id head_oid;
	int ret;
	struct option module_summary_options[] = {
		OPT_BOOL(0, "cached", &cached,
			 N_("use the commit stored in the index instead of the submodule HEAD")),
		OPT_BOOL(0, "files", &files,
			 N_("compare the commit in the index with that in the submodule HEAD")),
		OPT_BOOL(0, "for-status", &for_status,
			 N_("skip submodules with 'ignore_config' value set to 'all'")),
		OPT_INTEGER('n', "summary-limit", &summary_limit,
			     N_("limit the summary size")),
		OPT_END()
	};
	const char *const git_submodule_helper_usage[] = {
		N_("git submodule summary [<options>] [<commit>] [--] [<path>]"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_summary_options,
			     git_submodule_helper_usage, 0);

	if (!summary_limit)
		return 0;

	if (!repo_get_oid(the_repository, argc ? argv[0] : "HEAD", &head_oid)) {
		if (argc) {
			argv++;
			argc--;
		}
	} else if (!argc || !strcmp(argv[0], "HEAD")) {
		/* before the first commit: compare with an empty tree */
		oidcpy(&head_oid, the_hash_algo->empty_tree);
		if (argc) {
			argv++;
			argc--;
		}
	} else {
		if (repo_get_oid(the_repository, "HEAD", &head_oid))
			die(_("could not fetch a revision for HEAD"));
	}

	if (files) {
		if (cached)
			die(_("options '%s' and '%s' cannot be used together"), "--cached", "--files");
		diff_cmd = DIFF_FILES;
	}

	info.argc = argc;
	info.argv = argv;
	info.prefix = prefix;
	info.cached = !!cached;
	info.files = !!files;
	info.for_status = !!for_status;
	info.summary_limit = summary_limit;

	ret = compute_summary_module_list((diff_cmd == DIFF_INDEX) ? &head_oid : NULL,
					  &info, diff_cmd);
	return ret;
}

struct sync_cb {
	const char *prefix;
	const char *super_prefix;
	unsigned int flags;
};
#define SYNC_CB_INIT { 0 }

static void sync_submodule(const char *path, const char *prefix,
			   const char *super_prefix, unsigned int flags)
{
	const struct submodule *sub;
	char *remote_key = NULL;
	char *sub_origin_url, *super_config_url, *displaypath, *default_remote;
	struct strbuf sb = STRBUF_INIT;
	char *sub_config_path = NULL;
	int code;

	if (!is_submodule_active(the_repository, path))
		return;

	sub = submodule_from_path(the_repository, null_oid(), path);

	if (sub && sub->url) {
		if (starts_with_dot_dot_slash(sub->url) ||
		    starts_with_dot_slash(sub->url)) {
			char *up_path = get_up_path(path);

			sub_origin_url = resolve_relative_url(sub->url, up_path, 1);
			super_config_url = resolve_relative_url(sub->url, NULL, 1);
			free(up_path);
		} else {
			sub_origin_url = xstrdup(sub->url);
			super_config_url = xstrdup(sub->url);
		}
	} else {
		sub_origin_url = xstrdup("");
		super_config_url = xstrdup("");
	}

	displaypath = get_submodule_displaypath(path, prefix, super_prefix);

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

	strbuf_reset(&sb);
	code = get_default_remote_submodule(path, &default_remote);
	if (code)
		exit(code);

	remote_key = xstrfmt("remote.%s.url", default_remote);
	free(default_remote);

	submodule_to_gitdir(&sb, path);
	strbuf_addstr(&sb, "/config");

	if (git_config_set_in_file_gently(sb.buf, remote_key, sub_origin_url))
		die(_("failed to update remote for submodule '%s'"),
		      path);

	if (flags & OPT_RECURSIVE) {
		struct child_process cpr = CHILD_PROCESS_INIT;

		cpr.git_cmd = 1;
		cpr.dir = path;
		prepare_submodule_repo_env(&cpr.env);

		strvec_pushl(&cpr.args, "submodule--helper", "sync",
			     "--recursive", NULL);
		strvec_push(&cpr.args, "--super-prefix");
		strvec_pushf(&cpr.args, "%s/", displaypath);


		if (flags & OPT_QUIET)
			strvec_push(&cpr.args, "--quiet");

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

	sync_submodule(list_item->name, info->prefix, info->super_prefix,
		       info->flags);
}

static int module_sync(int argc, const char **argv, const char *prefix)
{
	struct sync_cb info = SYNC_CB_INIT;
	struct pathspec pathspec = { 0 };
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;
	int recursive = 0;
	struct option module_sync_options[] = {
		OPT__SUPER_PREFIX(&info.super_prefix),
		OPT__QUIET(&quiet, N_("suppress output of synchronizing submodule url")),
		OPT_BOOL(0, "recursive", &recursive,
			N_("recurse into nested submodules")),
		OPT_END()
	};
	const char *const git_submodule_helper_usage[] = {
		N_("git submodule sync [--quiet] [--recursive] [<path>]"),
		NULL
	};
	int ret = 1;

	argc = parse_options(argc, argv, prefix, module_sync_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(argv, prefix, &pathspec, &list) < 0)
		goto cleanup;

	info.prefix = prefix;
	if (quiet)
		info.flags |= OPT_QUIET;
	if (recursive)
		info.flags |= OPT_RECURSIVE;

	for_each_listed_submodule(&list, sync_submodule_cb, &info);

	ret = 0;
cleanup:
	module_list_release(&list);
	clear_pathspec(&pathspec);
	return ret;
}

struct deinit_cb {
	const char *prefix;
	unsigned int flags;
};
#define DEINIT_CB_INIT { 0 }

static void deinit_submodule(const char *path, const char *prefix,
			     unsigned int flags)
{
	const struct submodule *sub;
	char *displaypath = NULL;
	struct child_process cp_config = CHILD_PROCESS_INIT;
	struct strbuf sb_config = STRBUF_INIT;
	char *sub_git_dir = xstrfmt("%s/.git", path);

	sub = submodule_from_path(the_repository, null_oid(), path);

	if (!sub || !sub->name)
		goto cleanup;

	displaypath = get_submodule_displaypath(path, prefix, NULL);

	/* remove the submodule work tree (unless the user already did it) */
	if (is_directory(path)) {
		struct strbuf sb_rm = STRBUF_INIT;
		const char *format;

		if (is_directory(sub_git_dir)) {
			if (!(flags & OPT_QUIET))
				warning(_("Submodule work tree '%s' contains a .git "
					  "directory. This will be replaced with a "
					  ".git file by using absorbgitdirs."),
					displaypath);

			absorb_git_dir_into_superproject(path, NULL);

		}

		if (!(flags & OPT_FORCE)) {
			struct child_process cp_rm = CHILD_PROCESS_INIT;

			cp_rm.git_cmd = 1;
			strvec_pushl(&cp_rm.args, "rm", "-qn",
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

		submodule_unset_core_worktree(sub);

		strbuf_release(&sb_rm);
	}

	if (mkdir(path, 0777))
		printf(_("could not create empty submodule directory %s"),
		      displaypath);

	cp_config.git_cmd = 1;
	strvec_pushl(&cp_config.args, "config", "--get-regexp", NULL);
	strvec_pushf(&cp_config.args, "submodule.%s\\.", sub->name);

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
	struct pathspec pathspec = { 0 };
	struct module_list list = MODULE_LIST_INIT;
	int quiet = 0;
	int force = 0;
	int all = 0;
	struct option module_deinit_options[] = {
		OPT__QUIET(&quiet, N_("suppress submodule status output")),
		OPT__FORCE(&force, N_("remove submodule working trees even if they contain local changes"), 0),
		OPT_BOOL(0, "all", &all, N_("unregister all submodules")),
		OPT_END()
	};
	const char *const git_submodule_helper_usage[] = {
		N_("git submodule deinit [--quiet] [-f | --force] [--all | [--] [<path>...]]"),
		NULL
	};
	int ret = 1;

	argc = parse_options(argc, argv, prefix, module_deinit_options,
			     git_submodule_helper_usage, 0);

	if (all && argc) {
		error("pathspec and --all are incompatible");
		usage_with_options(git_submodule_helper_usage,
				   module_deinit_options);
	}

	if (!argc && !all)
		die(_("Use '--all' if you really want to deinitialize all submodules"));

	if (module_list_compute(argv, prefix, &pathspec, &list) < 0)
		goto cleanup;

	info.prefix = prefix;
	if (quiet)
		info.flags |= OPT_QUIET;
	if (force)
		info.flags |= OPT_FORCE;

	for_each_listed_submodule(&list, deinit_submodule_cb, &info);

	ret = 0;
cleanup:
	module_list_release(&list);
	clear_pathspec(&pathspec);
	return ret;
}

struct module_clone_data {
	const char *prefix;
	const char *path;
	const char *name;
	const char *url;
	const char *depth;
	struct list_objects_filter_options *filter_options;
	unsigned int quiet: 1;
	unsigned int progress: 1;
	unsigned int dissociate: 1;
	unsigned int require_init: 1;
	int single_branch;
};
#define MODULE_CLONE_DATA_INIT { \
	.single_branch = -1, \
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
#define SUBMODULE_ALTERNATE_SETUP_INIT { \
	.error_mode = SUBMODULE_ALTERNATE_ERROR_IGNORE, \
}

static const char alternate_error_advice[] = N_(
"An alternate computed from a superproject's alternate is invalid.\n"
"To allow Git to clone without an alternate in such a case, set\n"
"submodule.alternateErrorStrategy to 'info' or, equivalently, clone with\n"
"'--reference-if-able' instead of '--reference'."
);

static int add_possible_reference_from_superproject(
		struct object_directory *odb, void *sas_cb)
{
	struct submodule_alternate_setup *sas = sas_cb;
	size_t len;

	/*
	 * If the alternate object store is another repository, try the
	 * standard layout with .git/(modules/<name>)+/objects
	 */
	if (strip_suffix(odb->path, "/objects", &len)) {
		struct repository alternate;
		char *sm_alternate;
		struct strbuf sb = STRBUF_INIT;
		struct strbuf err = STRBUF_INIT;
		strbuf_add(&sb, odb->path, len);

		if (repo_init(&alternate, sb.buf, NULL) < 0)
			die(_("could not get a repository handle for gitdir '%s'"),
			    sb.buf);

		/*
		 * We need to end the new path with '/' to mark it as a dir,
		 * otherwise a submodule name containing '/' will be broken
		 * as the last part of a missing submodule reference would
		 * be taken as a file name.
		 */
		strbuf_reset(&sb);
		submodule_name_to_gitdir(&sb, &alternate, sas->submodule_name);
		strbuf_addch(&sb, '/');
		repo_clear(&alternate);

		sm_alternate = compute_alternate_path(sb.buf, &err);
		if (sm_alternate) {
			char *p = strbuf_detach(&sb, NULL);

			string_list_append(sas->reference, p)->util = p;
			free(sm_alternate);
		} else {
			switch (sas->error_mode) {
			case SUBMODULE_ALTERNATE_ERROR_DIE:
				if (advice_enabled(ADVICE_SUBMODULE_ALTERNATE_ERROR_STRATEGY_DIE))
					advise(_(alternate_error_advice));
				die(_("submodule '%s' cannot add alternate: %s"),
				    sas->submodule_name, err.buf);
			case SUBMODULE_ALTERNATE_ERROR_INFO:
				fprintf_ln(stderr, _("submodule '%s' cannot add alternate: %s"),
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

static char *clone_submodule_sm_gitdir(const char *name)
{
	struct strbuf sb = STRBUF_INIT;
	char *sm_gitdir;

	submodule_name_to_gitdir(&sb, the_repository, name);
	sm_gitdir = absolute_pathdup(sb.buf);
	strbuf_release(&sb);

	return sm_gitdir;
}

static int clone_submodule(const struct module_clone_data *clone_data,
			   struct string_list *reference)
{
	char *p;
	char *sm_gitdir = clone_submodule_sm_gitdir(clone_data->name);
	char *sm_alternate = NULL, *error_strategy = NULL;
	struct child_process cp = CHILD_PROCESS_INIT;
	const char *clone_data_path = clone_data->path;
	char *to_free = NULL;

	if (!is_absolute_path(clone_data->path))
		clone_data_path = to_free = xstrfmt("%s/%s", get_git_work_tree(),
						    clone_data->path);

	if (validate_submodule_git_dir(sm_gitdir, clone_data->name) < 0)
		die(_("refusing to create/use '%s' in another submodule's "
		      "git dir"), sm_gitdir);

	if (!file_exists(sm_gitdir)) {
		if (safe_create_leading_directories_const(sm_gitdir) < 0)
			die(_("could not create directory '%s'"), sm_gitdir);

		prepare_possible_alternates(clone_data->name, reference);

		strvec_push(&cp.args, "clone");
		strvec_push(&cp.args, "--no-checkout");
		if (clone_data->quiet)
			strvec_push(&cp.args, "--quiet");
		if (clone_data->progress)
			strvec_push(&cp.args, "--progress");
		if (clone_data->depth && *(clone_data->depth))
			strvec_pushl(&cp.args, "--depth", clone_data->depth, NULL);
		if (reference->nr) {
			struct string_list_item *item;

			for_each_string_list_item(item, reference)
				strvec_pushl(&cp.args, "--reference",
					     item->string, NULL);
		}
		if (clone_data->dissociate)
			strvec_push(&cp.args, "--dissociate");
		if (sm_gitdir && *sm_gitdir)
			strvec_pushl(&cp.args, "--separate-git-dir", sm_gitdir, NULL);
		if (clone_data->filter_options && clone_data->filter_options->choice)
			strvec_pushf(&cp.args, "--filter=%s",
				     expand_list_objects_filter_spec(
					     clone_data->filter_options));
		if (clone_data->single_branch >= 0)
			strvec_push(&cp.args, clone_data->single_branch ?
				    "--single-branch" :
				    "--no-single-branch");

		strvec_push(&cp.args, "--");
		strvec_push(&cp.args, clone_data->url);
		strvec_push(&cp.args, clone_data_path);

		cp.git_cmd = 1;
		prepare_submodule_repo_env(&cp.env);
		cp.no_stdin = 1;

		if(run_command(&cp))
			die(_("clone of '%s' into submodule path '%s' failed"),
			    clone_data->url, clone_data_path);
	} else {
		char *path;

		if (clone_data->require_init && !access(clone_data_path, X_OK) &&
		    !is_empty_dir(clone_data_path))
			die(_("directory not empty: '%s'"), clone_data_path);
		if (safe_create_leading_directories_const(clone_data_path) < 0)
			die(_("could not create directory '%s'"), clone_data_path);
		path = xstrfmt("%s/index", sm_gitdir);
		unlink_or_warn(path);
		free(path);
	}

	connect_work_tree_and_git_dir(clone_data_path, sm_gitdir, 0);

	p = git_pathdup_submodule(clone_data_path, "config");
	if (!p)
		die(_("could not get submodule directory for '%s'"), clone_data_path);

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

	free(sm_gitdir);
	free(p);
	free(to_free);
	return 0;
}

static int module_clone(int argc, const char **argv, const char *prefix)
{
	int dissociate = 0, quiet = 0, progress = 0, require_init = 0;
	struct module_clone_data clone_data = MODULE_CLONE_DATA_INIT;
	struct string_list reference = STRING_LIST_INIT_NODUP;
	struct list_objects_filter_options filter_options =
		LIST_OBJECTS_FILTER_INIT;

	struct option module_clone_options[] = {
		OPT_STRING(0, "prefix", &clone_data.prefix,
			   N_("path"),
			   N_("alternative anchor for relative paths")),
		OPT_STRING(0, "path", &clone_data.path,
			   N_("path"),
			   N_("where the new submodule will be cloned to")),
		OPT_STRING(0, "name", &clone_data.name,
			   N_("string"),
			   N_("name of the new submodule")),
		OPT_STRING(0, "url", &clone_data.url,
			   N_("string"),
			   N_("url where to clone the submodule from")),
		OPT_STRING_LIST(0, "reference", &reference,
			   N_("repo"),
			   N_("reference repository")),
		OPT_BOOL(0, "dissociate", &dissociate,
			   N_("use --reference only while cloning")),
		OPT_STRING(0, "depth", &clone_data.depth,
			   N_("string"),
			   N_("depth for shallow clones")),
		OPT__QUIET(&quiet, "suppress output for cloning a submodule"),
		OPT_BOOL(0, "progress", &progress,
			   N_("force cloning progress")),
		OPT_BOOL(0, "require-init", &require_init,
			   N_("disallow cloning into non-empty directory")),
		OPT_BOOL(0, "single-branch", &clone_data.single_branch,
			 N_("clone only one branch, HEAD or --branch")),
		OPT_PARSE_LIST_OBJECTS_FILTER(&filter_options),
		OPT_END()
	};
	const char *const git_submodule_helper_usage[] = {
		N_("git submodule--helper clone [--prefix=<path>] [--quiet] "
		   "[--reference <repository>] [--name <name>] [--depth <depth>] "
		   "[--single-branch] [--filter <filter-spec>] "
		   "--url <url> --path <path>"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, module_clone_options,
			     git_submodule_helper_usage, 0);

	clone_data.dissociate = !!dissociate;
	clone_data.quiet = !!quiet;
	clone_data.progress = !!progress;
	clone_data.require_init = !!require_init;
	clone_data.filter_options = &filter_options;

	if (argc || !clone_data.url || !clone_data.path || !*(clone_data.path))
		usage_with_options(git_submodule_helper_usage,
				   module_clone_options);

	clone_submodule(&clone_data, &reference);
	list_objects_filter_release(&filter_options);
	string_list_clear(&reference, 1);
	return 0;
}

static int determine_submodule_update_strategy(struct repository *r,
					       int just_cloned,
					       const char *path,
					       enum submodule_update_type update,
					       struct submodule_update_strategy *out)
{
	const struct submodule *sub = submodule_from_path(r, null_oid(), path);
	char *key;
	const char *val;
	int ret;

	key = xstrfmt("submodule.%s.update", sub->name);

	if (update) {
		out->type = update;
	} else if (!repo_config_get_string_tmp(r, key, &val)) {
		if (parse_submodule_update_strategy(val, out) < 0) {
			ret = die_message(_("Invalid update mode '%s' configured for submodule path '%s'"),
					  val, path);
			goto cleanup;
		}
	} else if (sub->update_strategy.type != SM_UPDATE_UNSPECIFIED) {
		if (sub->update_strategy.type == SM_UPDATE_COMMAND)
			BUG("how did we read update = !command from .gitmodules?");
		out->type = sub->update_strategy.type;
		out->command = sub->update_strategy.command;
	} else
		out->type = SM_UPDATE_CHECKOUT;

	if (just_cloned &&
	    (out->type == SM_UPDATE_MERGE ||
	     out->type == SM_UPDATE_REBASE ||
	     out->type == SM_UPDATE_NONE))
		out->type = SM_UPDATE_CHECKOUT;

	ret = 0;
cleanup:
	free(key);
	return ret;
}

struct update_clone_data {
	const struct submodule *sub;
	struct object_id oid;
	unsigned just_cloned;
};

struct submodule_update_clone {
	/* index into 'update_data.list', the list of submodules to look into for cloning */
	int current;

	/* configuration parameters which are passed on to the children */
	const struct update_data *update_data;

	/* to be consumed by update_submodule() */
	struct update_clone_data *update_clone;
	int update_clone_nr; int update_clone_alloc;

	/* If we want to stop as fast as possible and return an error */
	unsigned quickstop : 1;

	/* failed clones to be retried again */
	const struct cache_entry **failed_clones;
	int failed_clones_nr, failed_clones_alloc;
};
#define SUBMODULE_UPDATE_CLONE_INIT { 0 }

static void submodule_update_clone_release(struct submodule_update_clone *suc)
{
	free(suc->update_clone);
	free(suc->failed_clones);
}

struct update_data {
	const char *prefix;
	const char *super_prefix;
	char *displaypath;
	enum submodule_update_type update_default;
	struct object_id suboid;
	struct string_list references;
	struct submodule_update_strategy update_strategy;
	struct list_objects_filter_options *filter_options;
	struct module_list list;
	int depth;
	int max_jobs;
	int single_branch;
	int recommend_shallow;
	unsigned int require_init;
	unsigned int force;
	unsigned int quiet;
	unsigned int nofetch;
	unsigned int remote;
	unsigned int progress;
	unsigned int dissociate;
	unsigned int init;
	unsigned int warn_if_uninitialized;
	unsigned int recursive;

	/* copied over from update_clone_data */
	struct object_id oid;
	unsigned int just_cloned;
	const char *sm_path;
};
#define UPDATE_DATA_INIT { \
	.update_strategy = SUBMODULE_UPDATE_STRATEGY_INIT, \
	.list = MODULE_LIST_INIT, \
	.recommend_shallow = -1, \
	.references = STRING_LIST_INIT_DUP, \
	.single_branch = -1, \
	.max_jobs = 1, \
}

static void update_data_release(struct update_data *ud)
{
	free(ud->displaypath);
	module_list_release(&ud->list);
}

static void next_submodule_warn_missing(struct submodule_update_clone *suc,
		struct strbuf *out, const char *displaypath)
{
	/*
	 * Only mention uninitialized submodules when their
	 * paths have been specified.
	 */
	if (suc->update_data->warn_if_uninitialized) {
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
	const struct update_data *ud = suc->update_data;
	char *displaypath = get_submodule_displaypath(ce->name, ud->prefix,
						      ud->super_prefix);
	struct strbuf sb = STRBUF_INIT;
	int needs_cloning = 0;
	int need_free_url = 0;

	if (ce_stage(ce)) {
		strbuf_addf(out, _("Skipping unmerged submodule %s"), displaypath);
		strbuf_addch(out, '\n');
		goto cleanup;
	}

	sub = submodule_from_path(the_repository, null_oid(), ce->name);

	if (!sub) {
		next_submodule_warn_missing(suc, out, displaypath);
		goto cleanup;
	}

	key = xstrfmt("submodule.%s.update", sub->name);
	if (!repo_config_get_string_tmp(the_repository, key, &update_string)) {
		update_type = parse_submodule_update_type(update_string);
	} else {
		update_type = sub->update_strategy.type;
	}
	free(key);

	if (suc->update_data->update_strategy.type == SM_UPDATE_NONE
	    || (suc->update_data->update_strategy.type == SM_UPDATE_UNSPECIFIED
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
	if (repo_config_get_string_tmp(the_repository, sb.buf, &url)) {
		if (starts_with_dot_slash(sub->url) ||
		    starts_with_dot_dot_slash(sub->url)) {
			url = resolve_relative_url(sub->url, NULL, 0);
			need_free_url = 1;
		} else
			url = sub->url;
	}

	strbuf_reset(&sb);
	strbuf_addf(&sb, "%s/.git", ce->name);
	needs_cloning = !file_exists(sb.buf);

	ALLOC_GROW(suc->update_clone, suc->update_clone_nr + 1,
		   suc->update_clone_alloc);
	oidcpy(&suc->update_clone[suc->update_clone_nr].oid, &ce->oid);
	suc->update_clone[suc->update_clone_nr].just_cloned = needs_cloning;
	suc->update_clone[suc->update_clone_nr].sub = sub;
	suc->update_clone_nr++;

	if (!needs_cloning)
		goto cleanup;

	child->git_cmd = 1;
	child->no_stdin = 1;
	child->stdout_to_stderr = 1;
	child->err = -1;
	strvec_push(&child->args, "submodule--helper");
	strvec_push(&child->args, "clone");
	if (suc->update_data->progress)
		strvec_push(&child->args, "--progress");
	if (suc->update_data->quiet)
		strvec_push(&child->args, "--quiet");
	if (suc->update_data->prefix)
		strvec_pushl(&child->args, "--prefix", suc->update_data->prefix, NULL);
	if (suc->update_data->recommend_shallow && sub->recommend_shallow == 1)
		strvec_push(&child->args, "--depth=1");
	else if (suc->update_data->depth)
		strvec_pushf(&child->args, "--depth=%d", suc->update_data->depth);
	if (suc->update_data->filter_options && suc->update_data->filter_options->choice)
		strvec_pushf(&child->args, "--filter=%s",
			     expand_list_objects_filter_spec(suc->update_data->filter_options));
	if (suc->update_data->require_init)
		strvec_push(&child->args, "--require-init");
	strvec_pushl(&child->args, "--path", sub->path, NULL);
	strvec_pushl(&child->args, "--name", sub->name, NULL);
	strvec_pushl(&child->args, "--url", url, NULL);
	if (suc->update_data->references.nr) {
		struct string_list_item *item;

		for_each_string_list_item(item, &suc->update_data->references)
			strvec_pushl(&child->args, "--reference", item->string, NULL);
	}
	if (suc->update_data->dissociate)
		strvec_push(&child->args, "--dissociate");
	if (suc->update_data->single_branch >= 0)
		strvec_push(&child->args, suc->update_data->single_branch ?
					      "--single-branch" :
					      "--no-single-branch");

cleanup:
	free(displaypath);
	strbuf_release(&sb);
	if (need_free_url)
		free((void*)url);

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

	for (; suc->current < suc->update_data->list.nr; suc->current++) {
		ce = suc->update_data->list.entries[suc->current];
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
	index = suc->current - suc->update_data->list.nr;
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

static int update_clone_start_failure(struct strbuf *err UNUSED,
				      void *suc_cb,
				      void *idx_task_cb UNUSED)
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

	if (idx < suc->update_data->list.nr) {
		ce  = suc->update_data->list.entries[idx];
		strbuf_addf(err, _("Failed to clone '%s'. Retry scheduled"),
			    ce->name);
		strbuf_addch(err, '\n');
		ALLOC_GROW(suc->failed_clones,
			   suc->failed_clones_nr + 1,
			   suc->failed_clones_alloc);
		suc->failed_clones[suc->failed_clones_nr++] = ce;
		return 0;
	} else {
		idx -= suc->update_data->list.nr;
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

static int is_tip_reachable(const char *path, const struct object_id *oid)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf rev = STRBUF_INIT;
	char *hex = oid_to_hex(oid);

	cp.git_cmd = 1;
	cp.dir = path;
	cp.no_stderr = 1;
	strvec_pushl(&cp.args, "rev-list", "-n", "1", hex, "--not", "--all", NULL);

	prepare_submodule_repo_env(&cp.env);

	if (capture_command(&cp, &rev, GIT_MAX_HEXSZ + 1) || rev.len)
		return 0;

	return 1;
}

static int fetch_in_submodule(const char *module_path, int depth, int quiet,
			      const struct object_id *oid)
{
	struct child_process cp = CHILD_PROCESS_INIT;

	prepare_submodule_repo_env(&cp.env);
	cp.git_cmd = 1;
	cp.dir = module_path;

	strvec_push(&cp.args, "fetch");
	if (quiet)
		strvec_push(&cp.args, "--quiet");
	if (depth)
		strvec_pushf(&cp.args, "--depth=%d", depth);
	if (oid) {
		char *hex = oid_to_hex(oid);
		char *remote = get_default_remote();

		strvec_pushl(&cp.args, remote, hex, NULL);
		free(remote);
	}

	return run_command(&cp);
}

static int run_update_command(const struct update_data *ud, int subforce)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	char *oid = oid_to_hex(&ud->oid);
	int ret;

	switch (ud->update_strategy.type) {
	case SM_UPDATE_CHECKOUT:
		cp.git_cmd = 1;
		strvec_pushl(&cp.args, "checkout", "-q", NULL);
		if (subforce)
			strvec_push(&cp.args, "-f");
		break;
	case SM_UPDATE_REBASE:
		cp.git_cmd = 1;
		strvec_push(&cp.args, "rebase");
		if (ud->quiet)
			strvec_push(&cp.args, "--quiet");
		break;
	case SM_UPDATE_MERGE:
		cp.git_cmd = 1;
		strvec_push(&cp.args, "merge");
		if (ud->quiet)
			strvec_push(&cp.args, "--quiet");
		break;
	case SM_UPDATE_COMMAND:
		cp.use_shell = 1;
		strvec_push(&cp.args, ud->update_strategy.command);
		break;
	default:
		BUG("unexpected update strategy type: %d",
		    ud->update_strategy.type);
	}
	strvec_push(&cp.args, oid);

	cp.dir = ud->sm_path;
	prepare_submodule_repo_env(&cp.env);
	if ((ret = run_command(&cp))) {
		switch (ud->update_strategy.type) {
		case SM_UPDATE_CHECKOUT:
			die_message(_("Unable to checkout '%s' in submodule path '%s'"),
				    oid, ud->displaypath);
			/* No "ret" assignment, use "git checkout"'s */
			break;
		case SM_UPDATE_REBASE:
			ret = die_message(_("Unable to rebase '%s' in submodule path '%s'"),
					  oid, ud->displaypath);
			break;
		case SM_UPDATE_MERGE:
			ret = die_message(_("Unable to merge '%s' in submodule path '%s'"),
					  oid, ud->displaypath);
			break;
		case SM_UPDATE_COMMAND:
			ret = die_message(_("Execution of '%s %s' failed in submodule path '%s'"),
					  ud->update_strategy.command, oid, ud->displaypath);
			break;
		default:
			BUG("unexpected update strategy type: %d",
			    ud->update_strategy.type);
		}

		return ret;
	}

	if (ud->quiet)
		return 0;

	switch (ud->update_strategy.type) {
	case SM_UPDATE_CHECKOUT:
		printf(_("Submodule path '%s': checked out '%s'\n"),
		       ud->displaypath, oid);
		break;
	case SM_UPDATE_REBASE:
		printf(_("Submodule path '%s': rebased into '%s'\n"),
		       ud->displaypath, oid);
		break;
	case SM_UPDATE_MERGE:
		printf(_("Submodule path '%s': merged in '%s'\n"),
		       ud->displaypath, oid);
		break;
	case SM_UPDATE_COMMAND:
		printf(_("Submodule path '%s': '%s %s'\n"),
		       ud->displaypath, ud->update_strategy.command, oid);
		break;
	default:
		BUG("unexpected update strategy type: %d",
		    ud->update_strategy.type);
	}

	return 0;
}

static int run_update_procedure(const struct update_data *ud)
{
	int subforce = is_null_oid(&ud->suboid) || ud->force;

	if (!ud->nofetch) {
		/*
		 * Run fetch only if `oid` isn't present or it
		 * is not reachable from a ref.
		 */
		if (!is_tip_reachable(ud->sm_path, &ud->oid) &&
		    fetch_in_submodule(ud->sm_path, ud->depth, ud->quiet, NULL) &&
		    !ud->quiet)
			fprintf_ln(stderr,
				   _("Unable to fetch in submodule path '%s'; "
				     "trying to directly fetch %s:"),
				   ud->displaypath, oid_to_hex(&ud->oid));
		/*
		 * Now we tried the usual fetch, but `oid` may
		 * not be reachable from any of the refs.
		 */
		if (!is_tip_reachable(ud->sm_path, &ud->oid) &&
		    fetch_in_submodule(ud->sm_path, ud->depth, ud->quiet, &ud->oid))
			return die_message(_("Fetched in submodule path '%s', but it did not "
					     "contain %s. Direct fetching of that commit failed."),
					   ud->displaypath, oid_to_hex(&ud->oid));
	}

	return run_update_command(ud, subforce);
}

static int remote_submodule_branch(const char *path, const char **branch)
{
	const struct submodule *sub;
	char *key;
	*branch = NULL;

	sub = submodule_from_path(the_repository, null_oid(), path);
	if (!sub)
		return die_message(_("could not initialize submodule at path '%s'"),
				   path);

	key = xstrfmt("submodule.%s.branch", sub->name);
	if (repo_config_get_string_tmp(the_repository, key, branch))
		*branch = sub->branch;
	free(key);

	if (!*branch) {
		*branch = "HEAD";
		return 0;
	}

	if (!strcmp(*branch, ".")) {
		const char *refname = resolve_ref_unsafe("HEAD", 0, NULL, NULL);

		if (!refname)
			return die_message(_("No such ref: %s"), "HEAD");

		/* detached HEAD */
		if (!strcmp(refname, "HEAD"))
			return die_message(_("Submodule (%s) branch configured to inherit "
					     "branch from superproject, but the superproject "
					     "is not on any branch"), sub->name);

		if (!skip_prefix(refname, "refs/heads/", &refname))
			return die_message(_("Expecting a full ref name, got %s"),
					   refname);

		*branch = refname;
		return 0;
	}

	/* Our "branch" is coming from repo_config_get_string_tmp() */
	return 0;
}

static int ensure_core_worktree(const char *path)
{
	const char *cw;
	struct repository subrepo;

	if (repo_submodule_init(&subrepo, the_repository, path, null_oid()))
		return die_message(_("could not get a repository handle for submodule '%s'"),
				   path);

	if (!repo_config_get_string_tmp(&subrepo, "core.worktree", &cw)) {
		char *cfg_file, *abs_path;
		const char *rel_path;
		struct strbuf sb = STRBUF_INIT;

		cfg_file = repo_git_path(&subrepo, "config");

		abs_path = absolute_pathdup(path);
		rel_path = relative_path(abs_path, subrepo.gitdir, &sb);

		git_config_set_in_file(cfg_file, "core.worktree", rel_path);

		free(cfg_file);
		free(abs_path);
		strbuf_release(&sb);
	}

	repo_clear(&subrepo);
	return 0;
}

static const char *submodule_update_type_to_label(enum submodule_update_type type)
{
	switch (type) {
	case SM_UPDATE_CHECKOUT:
		return "checkout";
	case SM_UPDATE_MERGE:
		return "merge";
	case SM_UPDATE_REBASE:
		return "rebase";
	case SM_UPDATE_UNSPECIFIED:
	case SM_UPDATE_NONE:
	case SM_UPDATE_COMMAND:
		break;
	}
	BUG("unreachable with type %d", type);
}

static void update_data_to_args(const struct update_data *update_data,
				struct strvec *args)
{
	enum submodule_update_type update_type = update_data->update_default;

	strvec_pushl(args, "submodule--helper", "update", "--recursive", NULL);
	if (update_data->displaypath) {
		strvec_push(args, "--super-prefix");
		strvec_pushf(args, "%s/", update_data->displaypath);
	}
	strvec_pushf(args, "--jobs=%d", update_data->max_jobs);
	if (update_data->quiet)
		strvec_push(args, "--quiet");
	if (update_data->force)
		strvec_push(args, "--force");
	if (update_data->init)
		strvec_push(args, "--init");
	if (update_data->remote)
		strvec_push(args, "--remote");
	if (update_data->nofetch)
		strvec_push(args, "--no-fetch");
	if (update_data->dissociate)
		strvec_push(args, "--dissociate");
	if (update_data->progress)
		strvec_push(args, "--progress");
	if (update_data->require_init)
		strvec_push(args, "--require-init");
	if (update_data->depth)
		strvec_pushf(args, "--depth=%d", update_data->depth);
	if (update_type != SM_UPDATE_UNSPECIFIED)
		strvec_pushf(args, "--%s",
			     submodule_update_type_to_label(update_type));

	if (update_data->references.nr) {
		struct string_list_item *item;

		for_each_string_list_item(item, &update_data->references)
			strvec_pushl(args, "--reference", item->string, NULL);
	}
	if (update_data->filter_options && update_data->filter_options->choice)
		strvec_pushf(args, "--filter=%s",
				expand_list_objects_filter_spec(
					update_data->filter_options));
	if (update_data->recommend_shallow == 0)
		strvec_push(args, "--no-recommend-shallow");
	else if (update_data->recommend_shallow == 1)
		strvec_push(args, "--recommend-shallow");
	if (update_data->single_branch >= 0)
		strvec_push(args, update_data->single_branch ?
				    "--single-branch" :
				    "--no-single-branch");
}

static int update_submodule(struct update_data *update_data)
{
	int ret;

	ret = determine_submodule_update_strategy(the_repository,
						  update_data->just_cloned,
						  update_data->sm_path,
						  update_data->update_default,
						  &update_data->update_strategy);
	if (ret)
		return ret;

	if (update_data->just_cloned)
		oidcpy(&update_data->suboid, null_oid());
	else if (resolve_gitlink_ref(update_data->sm_path, "HEAD", &update_data->suboid))
		return die_message(_("Unable to find current revision in submodule path '%s'"),
				   update_data->displaypath);

	if (update_data->remote) {
		char *remote_name;
		const char *branch;
		char *remote_ref;
		int code;

		code = get_default_remote_submodule(update_data->sm_path, &remote_name);
		if (code)
			return code;
		code = remote_submodule_branch(update_data->sm_path, &branch);
		if (code)
			return code;
		remote_ref = xstrfmt("refs/remotes/%s/%s", remote_name, branch);

		free(remote_name);

		if (!update_data->nofetch) {
			if (fetch_in_submodule(update_data->sm_path, update_data->depth,
					      0, NULL))
				return die_message(_("Unable to fetch in submodule path '%s'"),
						   update_data->sm_path);
		}

		if (resolve_gitlink_ref(update_data->sm_path, remote_ref, &update_data->oid))
			return die_message(_("Unable to find %s revision in submodule path '%s'"),
					   remote_ref, update_data->sm_path);

		free(remote_ref);
	}

	if (!oideq(&update_data->oid, &update_data->suboid) || update_data->force) {
		ret = run_update_procedure(update_data);
		if (ret)
			return ret;
	}

	if (update_data->recursive) {
		struct child_process cp = CHILD_PROCESS_INIT;
		struct update_data next = *update_data;

		next.prefix = NULL;
		oidcpy(&next.oid, null_oid());
		oidcpy(&next.suboid, null_oid());

		cp.dir = update_data->sm_path;
		cp.git_cmd = 1;
		prepare_submodule_repo_env(&cp.env);
		update_data_to_args(&next, &cp.args);

		ret = run_command(&cp);
		if (ret)
			die_message(_("Failed to recurse into submodule path '%s'"),
				    update_data->displaypath);
		return ret;
	}

	return 0;
}

static int update_submodules(struct update_data *update_data)
{
	int i, ret = 0;
	struct submodule_update_clone suc = SUBMODULE_UPDATE_CLONE_INIT;
	const struct run_process_parallel_opts opts = {
		.tr2_category = "submodule",
		.tr2_label = "parallel/update",

		.processes = update_data->max_jobs,

		.get_next_task = update_clone_get_next_task,
		.start_failure = update_clone_start_failure,
		.task_finished = update_clone_task_finished,
		.data = &suc,
	};

	suc.update_data = update_data;
	run_processes_parallel(&opts);

	/*
	 * We saved the output and put it out all at once now.
	 * That means:
	 * - the listener does not have to interleave their (checkout)
	 *   work with our fetching.  The writes involved in a
	 *   checkout involve more straightforward sequential I/O.
	 * - the listener can avoid doing any work if fetching failed.
	 */
	if (suc.quickstop) {
		ret = 1;
		goto cleanup;
	}

	for (i = 0; i < suc.update_clone_nr; i++) {
		struct update_clone_data ucd = suc.update_clone[i];
		int code;

		oidcpy(&update_data->oid, &ucd.oid);
		update_data->just_cloned = ucd.just_cloned;
		update_data->sm_path = ucd.sub->path;

		code = ensure_core_worktree(update_data->sm_path);
		if (code)
			goto fail;

		update_data->displaypath = get_submodule_displaypath(
			update_data->sm_path, update_data->prefix,
			update_data->super_prefix);
		code = update_submodule(update_data);
		FREE_AND_NULL(update_data->displaypath);
fail:
		if (!code)
			continue;
		ret = code;
		if (ret == 128)
			goto cleanup;
	}

cleanup:
	submodule_update_clone_release(&suc);
	string_list_clear(&update_data->references, 0);
	return ret;
}

static int module_update(int argc, const char **argv, const char *prefix)
{
	struct pathspec pathspec = { 0 };
	struct pathspec pathspec2 = { 0 };
	struct update_data opt = UPDATE_DATA_INIT;
	struct list_objects_filter_options filter_options =
		LIST_OBJECTS_FILTER_INIT;
	int ret;
	struct option module_update_options[] = {
		OPT__SUPER_PREFIX(&opt.super_prefix),
		OPT__FORCE(&opt.force, N_("force checkout updates"), 0),
		OPT_BOOL(0, "init", &opt.init,
			 N_("initialize uninitialized submodules before update")),
		OPT_BOOL(0, "remote", &opt.remote,
			 N_("use SHA-1 of submodule's remote tracking branch")),
		OPT_BOOL(0, "recursive", &opt.recursive,
			 N_("traverse submodules recursively")),
		OPT_BOOL('N', "no-fetch", &opt.nofetch,
			 N_("don't fetch new objects from the remote site")),
		OPT_SET_INT(0, "checkout", &opt.update_default,
			N_("use the 'checkout' update strategy (default)"),
			SM_UPDATE_CHECKOUT),
		OPT_SET_INT('m', "merge", &opt.update_default,
			N_("use the 'merge' update strategy"),
			SM_UPDATE_MERGE),
		OPT_SET_INT('r', "rebase", &opt.update_default,
			N_("use the 'rebase' update strategy"),
			SM_UPDATE_REBASE),
		OPT_STRING_LIST(0, "reference", &opt.references, N_("repo"),
			   N_("reference repository")),
		OPT_BOOL(0, "dissociate", &opt.dissociate,
			   N_("use --reference only while cloning")),
		OPT_INTEGER(0, "depth", &opt.depth,
			   N_("create a shallow clone truncated to the "
			      "specified number of revisions")),
		OPT_INTEGER('j', "jobs", &opt.max_jobs,
			    N_("parallel jobs")),
		OPT_BOOL(0, "recommend-shallow", &opt.recommend_shallow,
			    N_("whether the initial clone should follow the shallow recommendation")),
		OPT__QUIET(&opt.quiet, N_("don't print cloning progress")),
		OPT_BOOL(0, "progress", &opt.progress,
			    N_("force cloning progress")),
		OPT_BOOL(0, "require-init", &opt.require_init,
			   N_("disallow cloning into non-empty directory, implies --init")),
		OPT_BOOL(0, "single-branch", &opt.single_branch,
			 N_("clone only one branch, HEAD or --branch")),
		OPT_PARSE_LIST_OBJECTS_FILTER(&filter_options),
		OPT_END()
	};
	const char *const git_submodule_helper_usage[] = {
		N_("git submodule [--quiet] update"
		" [--init [--filter=<filter-spec>]] [--remote]"
		" [-N|--no-fetch] [-f|--force]"
		" [--checkout|--merge|--rebase]"
		" [--[no-]recommend-shallow] [--reference <repository>]"
		" [--recursive] [--[no-]single-branch] [--] [<path>...]"),
		NULL
	};

	update_clone_config_from_gitmodules(&opt.max_jobs);
	git_config(git_update_clone_config, &opt.max_jobs);

	argc = parse_options(argc, argv, prefix, module_update_options,
			     git_submodule_helper_usage, 0);

	if (opt.require_init)
		opt.init = 1;

	if (filter_options.choice && !opt.init) {
		usage_with_options(git_submodule_helper_usage,
				   module_update_options);
	}

	opt.filter_options = &filter_options;
	opt.prefix = prefix;

	if (opt.update_default)
		opt.update_strategy.type = opt.update_default;

	if (module_list_compute(argv, prefix, &pathspec, &opt.list) < 0) {
		ret = 1;
		goto cleanup;
	}

	if (pathspec.nr)
		opt.warn_if_uninitialized = 1;

	if (opt.init) {
		struct module_list list = MODULE_LIST_INIT;
		struct init_cb info = INIT_CB_INIT;

		if (module_list_compute(argv, opt.prefix,
					&pathspec2, &list) < 0) {
			module_list_release(&list);
			ret = 1;
			goto cleanup;
		}

		/*
		 * If there are no path args and submodule.active is set then,
		 * by default, only initialize 'active' modules.
		 */
		if (!argc && !git_config_get("submodule.active"))
			module_list_active(&list);

		info.prefix = opt.prefix;
		info.super_prefix = opt.super_prefix;
		if (opt.quiet)
			info.flags |= OPT_QUIET;

		for_each_listed_submodule(&list, init_submodule_cb, &info);
		module_list_release(&list);
	}

	ret = update_submodules(&opt);
cleanup:
	update_data_release(&opt);
	list_objects_filter_release(&filter_options);
	clear_pathspec(&pathspec);
	clear_pathspec(&pathspec2);
	return ret;
}

static int push_check(int argc, const char **argv, const char *prefix UNUSED)
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
	struct pathspec pathspec = { 0 };
	struct module_list list = MODULE_LIST_INIT;
	const char *super_prefix = NULL;
	struct option embed_gitdir_options[] = {
		OPT__SUPER_PREFIX(&super_prefix),
		OPT_END()
	};
	const char *const git_submodule_helper_usage[] = {
		N_("git submodule absorbgitdirs [<options>] [<path>...]"),
		NULL
	};
	int ret = 1;

	argc = parse_options(argc, argv, prefix, embed_gitdir_options,
			     git_submodule_helper_usage, 0);

	if (module_list_compute(argv, prefix, &pathspec, &list) < 0)
		goto cleanup;

	for (i = 0; i < list.nr; i++)
		absorb_git_dir_into_superproject(list.entries[i]->name,
						 super_prefix);

	ret = 0;
cleanup:
	clear_pathspec(&pathspec);
	module_list_release(&list);
	return ret;
}

static int module_set_url(int argc, const char **argv, const char *prefix)
{
	int quiet = 0;
	const char *newurl;
	const char *path;
	char *config_name;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("suppress output for setting url of a submodule")),
		OPT_END()
	};
	const char *const usage[] = {
		N_("git submodule set-url [--quiet] <path> <newurl>"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, options, usage, 0);

	if (argc != 2 || !(path = argv[0]) || !(newurl = argv[1]))
		usage_with_options(usage, options);

	config_name = xstrfmt("submodule.%s.url", path);

	config_set_in_gitmodules_file_gently(config_name, newurl);
	sync_submodule(path, prefix, NULL, quiet ? OPT_QUIET : 0);

	free(config_name);

	return 0;
}

static int module_set_branch(int argc, const char **argv, const char *prefix)
{
	int opt_default = 0, ret;
	const char *opt_branch = NULL;
	const char *path;
	char *config_name;
	struct option options[] = {
		/*
		 * We accept the `quiet` option for uniformity across subcommands,
		 * though there is nothing to make less verbose in this subcommand.
		 */
		OPT_NOOP_NOARG('q', "quiet"),

		OPT_BOOL('d', "default", &opt_default,
			N_("set the default tracking branch to master")),
		OPT_STRING('b', "branch", &opt_branch, N_("branch"),
			N_("set the default tracking branch")),
		OPT_END()
	};
	const char *const usage[] = {
		N_("git submodule set-branch [-q|--quiet] (-d|--default) <path>"),
		N_("git submodule set-branch [-q|--quiet] (-b|--branch) <branch> <path>"),
		NULL
	};

	argc = parse_options(argc, argv, prefix, options, usage, 0);

	if (!opt_branch && !opt_default)
		die(_("--branch or --default required"));

	if (opt_branch && opt_default)
		die(_("options '%s' and '%s' cannot be used together"), "--branch", "--default");

	if (argc != 1 || !(path = argv[0]))
		usage_with_options(usage, options);

	config_name = xstrfmt("submodule.%s.branch", path);
	ret = config_set_in_gitmodules_file_gently(config_name, opt_branch);

	free(config_name);
	return !!ret;
}

static int module_create_branch(int argc, const char **argv, const char *prefix)
{
	enum branch_track track;
	int quiet = 0, force = 0, reflog = 0, dry_run = 0;
	struct option options[] = {
		OPT__QUIET(&quiet, N_("print only error messages")),
		OPT__FORCE(&force, N_("force creation"), 0),
		OPT_BOOL(0, "create-reflog", &reflog,
			 N_("create the branch's reflog")),
		OPT_CALLBACK_F('t', "track",  &track, "(direct|inherit)",
			N_("set branch tracking configuration"),
			PARSE_OPT_OPTARG,
			parse_opt_tracking_mode),
		OPT__DRY_RUN(&dry_run,
			     N_("show whether the branch would be created")),
		OPT_END()
	};
	const char *const usage[] = {
		N_("git submodule--helper create-branch [-f|--force] [--create-reflog] [-q|--quiet] [-t|--track] [-n|--dry-run] <name> <start-oid> <start-name>"),
		NULL
	};

	git_config(git_default_config, NULL);
	track = git_branch_track;
	argc = parse_options(argc, argv, prefix, options, usage, 0);

	if (argc != 3)
		usage_with_options(usage, options);

	if (!quiet && !dry_run)
		printf_ln(_("creating branch '%s'"), argv[0]);

	create_branches_recursively(the_repository, argv[0], argv[1], argv[2],
				    force, reflog, quiet, track, dry_run);
	return 0;
}

struct add_data {
	const char *prefix;
	const char *branch;
	const char *reference_path;
	char *sm_path;
	const char *sm_name;
	const char *repo;
	const char *realrepo;
	int depth;
	unsigned int force: 1;
	unsigned int quiet: 1;
	unsigned int progress: 1;
	unsigned int dissociate: 1;
};
#define ADD_DATA_INIT { .depth = -1 }

static void append_fetch_remotes(struct strbuf *msg, const char *git_dir_path)
{
	struct child_process cp_remote = CHILD_PROCESS_INIT;
	struct strbuf sb_remote_out = STRBUF_INIT;

	cp_remote.git_cmd = 1;
	strvec_pushf(&cp_remote.env,
		     "GIT_DIR=%s", git_dir_path);
	strvec_push(&cp_remote.env, "GIT_WORK_TREE=.");
	strvec_pushl(&cp_remote.args, "remote", "-v", NULL);
	if (!capture_command(&cp_remote, &sb_remote_out, 0)) {
		char *next_line;
		char *line = sb_remote_out.buf;

		while ((next_line = strchr(line, '\n')) != NULL) {
			size_t len = next_line - line;

			if (strip_suffix_mem(line, &len, " (fetch)"))
				strbuf_addf(msg, "  %.*s\n", (int)len, line);
			line = next_line + 1;
		}
	}

	strbuf_release(&sb_remote_out);
}

static int add_submodule(const struct add_data *add_data)
{
	char *submod_gitdir_path;
	struct module_clone_data clone_data = MODULE_CLONE_DATA_INIT;
	struct string_list reference = STRING_LIST_INIT_NODUP;
	int ret = -1;

	/* perhaps the path already exists and is already a git repo, else clone it */
	if (is_directory(add_data->sm_path)) {
		struct strbuf sm_path = STRBUF_INIT;
		strbuf_addstr(&sm_path, add_data->sm_path);
		submod_gitdir_path = xstrfmt("%s/.git", add_data->sm_path);
		if (is_nonbare_repository_dir(&sm_path))
			printf(_("Adding existing repo at '%s' to the index\n"),
			       add_data->sm_path);
		else
			die(_("'%s' already exists and is not a valid git repo"),
			    add_data->sm_path);
		strbuf_release(&sm_path);
		free(submod_gitdir_path);
	} else {
		struct child_process cp = CHILD_PROCESS_INIT;

		submod_gitdir_path = xstrfmt(".git/modules/%s", add_data->sm_name);

		if (is_directory(submod_gitdir_path)) {
			if (!add_data->force) {
				struct strbuf msg = STRBUF_INIT;
				char *die_msg;

				strbuf_addf(&msg, _("A git directory for '%s' is found "
						    "locally with remote(s):\n"),
					    add_data->sm_name);

				append_fetch_remotes(&msg, submod_gitdir_path);
				free(submod_gitdir_path);

				strbuf_addf(&msg, _("If you want to reuse this local git "
						    "directory instead of cloning again from\n"
						    "  %s\n"
						    "use the '--force' option. If the local git "
						    "directory is not the correct repo\n"
						    "or you are unsure what this means choose "
						    "another name with the '--name' option."),
					    add_data->realrepo);

				die_msg = strbuf_detach(&msg, NULL);
				die("%s", die_msg);
			} else {
				printf(_("Reactivating local git directory for "
					 "submodule '%s'\n"), add_data->sm_name);
			}
		}
		free(submod_gitdir_path);

		clone_data.prefix = add_data->prefix;
		clone_data.path = add_data->sm_path;
		clone_data.name = add_data->sm_name;
		clone_data.url = add_data->realrepo;
		clone_data.quiet = add_data->quiet;
		clone_data.progress = add_data->progress;
		if (add_data->reference_path) {
			char *p = xstrdup(add_data->reference_path);

			string_list_append(&reference, p)->util = p;
		}
		clone_data.dissociate = add_data->dissociate;
		if (add_data->depth >= 0)
			clone_data.depth = xstrfmt("%d", add_data->depth);

		if (clone_submodule(&clone_data, &reference))
			goto cleanup;

		prepare_submodule_repo_env(&cp.env);
		cp.git_cmd = 1;
		cp.dir = add_data->sm_path;
		/*
		 * NOTE: we only get here if add_data->force is true, so
		 * passing --force to checkout is reasonable.
		 */
		strvec_pushl(&cp.args, "checkout", "-f", "-q", NULL);

		if (add_data->branch) {
			strvec_pushl(&cp.args, "-B", add_data->branch, NULL);
			strvec_pushf(&cp.args, "origin/%s", add_data->branch);
		}

		if (run_command(&cp))
			die(_("unable to checkout submodule '%s'"), add_data->sm_path);
	}
	ret = 0;
cleanup:
	string_list_clear(&reference, 1);
	return ret;
}

static int config_submodule_in_gitmodules(const char *name, const char *var, const char *value)
{
	char *key;
	int ret;

	if (!is_writing_gitmodules_ok())
		die(_("please make sure that the .gitmodules file is in the working tree"));

	key = xstrfmt("submodule.%s.%s", name, var);
	ret = config_set_in_gitmodules_file_gently(key, value);
	free(key);

	return ret;
}

static void configure_added_submodule(struct add_data *add_data)
{
	char *key;
	struct child_process add_submod = CHILD_PROCESS_INIT;
	struct child_process add_gitmodules = CHILD_PROCESS_INIT;

	key = xstrfmt("submodule.%s.url", add_data->sm_name);
	git_config_set_gently(key, add_data->realrepo);
	free(key);

	add_submod.git_cmd = 1;
	strvec_pushl(&add_submod.args, "add",
		     "--no-warn-embedded-repo", NULL);
	if (add_data->force)
		strvec_push(&add_submod.args, "--force");
	strvec_pushl(&add_submod.args, "--", add_data->sm_path, NULL);

	if (run_command(&add_submod))
		die(_("Failed to add submodule '%s'"), add_data->sm_path);

	if (config_submodule_in_gitmodules(add_data->sm_name, "path", add_data->sm_path) ||
	    config_submodule_in_gitmodules(add_data->sm_name, "url", add_data->repo))
		die(_("Failed to register submodule '%s'"), add_data->sm_path);

	if (add_data->branch) {
		if (config_submodule_in_gitmodules(add_data->sm_name,
						   "branch", add_data->branch))
			die(_("Failed to register submodule '%s'"), add_data->sm_path);
	}

	add_gitmodules.git_cmd = 1;
	strvec_pushl(&add_gitmodules.args,
		     "add", "--force", "--", ".gitmodules", NULL);

	if (run_command(&add_gitmodules))
		die(_("Failed to register submodule '%s'"), add_data->sm_path);

	/*
	 * NEEDSWORK: In a multi-working-tree world this needs to be
	 * set in the per-worktree config.
	 */
	/*
	 * NEEDSWORK: In the longer run, we need to get rid of this
	 * pattern of querying "submodule.active" before calling
	 * is_submodule_active(), since that function needs to find
	 * out the value of "submodule.active" again anyway.
	 */
	if (!git_config_get("submodule.active")) {
		/*
		 * If the submodule being added isn't already covered by the
		 * current configured pathspec, set the submodule's active flag
		 */
		if (!is_submodule_active(the_repository, add_data->sm_path)) {
			key = xstrfmt("submodule.%s.active", add_data->sm_name);
			git_config_set_gently(key, "true");
			free(key);
		}
	} else {
		key = xstrfmt("submodule.%s.active", add_data->sm_name);
		git_config_set_gently(key, "true");
		free(key);
	}
}

static void die_on_index_match(const char *path, int force)
{
	struct pathspec ps;
	const char *args[] = { path, NULL };
	parse_pathspec(&ps, 0, PATHSPEC_PREFER_CWD, NULL, args);

	if (repo_read_index_preload(the_repository, NULL, 0) < 0)
		die(_("index file corrupt"));

	if (ps.nr) {
		int i;
		char *ps_matched = xcalloc(ps.nr, 1);

		/* TODO: audit for interaction with sparse-index. */
		ensure_full_index(&the_index);

		/*
		 * Since there is only one pathspec, we just need to
		 * check ps_matched[0] to know if a cache entry matched.
		 */
		for (i = 0; i < the_index.cache_nr; i++) {
			ce_path_match(&the_index, the_index.cache[i], &ps,
				      ps_matched);

			if (ps_matched[0]) {
				if (!force)
					die(_("'%s' already exists in the index"),
					    path);
				if (!S_ISGITLINK(the_index.cache[i]->ce_mode))
					die(_("'%s' already exists in the index "
					      "and is not a submodule"), path);
				break;
			}
		}
		free(ps_matched);
	}
	clear_pathspec(&ps);
}

static void die_on_repo_without_commits(const char *path)
{
	struct strbuf sb = STRBUF_INIT;
	strbuf_addstr(&sb, path);
	if (is_nonbare_repository_dir(&sb)) {
		struct object_id oid;
		if (resolve_gitlink_ref(path, "HEAD", &oid) < 0)
			die(_("'%s' does not have a commit checked out"), path);
	}
	strbuf_release(&sb);
}

static int module_add(int argc, const char **argv, const char *prefix)
{
	int force = 0, quiet = 0, progress = 0, dissociate = 0;
	struct add_data add_data = ADD_DATA_INIT;
	char *to_free = NULL;
	struct option options[] = {
		OPT_STRING('b', "branch", &add_data.branch, N_("branch"),
			   N_("branch of repository to add as submodule")),
		OPT__FORCE(&force, N_("allow adding an otherwise ignored submodule path"),
			   PARSE_OPT_NOCOMPLETE),
		OPT__QUIET(&quiet, N_("print only error messages")),
		OPT_BOOL(0, "progress", &progress, N_("force cloning progress")),
		OPT_STRING(0, "reference", &add_data.reference_path, N_("repository"),
			   N_("reference repository")),
		OPT_BOOL(0, "dissociate", &dissociate, N_("borrow the objects from reference repositories")),
		OPT_STRING(0, "name", &add_data.sm_name, N_("name"),
			   N_("sets the submodule's name to the given string "
			      "instead of defaulting to its path")),
		OPT_INTEGER(0, "depth", &add_data.depth, N_("depth for shallow clones")),
		OPT_END()
	};
	const char *const usage[] = {
		N_("git submodule add [<options>] [--] <repository> [<path>]"),
		NULL
	};
	struct strbuf sb = STRBUF_INIT;
	int ret = 1;

	argc = parse_options(argc, argv, prefix, options, usage, 0);

	if (!is_writing_gitmodules_ok())
		die(_("please make sure that the .gitmodules file is in the working tree"));

	if (prefix && *prefix &&
	    add_data.reference_path && !is_absolute_path(add_data.reference_path))
		add_data.reference_path = xstrfmt("%s%s", prefix, add_data.reference_path);

	if (argc == 0 || argc > 2)
		usage_with_options(usage, options);

	add_data.repo = argv[0];
	if (argc == 1)
		add_data.sm_path = git_url_basename(add_data.repo, 0, 0);
	else
		add_data.sm_path = xstrdup(argv[1]);

	if (prefix && *prefix && !is_absolute_path(add_data.sm_path)) {
		char *sm_path = add_data.sm_path;

		add_data.sm_path = xstrfmt("%s%s", prefix, sm_path);
		free(sm_path);
	}

	if (starts_with_dot_dot_slash(add_data.repo) ||
	    starts_with_dot_slash(add_data.repo)) {
		if (prefix)
			die(_("Relative path can only be used from the toplevel "
			      "of the working tree"));

		/* dereference source url relative to parent's url */
		to_free = resolve_relative_url(add_data.repo, NULL, 1);
		add_data.realrepo = to_free;
	} else if (is_dir_sep(add_data.repo[0]) || strchr(add_data.repo, ':')) {
		add_data.realrepo = add_data.repo;
	} else {
		die(_("repo URL: '%s' must be absolute or begin with ./|../"),
		    add_data.repo);
	}

	/*
	 * normalize path:
	 * multiple //; leading ./; /./; /../;
	 */
	normalize_path_copy(add_data.sm_path, add_data.sm_path);
	strip_dir_trailing_slashes(add_data.sm_path);

	die_on_index_match(add_data.sm_path, force);
	die_on_repo_without_commits(add_data.sm_path);

	if (!force) {
		struct child_process cp = CHILD_PROCESS_INIT;

		cp.git_cmd = 1;
		cp.no_stdout = 1;
		strvec_pushl(&cp.args, "add", "--dry-run", "--ignore-missing",
			     "--no-warn-embedded-repo", add_data.sm_path, NULL);
		if ((ret = pipe_command(&cp, NULL, 0, NULL, 0, &sb, 0))) {
			strbuf_complete_line(&sb);
			fputs(sb.buf, stderr);
			goto cleanup;
		}
	}

	if(!add_data.sm_name)
		add_data.sm_name = add_data.sm_path;

	if (check_submodule_name(add_data.sm_name))
		die(_("'%s' is not a valid submodule name"), add_data.sm_name);

	add_data.prefix = prefix;
	add_data.force = !!force;
	add_data.quiet = !!quiet;
	add_data.progress = !!progress;
	add_data.dissociate = !!dissociate;

	if (add_submodule(&add_data))
		goto cleanup;
	configure_added_submodule(&add_data);

	ret = 0;
cleanup:
	free(add_data.sm_path);
	free(to_free);
	strbuf_release(&sb);

	return ret;
}

int cmd_submodule__helper(int argc, const char **argv, const char *prefix)
{
	parse_opt_subcommand_fn *fn = NULL;
	const char *const usage[] = {
		N_("git submodule--helper <command>"),
		NULL
	};
	struct option options[] = {
		OPT_SUBCOMMAND("clone", &fn, module_clone),
		OPT_SUBCOMMAND("add", &fn, module_add),
		OPT_SUBCOMMAND("update", &fn, module_update),
		OPT_SUBCOMMAND("foreach", &fn, module_foreach),
		OPT_SUBCOMMAND("init", &fn, module_init),
		OPT_SUBCOMMAND("status", &fn, module_status),
		OPT_SUBCOMMAND("sync", &fn, module_sync),
		OPT_SUBCOMMAND("deinit", &fn, module_deinit),
		OPT_SUBCOMMAND("summary", &fn, module_summary),
		OPT_SUBCOMMAND("push-check", &fn, push_check),
		OPT_SUBCOMMAND("absorbgitdirs", &fn, absorb_git_dirs),
		OPT_SUBCOMMAND("set-url", &fn, module_set_url),
		OPT_SUBCOMMAND("set-branch", &fn, module_set_branch),
		OPT_SUBCOMMAND("create-branch", &fn, module_create_branch),
		OPT_END()
	};
	argc = parse_options(argc, argv, prefix, options, usage, 0);

	return fn(argc, argv, prefix);
}
