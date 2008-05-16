/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"
#include "builtin.h"
#include "exec_cmd.h"

#ifndef DEFAULT_GIT_TEMPLATE_DIR
#define DEFAULT_GIT_TEMPLATE_DIR "/usr/share/git-core/templates"
#endif

#ifdef NO_TRUSTABLE_FILEMODE
#define TEST_FILEMODE 0
#else
#define TEST_FILEMODE 1
#endif

static void safe_create_dir(const char *dir, int share)
{
	if (mkdir(dir, 0777) < 0) {
		if (errno != EEXIST) {
			perror(dir);
			exit(1);
		}
	}
	else if (share && adjust_shared_perm(dir))
		die("Could not make %s writable by group\n", dir);
}

static void copy_templates_1(char *path, int baselen,
			     char *template, int template_baselen,
			     DIR *dir)
{
	struct dirent *de;

	/* Note: if ".git/hooks" file exists in the repository being
	 * re-initialized, /etc/core-git/templates/hooks/update would
	 * cause git-init to fail here.  I think this is sane but
	 * it means that the set of templates we ship by default, along
	 * with the way the namespace under .git/ is organized, should
	 * be really carefully chosen.
	 */
	safe_create_dir(path, 1);
	while ((de = readdir(dir)) != NULL) {
		struct stat st_git, st_template;
		int namelen;
		int exists = 0;

		if (de->d_name[0] == '.')
			continue;
		namelen = strlen(de->d_name);
		if ((PATH_MAX <= baselen + namelen) ||
		    (PATH_MAX <= template_baselen + namelen))
			die("insanely long template name %s", de->d_name);
		memcpy(path + baselen, de->d_name, namelen+1);
		memcpy(template + template_baselen, de->d_name, namelen+1);
		if (lstat(path, &st_git)) {
			if (errno != ENOENT)
				die("cannot stat %s", path);
		}
		else
			exists = 1;

		if (lstat(template, &st_template))
			die("cannot stat template %s", template);

		if (S_ISDIR(st_template.st_mode)) {
			DIR *subdir = opendir(template);
			int baselen_sub = baselen + namelen;
			int template_baselen_sub = template_baselen + namelen;
			if (!subdir)
				die("cannot opendir %s", template);
			path[baselen_sub++] =
				template[template_baselen_sub++] = '/';
			path[baselen_sub] =
				template[template_baselen_sub] = 0;
			copy_templates_1(path, baselen_sub,
					 template, template_baselen_sub,
					 subdir);
			closedir(subdir);
		}
		else if (exists)
			continue;
		else if (S_ISLNK(st_template.st_mode)) {
			char lnk[256];
			int len;
			len = readlink(template, lnk, sizeof(lnk));
			if (len < 0)
				die("cannot readlink %s", template);
			if (sizeof(lnk) <= len)
				die("insanely long symlink %s", template);
			lnk[len] = 0;
			if (symlink(lnk, path))
				die("cannot symlink %s %s", lnk, path);
		}
		else if (S_ISREG(st_template.st_mode)) {
			if (copy_file(path, template, st_template.st_mode))
				die("cannot copy %s to %s", template, path);
		}
		else
			error("ignoring template %s", template);
	}
}

static void copy_templates(const char *git_dir, int len, const char *template_dir)
{
	char path[PATH_MAX];
	char template_path[PATH_MAX];
	int template_len;
	DIR *dir;

	if (!template_dir)
		template_dir = getenv(TEMPLATE_DIR_ENVIRONMENT);
	if (!template_dir) {
		/*
		 * if the hard-coded template is relative, it is
		 * interpreted relative to the exec_dir
		 */
		template_dir = DEFAULT_GIT_TEMPLATE_DIR;
		if (!is_absolute_path(template_dir)) {
			struct strbuf d = STRBUF_INIT;
			strbuf_addf(&d, "%s/%s", git_exec_path(), template_dir);
			template_dir = strbuf_detach(&d, NULL);
		}
	}
	strcpy(template_path, template_dir);
	template_len = strlen(template_path);
	if (template_path[template_len-1] != '/') {
		template_path[template_len++] = '/';
		template_path[template_len] = 0;
	}
	dir = opendir(template_path);
	if (!dir) {
		fprintf(stderr, "warning: templates not found %s\n",
			template_dir);
		return;
	}

	/* Make sure that template is from the correct vintage */
	strcpy(template_path + template_len, "config");
	repository_format_version = 0;
	git_config_from_file(check_repository_format_version,
			     template_path);
	template_path[template_len] = 0;

	if (repository_format_version &&
	    repository_format_version != GIT_REPO_VERSION) {
		fprintf(stderr, "warning: not copying templates of "
			"a wrong format version %d from '%s'\n",
			repository_format_version,
			template_dir);
		closedir(dir);
		return;
	}

	memcpy(path, git_dir, len);
	path[len] = 0;
	copy_templates_1(path, len,
			 template_path, template_len,
			 dir);
	closedir(dir);
}

static int create_default_files(const char *git_dir, const char *template_path)
{
	unsigned len = strlen(git_dir);
	static char path[PATH_MAX];
	struct stat st1;
	char repo_version_string[10];
	char junk[2];
	int reinit;
	int filemode;

	if (len > sizeof(path)-50)
		die("insane git directory %s", git_dir);
	memcpy(path, git_dir, len);

	if (len && path[len-1] != '/')
		path[len++] = '/';

	/*
	 * Create .git/refs/{heads,tags}
	 */
	strcpy(path + len, "refs");
	safe_create_dir(path, 1);
	strcpy(path + len, "refs/heads");
	safe_create_dir(path, 1);
	strcpy(path + len, "refs/tags");
	safe_create_dir(path, 1);

	/* First copy the templates -- we might have the default
	 * config file there, in which case we would want to read
	 * from it after installing.
	 */
	path[len] = 0;
	copy_templates(path, len, template_path);

	git_config(git_default_config);

	/*
	 * We would have created the above under user's umask -- under
	 * shared-repository settings, we would need to fix them up.
	 */
	if (shared_repository) {
		path[len] = 0;
		adjust_shared_perm(path);
		strcpy(path + len, "refs");
		adjust_shared_perm(path);
		strcpy(path + len, "refs/heads");
		adjust_shared_perm(path);
		strcpy(path + len, "refs/tags");
		adjust_shared_perm(path);
	}

	/*
	 * Create the default symlink from ".git/HEAD" to the "master"
	 * branch, if it does not exist yet.
	 */
	strcpy(path + len, "HEAD");
	reinit = (!access(path, R_OK)
		  || readlink(path, junk, sizeof(junk)-1) != -1);
	if (!reinit) {
		if (create_symref("HEAD", "refs/heads/master", NULL) < 0)
			exit(1);
	}

	/* This forces creation of new config file */
	sprintf(repo_version_string, "%d", GIT_REPO_VERSION);
	git_config_set("core.repositoryformatversion", repo_version_string);

	path[len] = 0;
	strcpy(path + len, "config");

	/* Check filemode trustability */
	filemode = TEST_FILEMODE;
	if (TEST_FILEMODE && !lstat(path, &st1)) {
		struct stat st2;
		filemode = (!chmod(path, st1.st_mode ^ S_IXUSR) &&
				!lstat(path, &st2) &&
				st1.st_mode != st2.st_mode);
	}
	git_config_set("core.filemode", filemode ? "true" : "false");

	if (is_bare_repository())
		git_config_set("core.bare", "true");
	else {
		const char *work_tree = get_git_work_tree();
		git_config_set("core.bare", "false");
		/* allow template config file to override the default */
		if (log_all_ref_updates == -1)
		    git_config_set("core.logallrefupdates", "true");
		if (work_tree != git_work_tree_cfg)
			git_config_set("core.worktree", work_tree);
	}

	/* Check if symlink is supported in the work tree */
	if (!reinit) {
		path[len] = 0;
		strcpy(path + len, "tXXXXXX");
		if (!close(xmkstemp(path)) &&
		    !unlink(path) &&
		    !symlink("testing", path) &&
		    !lstat(path, &st1) &&
		    S_ISLNK(st1.st_mode))
			unlink(path); /* good */
		else
			git_config_set("core.symlinks", "false");
	}

	return reinit;
}

static void guess_repository_type(const char *git_dir)
{
	char cwd[PATH_MAX];
	const char *slash;

	if (0 <= is_bare_repository_cfg)
		return;
	if (!git_dir)
		return;

	/*
	 * "GIT_DIR=. git init" is always bare.
	 * "GIT_DIR=`pwd` git init" too.
	 */
	if (!strcmp(".", git_dir))
		goto force_bare;
	if (!getcwd(cwd, sizeof(cwd)))
		die("cannot tell cwd");
	if (!strcmp(git_dir, cwd))
		goto force_bare;
	/*
	 * "GIT_DIR=.git or GIT_DIR=something/.git is usually not.
	 */
	if (!strcmp(git_dir, ".git"))
		return;
	slash = strrchr(git_dir, '/');
	if (slash && !strcmp(slash, "/.git"))
		return;

	/*
	 * Otherwise it is often bare.  At this point
	 * we are just guessing.
	 */
 force_bare:
	is_bare_repository_cfg = 1;
	return;
}

static const char init_db_usage[] =
"git-init [-q | --quiet] [--template=<template-directory>] [--shared]";

/*
 * If you want to, you can share the DB area with any number of branches.
 * That has advantages: you can save space by sharing all the SHA1 objects.
 * On the other hand, it might just make lookup slower and messier. You
 * be the judge.  The default case is to have one DB per managed directory.
 */
int cmd_init_db(int argc, const char **argv, const char *prefix)
{
	const char *git_dir;
	const char *sha1_dir;
	const char *template_dir = NULL;
	char *path;
	int len, i, reinit;
	int quiet = 0;

	for (i = 1; i < argc; i++, argv++) {
		const char *arg = argv[1];
		if (!prefixcmp(arg, "--template="))
			template_dir = arg+11;
		else if (!strcmp(arg, "--shared"))
			shared_repository = PERM_GROUP;
		else if (!prefixcmp(arg, "--shared="))
			shared_repository = git_config_perm("arg", arg+9);
		else if (!strcmp(arg, "-q") || !strcmp(arg, "--quiet"))
		        quiet = 1;
		else
			usage(init_db_usage);
	}

	/*
	 * GIT_WORK_TREE makes sense only in conjunction with GIT_DIR
	 * without --bare.  Catch the error early.
	 */
	git_dir = getenv(GIT_DIR_ENVIRONMENT);
	if ((!git_dir || is_bare_repository_cfg == 1)
	    && getenv(GIT_WORK_TREE_ENVIRONMENT))
		die("%s (or --work-tree=<directory>) not allowed without "
		    "specifying %s (or --git-dir=<directory>)",
		    GIT_WORK_TREE_ENVIRONMENT,
		    GIT_DIR_ENVIRONMENT);

	guess_repository_type(git_dir);

	if (is_bare_repository_cfg <= 0) {
		git_work_tree_cfg = xcalloc(PATH_MAX, 1);
		if (!getcwd(git_work_tree_cfg, PATH_MAX))
			die ("Cannot access current working directory.");
		if (access(get_git_work_tree(), X_OK))
			die ("Cannot access work tree '%s'",
			     get_git_work_tree());
	}

	/*
	 * Set up the default .git directory contents
	 */
	git_dir = getenv(GIT_DIR_ENVIRONMENT);
	if (!git_dir)
		git_dir = DEFAULT_GIT_DIR_ENVIRONMENT;
	safe_create_dir(git_dir, 0);

	/* Check to see if the repository version is right.
	 * Note that a newly created repository does not have
	 * config file, so this will not fail.  What we are catching
	 * is an attempt to reinitialize new repository with an old tool.
	 */
	check_repository_format();

	reinit = create_default_files(git_dir, template_dir);

	/*
	 * And set up the object store.
	 */
	sha1_dir = get_object_directory();
	len = strlen(sha1_dir);
	path = xmalloc(len + 40);
	memcpy(path, sha1_dir, len);

	safe_create_dir(sha1_dir, 1);
	strcpy(path+len, "/pack");
	safe_create_dir(path, 1);
	strcpy(path+len, "/info");
	safe_create_dir(path, 1);

	if (shared_repository) {
		char buf[10];
		/* We do not spell "group" and such, so that
		 * the configuration can be read by older version
		 * of git. Note, we use octal numbers for new share modes,
		 * and compatibility values for PERM_GROUP and
		 * PERM_EVERYBODY.
		 */
		if (shared_repository == PERM_GROUP)
			sprintf(buf, "%d", OLD_PERM_GROUP);
		else if (shared_repository == PERM_EVERYBODY)
			sprintf(buf, "%d", OLD_PERM_EVERYBODY);
		else
			sprintf(buf, "0%o", shared_repository);
		git_config_set("core.sharedrepository", buf);
		git_config_set("receive.denyNonFastforwards", "true");
	}

	if (!quiet)
		printf("%s%s Git repository in %s/\n",
		       reinit ? "Reinitialized existing" : "Initialized empty",
		       shared_repository ? " shared" : "",
		       git_dir);

	return 0;
}
