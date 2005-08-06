/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

#ifndef DEFAULT_GIT_TEMPLATE_DIR
#define DEFAULT_GIT_TEMPLATE_DIR "/usr/share/git-core/templates/"
#endif

static void safe_create_dir(const char *dir)
{
	if (mkdir(dir, 0777) < 0) {
		if (errno != EEXIST) {
			perror(dir);
			exit(1);
		}
	}
}

static int copy_file(const char *dst, const char *src, int mode)
{
	int fdi, fdo;

	mode = (mode & 0111) ? 0777 : 0666;
	if ((fdi = open(src, O_RDONLY)) < 0)
		return fdi;
	if ((fdo = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode)) < 0) {
		close(fdi);
		return fdo;
	}
	while (1) {
		char buf[BUFSIZ];
		ssize_t leni, leno, ofs;
		leni = read(fdi, buf, sizeof(buf));
		if (leni < 0) {
		error_return:
			close(fdo);
			close(fdi);
			return -1;
		}
		if (!leni)
			break;
		ofs = 0;
		do {
			leno = write(fdo, buf+ofs, leni);
			if (leno < 0)
				goto error_return;
			leni -= leno;
			ofs += leno;
		} while (0 < leni);
	}
	close(fdo);
	close(fdi);
	return 0;
}

static void copy_templates_1(char *path, int baselen,
			     char *template, int template_baselen,
			     DIR *dir)
{
	struct dirent *de;

	/* Note: if ".git/hooks" file exists in the repository being
	 * re-initialized, /etc/core-git/templates/hooks/update would
	 * cause git-init-db to fail here.  I think this is sane but
	 * it means that the set of templates we ship by default, along
	 * with the way the namespace under .git/ is organized, should
	 * be really carefully chosen.
	 */
	safe_create_dir(path);
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

static void copy_templates(const char *git_dir, int len, char *template_dir)
{
	char path[PATH_MAX];
	char template_path[PATH_MAX];
	int template_len;
	DIR *dir;

	if (!template_dir)
		template_dir = DEFAULT_GIT_TEMPLATE_DIR;
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

	memcpy(path, git_dir, len);
	copy_templates_1(path, len,
			 template_path, template_len,
			 dir);
	closedir(dir);
}

static void create_default_files(const char *git_dir,
				 char *template_path)
{
	unsigned len = strlen(git_dir);
	static char path[PATH_MAX];

	if (len > sizeof(path)-50)
		die("insane git directory %s", git_dir);
	memcpy(path, git_dir, len);

	if (len && path[len-1] != '/')
		path[len++] = '/';

	/*
	 * Create .git/refs/{heads,tags}
	 */
	strcpy(path + len, "refs");
	safe_create_dir(path);
	strcpy(path + len, "refs/heads");
	safe_create_dir(path);
	strcpy(path + len, "refs/tags");
	safe_create_dir(path);

	/*
	 * Create the default symlink from ".git/HEAD" to the "master"
	 * branch
	 */
	strcpy(path + len, "HEAD");
	if (symlink("refs/heads/master", path) < 0) {
		if (errno != EEXIST) {
			perror(path);
			exit(1);
		}
	}
	copy_templates(path, len, template_path);
}

static const char init_db_usage[] =
"git-init-db [--template=<template-directory>]";

/*
 * If you want to, you can share the DB area with any number of branches.
 * That has advantages: you can save space by sharing all the SHA1 objects.
 * On the other hand, it might just make lookup slower and messier. You
 * be the judge.  The default case is to have one DB per managed directory.
 */
int main(int argc, char **argv)
{
	const char *git_dir;
	const char *sha1_dir;
	char *path, *template_dir = NULL;
	int len, i;

	for (i = 1; i < argc; i++, argv++) {
		char *arg = argv[1];
		if (arg[0] != '-')
			break;
		else if (!strncmp(arg, "--template=", 11))
			template_dir = arg+11;
		else
			die(init_db_usage);
	}

	/*
	 * Set up the default .git directory contents
	 */
	git_dir = gitenv(GIT_DIR_ENVIRONMENT);
	if (!git_dir) {
		git_dir = DEFAULT_GIT_DIR_ENVIRONMENT;
		fprintf(stderr, "defaulting to local storage area\n");
	}
	safe_create_dir(git_dir);
	create_default_files(git_dir, template_dir);

	/*
	 * And set up the object store.
	 */
	sha1_dir = get_object_directory();
	len = strlen(sha1_dir);
	path = xmalloc(len + 40);
	memcpy(path, sha1_dir, len);

	safe_create_dir(sha1_dir);
	for (i = 0; i < 256; i++) {
		sprintf(path+len, "/%02x", i);
		safe_create_dir(path);
	}
	strcpy(path+len, "/pack");
	safe_create_dir(path);
	return 0;
}
