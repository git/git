/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "cache.h"

static void safe_create_dir(const char *dir)
{
	if (mkdir(dir, 0777) < 0) {
		if (errno != EEXIST) {
			perror(dir);
			exit(1);
		}
	}
}

static void create_default_files(const char *git_dir)
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
}

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
	char *path;
	int len, i;

	/*
	 * Set up the default .git directory contents
	 */
	git_dir = gitenv(GIT_DIR_ENVIRONMENT);
	if (!git_dir) {
		git_dir = DEFAULT_GIT_DIR_ENVIRONMENT;
		fprintf(stderr, "defaulting to local storage area\n");
	}
	safe_create_dir(git_dir);
	create_default_files(git_dir);

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
