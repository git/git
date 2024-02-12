/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "builtin.h"
#include "abspath.h"
#include "environment.h"
#include "gettext.h"
#include "object-file.h"
#include "parse-options.h"
#include "path.h"
#include "refs.h"
#include "repository.h"
#include "setup.h"
#include "strbuf.h"

static int guess_repository_type(const char *git_dir)
{
	const char *slash;
	char *cwd;
	int cwd_is_git_dir;

	/*
	 * "GIT_DIR=. git init" is always bare.
	 * "GIT_DIR=`pwd` git init" too.
	 */
	if (!strcmp(".", git_dir))
		return 1;
	cwd = xgetcwd();
	cwd_is_git_dir = !strcmp(git_dir, cwd);
	free(cwd);
	if (cwd_is_git_dir)
		return 1;
	/*
	 * "GIT_DIR=.git or GIT_DIR=something/.git is usually not.
	 */
	if (!strcmp(git_dir, ".git"))
		return 0;
	slash = strrchr(git_dir, '/');
	if (slash && !strcmp(slash, "/.git"))
		return 0;

	/*
	 * Otherwise it is often bare.  At this point
	 * we are just guessing.
	 */
	return 1;
}

static int shared_callback(const struct option *opt, const char *arg, int unset)
{
	BUG_ON_OPT_NEG(unset);
	*((int *) opt->value) = (arg) ? git_config_perm("arg", arg) : PERM_GROUP;
	return 0;
}

static const char *const init_db_usage[] = {
	N_("git init [-q | --quiet] [--bare] [--template=<template-directory>]\n"
	   "         [--separate-git-dir <git-dir>] [--object-format=<format>]\n"
	   "         [--ref-format=<format>]\n"
	   "         [-b <branch-name> | --initial-branch=<branch-name>]\n"
	   "         [--shared[=<permissions>]] [<directory>]"),
	NULL
};

/*
 * If you want to, you can share the DB area with any number of branches.
 * That has advantages: you can save space by sharing all the SHA1 objects.
 * On the other hand, it might just make lookup slower and messier. You
 * be the judge.  The default case is to have one DB per managed directory.
 */
int cmd_init_db(int argc, const char **argv, const char *prefix)
{
	const char *git_dir;
	const char *real_git_dir = NULL;
	const char *work_tree;
	const char *template_dir = NULL;
	unsigned int flags = 0;
	const char *object_format = NULL;
	const char *ref_format = NULL;
	const char *initial_branch = NULL;
	int hash_algo = GIT_HASH_UNKNOWN;
	unsigned int ref_storage_format = REF_STORAGE_FORMAT_UNKNOWN;
	int init_shared_repository = -1;
	const struct option init_db_options[] = {
		OPT_STRING(0, "template", &template_dir, N_("template-directory"),
				N_("directory from which templates will be used")),
		OPT_SET_INT(0, "bare", &is_bare_repository_cfg,
				N_("create a bare repository"), 1),
		{ OPTION_CALLBACK, 0, "shared", &init_shared_repository,
			N_("permissions"),
			N_("specify that the git repository is to be shared amongst several users"),
			PARSE_OPT_OPTARG | PARSE_OPT_NONEG, shared_callback, 0},
		OPT_BIT('q', "quiet", &flags, N_("be quiet"), INIT_DB_QUIET),
		OPT_STRING(0, "separate-git-dir", &real_git_dir, N_("gitdir"),
			   N_("separate git dir from working tree")),
		OPT_STRING('b', "initial-branch", &initial_branch, N_("name"),
			   N_("override the name of the initial branch")),
		OPT_STRING(0, "object-format", &object_format, N_("hash"),
			   N_("specify the hash algorithm to use")),
		OPT_STRING(0, "ref-format", &ref_format, N_("format"),
			   N_("specify the reference format to use")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, init_db_options, init_db_usage, 0);

	if (real_git_dir && is_bare_repository_cfg == 1)
		die(_("options '%s' and '%s' cannot be used together"), "--separate-git-dir", "--bare");

	if (real_git_dir && !is_absolute_path(real_git_dir))
		real_git_dir = real_pathdup(real_git_dir, 1);

	if (template_dir && *template_dir && !is_absolute_path(template_dir)) {
		template_dir = absolute_pathdup(template_dir);
		UNLEAK(template_dir);
	}

	if (argc == 1) {
		int mkdir_tried = 0;
	retry:
		if (chdir(argv[0]) < 0) {
			if (!mkdir_tried) {
				int saved;
				/*
				 * At this point we haven't read any configuration,
				 * and we know shared_repository should always be 0;
				 * but just in case we play safe.
				 */
				saved = get_shared_repository();
				set_shared_repository(0);
				switch (safe_create_leading_directories_const(argv[0])) {
				case SCLD_OK:
				case SCLD_PERMS:
					break;
				case SCLD_EXISTS:
					errno = EEXIST;
					/* fallthru */
				default:
					die_errno(_("cannot mkdir %s"), argv[0]);
					break;
				}
				set_shared_repository(saved);
				if (mkdir(argv[0], 0777) < 0)
					die_errno(_("cannot mkdir %s"), argv[0]);
				mkdir_tried = 1;
				goto retry;
			}
			die_errno(_("cannot chdir to %s"), argv[0]);
		}
	} else if (0 < argc) {
		usage(init_db_usage[0]);
	}
	if (is_bare_repository_cfg == 1) {
		char *cwd = xgetcwd();
		setenv(GIT_DIR_ENVIRONMENT, cwd, argc > 0);
		free(cwd);
	}

	if (object_format) {
		hash_algo = hash_algo_by_name(object_format);
		if (hash_algo == GIT_HASH_UNKNOWN)
			die(_("unknown hash algorithm '%s'"), object_format);
	}

	if (ref_format) {
		ref_storage_format = ref_storage_format_by_name(ref_format);
		if (ref_storage_format == REF_STORAGE_FORMAT_UNKNOWN)
			die(_("unknown ref storage format '%s'"), ref_format);
	}

	if (init_shared_repository != -1)
		set_shared_repository(init_shared_repository);

	/*
	 * GIT_WORK_TREE makes sense only in conjunction with GIT_DIR
	 * without --bare.  Catch the error early.
	 */
	git_dir = xstrdup_or_null(getenv(GIT_DIR_ENVIRONMENT));
	work_tree = xstrdup_or_null(getenv(GIT_WORK_TREE_ENVIRONMENT));
	if ((!git_dir || is_bare_repository_cfg == 1) && work_tree)
		die(_("%s (or --work-tree=<directory>) not allowed without "
			  "specifying %s (or --git-dir=<directory>)"),
		    GIT_WORK_TREE_ENVIRONMENT,
		    GIT_DIR_ENVIRONMENT);

	/*
	 * Set up the default .git directory contents
	 */
	if (!git_dir)
		git_dir = DEFAULT_GIT_DIR_ENVIRONMENT;

	/*
	 * When --separate-git-dir is used inside a linked worktree, take
	 * care to ensure that the common .git/ directory is relocated, not
	 * the worktree-specific .git/worktrees/<id>/ directory.
	 */
	if (real_git_dir) {
		int err;
		const char *p;
		struct strbuf sb = STRBUF_INIT;

		p = read_gitfile_gently(git_dir, &err);
		if (p && get_common_dir(&sb, p)) {
			struct strbuf mainwt = STRBUF_INIT;

			strbuf_addbuf(&mainwt, &sb);
			strbuf_strip_suffix(&mainwt, "/.git");
			if (chdir(mainwt.buf) < 0)
				die_errno(_("cannot chdir to %s"), mainwt.buf);
			strbuf_release(&mainwt);
			git_dir = strbuf_detach(&sb, NULL);
		}
		strbuf_release(&sb);
	}

	if (is_bare_repository_cfg < 0)
		is_bare_repository_cfg = guess_repository_type(git_dir);

	if (!is_bare_repository_cfg) {
		const char *git_dir_parent = strrchr(git_dir, '/');
		if (git_dir_parent) {
			char *rel = xstrndup(git_dir, git_dir_parent - git_dir);
			git_work_tree_cfg = real_pathdup(rel, 1);
			free(rel);
		}
		if (!git_work_tree_cfg)
			git_work_tree_cfg = xgetcwd();
		if (work_tree)
			set_git_work_tree(work_tree);
		else
			set_git_work_tree(git_work_tree_cfg);
		if (access(get_git_work_tree(), X_OK))
			die_errno (_("Cannot access work tree '%s'"),
				   get_git_work_tree());
	}
	else {
		if (real_git_dir)
			die(_("--separate-git-dir incompatible with bare repository"));
		if (work_tree)
			set_git_work_tree(work_tree);
	}

	UNLEAK(real_git_dir);
	UNLEAK(git_dir);
	UNLEAK(work_tree);

	flags |= INIT_DB_EXIST_OK;
	return init_db(git_dir, real_git_dir, template_dir, hash_algo,
		       ref_storage_format, initial_branch,
		       init_shared_repository, flags);
}
