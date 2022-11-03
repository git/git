/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "config.h"
#include "refs.h"
#include "builtin.h"
#include "exec-cmd.h"
#include "parse-options.h"
#include "worktree.h"

#ifndef DEFAULT_GIT_TEMPLATE_DIR
#define DEFAULT_GIT_TEMPLATE_DIR "/usr/share/git-core/templates"
#endif

#ifdef NO_TRUSTABLE_FILEMODE
#define TEST_FILEMODE 0
#else
#define TEST_FILEMODE 1
#endif

#define GIT_DEFAULT_HASH_ENVIRONMENT "GIT_DEFAULT_HASH"

static int init_is_bare_repository = 0;
static int init_shared_repository = -1;

static void copy_templates_1(struct strbuf *path, struct strbuf *template_path,
			     DIR *dir)
{
	size_t path_baselen = path->len;
	size_t template_baselen = template_path->len;
	struct dirent *de;

	/* Note: if ".git/hooks" file exists in the repository being
	 * re-initialized, /etc/core-git/templates/hooks/update would
	 * cause "git init" to fail here.  I think this is sane but
	 * it means that the set of templates we ship by default, along
	 * with the way the namespace under .git/ is organized, should
	 * be really carefully chosen.
	 */
	safe_create_dir(path->buf, 1);
	while ((de = readdir(dir)) != NULL) {
		struct stat st_git, st_template;
		int exists = 0;

		strbuf_setlen(path, path_baselen);
		strbuf_setlen(template_path, template_baselen);

		if (de->d_name[0] == '.')
			continue;
		strbuf_addstr(path, de->d_name);
		strbuf_addstr(template_path, de->d_name);
		if (lstat(path->buf, &st_git)) {
			if (errno != ENOENT)
				die_errno(_("cannot stat '%s'"), path->buf);
		}
		else
			exists = 1;

		if (lstat(template_path->buf, &st_template))
			die_errno(_("cannot stat template '%s'"), template_path->buf);

		if (S_ISDIR(st_template.st_mode)) {
			DIR *subdir = opendir(template_path->buf);
			if (!subdir)
				die_errno(_("cannot opendir '%s'"), template_path->buf);
			strbuf_addch(path, '/');
			strbuf_addch(template_path, '/');
			copy_templates_1(path, template_path, subdir);
			closedir(subdir);
		}
		else if (exists)
			continue;
		else if (S_ISLNK(st_template.st_mode)) {
			struct strbuf lnk = STRBUF_INIT;
			if (strbuf_readlink(&lnk, template_path->buf,
					    st_template.st_size) < 0)
				die_errno(_("cannot readlink '%s'"), template_path->buf);
			if (symlink(lnk.buf, path->buf))
				die_errno(_("cannot symlink '%s' '%s'"),
					  lnk.buf, path->buf);
			strbuf_release(&lnk);
		}
		else if (S_ISREG(st_template.st_mode)) {
			if (copy_file(path->buf, template_path->buf, st_template.st_mode))
				die_errno(_("cannot copy '%s' to '%s'"),
					  template_path->buf, path->buf);
		}
		else
			error(_("ignoring template %s"), template_path->buf);
	}
}

static void copy_templates(const char *template_dir, const char *init_template_dir)
{
	struct strbuf path = STRBUF_INIT;
	struct strbuf template_path = STRBUF_INIT;
	size_t template_len;
	struct repository_format template_format = REPOSITORY_FORMAT_INIT;
	struct strbuf err = STRBUF_INIT;
	DIR *dir;
	char *to_free = NULL;

	if (!template_dir)
		template_dir = getenv(TEMPLATE_DIR_ENVIRONMENT);
	if (!template_dir)
		template_dir = init_template_dir;
	if (!template_dir)
		template_dir = to_free = system_path(DEFAULT_GIT_TEMPLATE_DIR);
	if (!template_dir[0]) {
		free(to_free);
		return;
	}

	strbuf_addstr(&template_path, template_dir);
	strbuf_complete(&template_path, '/');
	template_len = template_path.len;

	dir = opendir(template_path.buf);
	if (!dir) {
		warning(_("templates not found in %s"), template_dir);
		goto free_return;
	}

	/* Make sure that template is from the correct vintage */
	strbuf_addstr(&template_path, "config");
	read_repository_format(&template_format, template_path.buf);
	strbuf_setlen(&template_path, template_len);

	/*
	 * No mention of version at all is OK, but anything else should be
	 * verified.
	 */
	if (template_format.version >= 0 &&
	    verify_repository_format(&template_format, &err) < 0) {
		warning(_("not copying templates from '%s': %s"),
			  template_dir, err.buf);
		strbuf_release(&err);
		goto close_free_return;
	}

	strbuf_addstr(&path, get_git_common_dir());
	strbuf_complete(&path, '/');
	copy_templates_1(&path, &template_path, dir);
close_free_return:
	closedir(dir);
free_return:
	free(to_free);
	strbuf_release(&path);
	strbuf_release(&template_path);
	clear_repository_format(&template_format);
}

/*
 * If the git_dir is not directly inside the working tree, then git will not
 * find it by default, and we need to set the worktree explicitly.
 */
static int needs_work_tree_config(const char *git_dir, const char *work_tree)
{
	if (!strcmp(work_tree, "/") && !strcmp(git_dir, "/.git"))
		return 0;
	if (skip_prefix(git_dir, work_tree, &git_dir) &&
	    !strcmp(git_dir, "/.git"))
		return 0;
	return 1;
}

void initialize_repository_version(int hash_algo, int reinit)
{
	char repo_version_string[10];
	int repo_version = GIT_REPO_VERSION;

	if (hash_algo != GIT_HASH_SHA1)
		repo_version = GIT_REPO_VERSION_READ;

	/* This forces creation of new config file */
	xsnprintf(repo_version_string, sizeof(repo_version_string),
		  "%d", repo_version);
	git_config_set("core.repositoryformatversion", repo_version_string);

	if (hash_algo != GIT_HASH_SHA1)
		git_config_set("extensions.objectformat",
			       hash_algos[hash_algo].name);
	else if (reinit)
		git_config_set_gently("extensions.objectformat", NULL);
}

static int create_default_files(const char *template_path,
				const char *original_git_dir,
				const char *initial_branch,
				const struct repository_format *fmt,
				int quiet)
{
	struct stat st1;
	struct strbuf buf = STRBUF_INIT;
	char *path;
	char junk[2];
	int reinit;
	int filemode;
	struct strbuf err = STRBUF_INIT;
	const char *init_template_dir = NULL;
	const char *work_tree = get_git_work_tree();

	/*
	 * First copy the templates -- we might have the default
	 * config file there, in which case we would want to read
	 * from it after installing.
	 *
	 * Before reading that config, we also need to clear out any cached
	 * values (since we've just potentially changed what's available on
	 * disk).
	 */
	git_config_get_pathname("init.templatedir", &init_template_dir);
	copy_templates(template_path, init_template_dir);
	free((char *)init_template_dir);
	git_config_clear();
	reset_shared_repository();
	git_config(git_default_config, NULL);

	/*
	 * We must make sure command-line options continue to override any
	 * values we might have just re-read from the config.
	 */
	is_bare_repository_cfg = init_is_bare_repository || !work_tree;
	if (init_shared_repository != -1)
		set_shared_repository(init_shared_repository);

	/*
	 * We would have created the above under user's umask -- under
	 * shared-repository settings, we would need to fix them up.
	 */
	if (get_shared_repository()) {
		adjust_shared_perm(get_git_dir());
	}

	/*
	 * We need to create a "refs" dir in any case so that older
	 * versions of git can tell that this is a repository.
	 */
	safe_create_dir(git_path("refs"), 1);
	adjust_shared_perm(git_path("refs"));

	if (refs_init_db(&err))
		die("failed to set up refs db: %s", err.buf);

	/*
	 * Point the HEAD symref to the initial branch with if HEAD does
	 * not yet exist.
	 */
	path = git_path_buf(&buf, "HEAD");
	reinit = (!access(path, R_OK)
		  || readlink(path, junk, sizeof(junk)-1) != -1);
	if (!reinit) {
		char *ref;

		if (!initial_branch)
			initial_branch = git_default_branch_name(quiet);

		ref = xstrfmt("refs/heads/%s", initial_branch);
		if (check_refname_format(ref, 0) < 0)
			die(_("invalid initial branch name: '%s'"),
			    initial_branch);

		if (create_symref("HEAD", ref, NULL) < 0)
			exit(1);
		free(ref);
	}

	initialize_repository_version(fmt->hash_algo, 0);

	/* Check filemode trustability */
	path = git_path_buf(&buf, "config");
	filemode = TEST_FILEMODE;
	if (TEST_FILEMODE && !lstat(path, &st1)) {
		struct stat st2;
		filemode = (!chmod(path, st1.st_mode ^ S_IXUSR) &&
				!lstat(path, &st2) &&
				st1.st_mode != st2.st_mode &&
				!chmod(path, st1.st_mode));
		if (filemode && !reinit && (st1.st_mode & S_IXUSR))
			filemode = 0;
	}
	git_config_set("core.filemode", filemode ? "true" : "false");

	if (is_bare_repository())
		git_config_set("core.bare", "true");
	else {
		git_config_set("core.bare", "false");
		/* allow template config file to override the default */
		if (log_all_ref_updates == LOG_REFS_UNSET)
			git_config_set("core.logallrefupdates", "true");
		if (needs_work_tree_config(original_git_dir, work_tree))
			git_config_set("core.worktree", work_tree);
	}

	if (!reinit) {
		/* Check if symlink is supported in the work tree */
		path = git_path_buf(&buf, "tXXXXXX");
		if (!close(xmkstemp(path)) &&
		    !unlink(path) &&
		    !symlink("testing", path) &&
		    !lstat(path, &st1) &&
		    S_ISLNK(st1.st_mode))
			unlink(path); /* good */
		else
			git_config_set("core.symlinks", "false");

		/* Check if the filesystem is case-insensitive */
		path = git_path_buf(&buf, "CoNfIg");
		if (!access(path, F_OK))
			git_config_set("core.ignorecase", "true");
		probe_utf8_pathname_composition();
	}

	strbuf_release(&buf);
	return reinit;
}

static void create_object_directory(void)
{
	struct strbuf path = STRBUF_INIT;
	size_t baselen;

	strbuf_addstr(&path, get_object_directory());
	baselen = path.len;

	safe_create_dir(path.buf, 1);

	strbuf_setlen(&path, baselen);
	strbuf_addstr(&path, "/pack");
	safe_create_dir(path.buf, 1);

	strbuf_setlen(&path, baselen);
	strbuf_addstr(&path, "/info");
	safe_create_dir(path.buf, 1);

	strbuf_release(&path);
}

static void separate_git_dir(const char *git_dir, const char *git_link)
{
	struct stat st;

	if (!stat(git_link, &st)) {
		const char *src;

		if (S_ISREG(st.st_mode))
			src = read_gitfile(git_link);
		else if (S_ISDIR(st.st_mode))
			src = git_link;
		else
			die(_("unable to handle file type %d"), (int)st.st_mode);

		if (rename(src, git_dir))
			die_errno(_("unable to move %s to %s"), src, git_dir);
		repair_worktrees(NULL, NULL);
	}

	write_file(git_link, "gitdir: %s", git_dir);
}

static void validate_hash_algorithm(struct repository_format *repo_fmt, int hash)
{
	const char *env = getenv(GIT_DEFAULT_HASH_ENVIRONMENT);
	/*
	 * If we already have an initialized repo, don't allow the user to
	 * specify a different algorithm, as that could cause corruption.
	 * Otherwise, if the user has specified one on the command line, use it.
	 */
	if (repo_fmt->version >= 0 && hash != GIT_HASH_UNKNOWN && hash != repo_fmt->hash_algo)
		die(_("attempt to reinitialize repository with different hash"));
	else if (hash != GIT_HASH_UNKNOWN)
		repo_fmt->hash_algo = hash;
	else if (env) {
		int env_algo = hash_algo_by_name(env);
		if (env_algo == GIT_HASH_UNKNOWN)
			die(_("unknown hash algorithm '%s'"), env);
		repo_fmt->hash_algo = env_algo;
	}
}

int init_db(const char *git_dir, const char *real_git_dir,
	    const char *template_dir, int hash, const char *initial_branch,
	    unsigned int flags)
{
	int reinit;
	int exist_ok = flags & INIT_DB_EXIST_OK;
	char *original_git_dir = real_pathdup(git_dir, 1);
	struct repository_format repo_fmt = REPOSITORY_FORMAT_INIT;

	if (real_git_dir) {
		struct stat st;

		if (!exist_ok && !stat(git_dir, &st))
			die(_("%s already exists"), git_dir);

		if (!exist_ok && !stat(real_git_dir, &st))
			die(_("%s already exists"), real_git_dir);

		set_git_dir(real_git_dir, 1);
		git_dir = get_git_dir();
		separate_git_dir(git_dir, original_git_dir);
	}
	else {
		set_git_dir(git_dir, 1);
		git_dir = get_git_dir();
	}
	startup_info->have_repository = 1;

	/* Ensure `core.hidedotfiles` is processed */
	git_config(platform_core_config, NULL);

	safe_create_dir(git_dir, 0);

	init_is_bare_repository = is_bare_repository();

	/* Check to see if the repository version is right.
	 * Note that a newly created repository does not have
	 * config file, so this will not fail.  What we are catching
	 * is an attempt to reinitialize new repository with an old tool.
	 */
	check_repository_format(&repo_fmt);

	validate_hash_algorithm(&repo_fmt, hash);

	reinit = create_default_files(template_dir, original_git_dir,
				      initial_branch, &repo_fmt,
				      flags & INIT_DB_QUIET);
	if (reinit && initial_branch)
		warning(_("re-init: ignored --initial-branch=%s"),
			initial_branch);

	create_object_directory();

	if (get_shared_repository()) {
		char buf[10];
		/* We do not spell "group" and such, so that
		 * the configuration can be read by older version
		 * of git. Note, we use octal numbers for new share modes,
		 * and compatibility values for PERM_GROUP and
		 * PERM_EVERYBODY.
		 */
		if (get_shared_repository() < 0)
			/* force to the mode value */
			xsnprintf(buf, sizeof(buf), "0%o", -get_shared_repository());
		else if (get_shared_repository() == PERM_GROUP)
			xsnprintf(buf, sizeof(buf), "%d", OLD_PERM_GROUP);
		else if (get_shared_repository() == PERM_EVERYBODY)
			xsnprintf(buf, sizeof(buf), "%d", OLD_PERM_EVERYBODY);
		else
			BUG("invalid value for shared_repository");
		git_config_set("core.sharedrepository", buf);
		git_config_set("receive.denyNonFastforwards", "true");
	}

	if (!(flags & INIT_DB_QUIET)) {
		int len = strlen(git_dir);

		if (reinit)
			printf(get_shared_repository()
			       ? _("Reinitialized existing shared Git repository in %s%s\n")
			       : _("Reinitialized existing Git repository in %s%s\n"),
			       git_dir, len && git_dir[len-1] != '/' ? "/" : "");
		else
			printf(get_shared_repository()
			       ? _("Initialized empty shared Git repository in %s%s\n")
			       : _("Initialized empty Git repository in %s%s\n"),
			       git_dir, len && git_dir[len-1] != '/' ? "/" : "");
	}

	free(original_git_dir);
	return 0;
}

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
	const char *initial_branch = NULL;
	int hash_algo = GIT_HASH_UNKNOWN;
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
		       initial_branch, flags);
}
