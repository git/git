#include "cache.h"

const char *prefix_path(const char *prefix, int len, const char *path)
{
	const char *orig = path;
	for (;;) {
		char c;
		if (*path != '.')
			break;
		c = path[1];
		/* "." */
		if (!c) {
			path++;
			break;
		}
		/* "./" */
		if (c == '/') {
			path += 2;
			continue;
		}
		if (c != '.')
			break;
		c = path[2];
		if (!c)
			path += 2;
		else if (c == '/')
			path += 3;
		else
			break;
		/* ".." and "../" */
		/* Remove last component of the prefix */
		do {
			if (!len)
				die("'%s' is outside repository", orig);
			len--;
		} while (len && prefix[len-1] != '/');
		continue;
	}
	if (len) {
		int speclen = strlen(path);
		char *n = xmalloc(speclen + len + 1);

		memcpy(n, prefix, len);
		memcpy(n + len, path, speclen+1);
		path = n;
	}
	return path;
}

/*
 * Unlike prefix_path, this should be used if the named file does
 * not have to interact with index entry; i.e. name of a random file
 * on the filesystem.
 */
const char *prefix_filename(const char *pfx, int pfx_len, const char *arg)
{
	static char path[PATH_MAX];
	if (!pfx || !*pfx || arg[0] == '/')
		return arg;
	memcpy(path, pfx, pfx_len);
	strcpy(path + pfx_len, arg);
	return path;
}

/*
 * Verify a filename that we got as an argument for a pathspec
 * entry. Note that a filename that begins with "-" never verifies
 * as true, because even if such a filename were to exist, we want
 * it to be preceded by the "--" marker (or we want the user to
 * use a format like "./-filename")
 */
void verify_filename(const char *prefix, const char *arg)
{
	const char *name;
	struct stat st;

	if (*arg == '-')
		die("bad flag '%s' used after filename", arg);
	name = prefix ? prefix_filename(prefix, strlen(prefix), arg) : arg;
	if (!lstat(name, &st))
		return;
	if (errno == ENOENT)
		die("ambiguous argument '%s': unknown revision or path not in the working tree.\n"
		    "Use '--' to separate paths from revisions", arg);
	die("'%s': %s", arg, strerror(errno));
}

/*
 * Opposite of the above: the command line did not have -- marker
 * and we parsed the arg as a refname.  It should not be interpretable
 * as a filename.
 */
void verify_non_filename(const char *prefix, const char *arg)
{
	const char *name;
	struct stat st;

	if (!is_inside_work_tree() || is_inside_git_dir())
		return;
	if (*arg == '-')
		return; /* flag */
	name = prefix ? prefix_filename(prefix, strlen(prefix), arg) : arg;
	if (!lstat(name, &st))
		die("ambiguous argument '%s': both revision and filename\n"
		    "Use '--' to separate filenames from revisions", arg);
	if (errno != ENOENT)
		die("'%s': %s", arg, strerror(errno));
}

const char **get_pathspec(const char *prefix, const char **pathspec)
{
	const char *entry = *pathspec;
	const char **p;
	int prefixlen;

	if (!prefix && !entry)
		return NULL;

	if (!entry) {
		static const char *spec[2];
		spec[0] = prefix;
		spec[1] = NULL;
		return spec;
	}

	/* Otherwise we have to re-write the entries.. */
	p = pathspec;
	prefixlen = prefix ? strlen(prefix) : 0;
	do {
		*p = prefix_path(prefix, prefixlen, entry);
	} while ((entry = *++p) != NULL);
	return (const char **) pathspec;
}

/*
 * Test if it looks like we're at a git directory.
 * We want to see:
 *
 *  - either a objects/ directory _or_ the proper
 *    GIT_OBJECT_DIRECTORY environment variable
 *  - a refs/ directory
 *  - either a HEAD symlink or a HEAD file that is formatted as
 *    a proper "ref:", or a regular file HEAD that has a properly
 *    formatted sha1 object name.
 */
static int is_git_directory(const char *suspect)
{
	char path[PATH_MAX];
	size_t len = strlen(suspect);

	strcpy(path, suspect);
	if (getenv(DB_ENVIRONMENT)) {
		if (access(getenv(DB_ENVIRONMENT), X_OK))
			return 0;
	}
	else {
		strcpy(path + len, "/objects");
		if (access(path, X_OK))
			return 0;
	}

	strcpy(path + len, "/refs");
	if (access(path, X_OK))
		return 0;

	strcpy(path + len, "/HEAD");
	if (validate_headref(path))
		return 0;

	return 1;
}

static int inside_git_dir = -1;

int is_inside_git_dir(void)
{
	if (inside_git_dir >= 0)
		return inside_git_dir;
	die("BUG: is_inside_git_dir called before setup_git_directory");
}

static int inside_work_tree = -1;

int is_inside_work_tree(void)
{
	if (inside_git_dir >= 0)
		return inside_work_tree;
	die("BUG: is_inside_work_tree called before setup_git_directory");
}

static char *gitworktree_config;

static int git_setup_config(const char *var, const char *value)
{
	if (!strcmp(var, "core.worktree")) {
		if (gitworktree_config)
			strlcpy(gitworktree_config, value, PATH_MAX);
		return 0;
	}
	return git_default_config(var, value);
}

const char *setup_git_directory_gently(int *nongit_ok)
{
	static char cwd[PATH_MAX+1];
	char worktree[PATH_MAX+1], gitdir[PATH_MAX+1];
	const char *gitdirenv, *gitworktree;
	int wt_rel_gitdir = 0;

	gitdirenv = getenv(GIT_DIR_ENVIRONMENT);
	if (!gitdirenv) {
		int len, offset;

		if (!getcwd(cwd, sizeof(cwd)-1) || cwd[0] != '/')
			die("Unable to read current working directory");

		offset = len = strlen(cwd);
		for (;;) {
			if (is_git_directory(".git"))
				break;
			if (offset == 0) {
				offset = -1;
				break;
			}
			chdir("..");
			while (cwd[--offset] != '/')
				; /* do nothing */
		}

		if (offset >= 0) {
			inside_work_tree = 1;
			git_config(git_default_config);
			if (offset == len) {
				inside_git_dir = 0;
				return NULL;
			}

			cwd[len++] = '/';
			cwd[len] = '\0';
			inside_git_dir = !prefixcmp(cwd + offset + 1, ".git/");
			return cwd + offset + 1;
		}

		if (chdir(cwd))
			die("Cannot come back to cwd");
		if (!is_git_directory(".")) {
			if (nongit_ok) {
				*nongit_ok = 1;
				return NULL;
			}
			die("Not a git repository");
		}
		setenv(GIT_DIR_ENVIRONMENT, cwd, 1);
		gitdirenv = getenv(GIT_DIR_ENVIRONMENT);
		if (!gitdirenv)
			die("getenv after setenv failed");
	}

	if (PATH_MAX - 40 < strlen(gitdirenv)) {
		if (nongit_ok) {
			*nongit_ok = 1;
			return NULL;
		}
		die("$%s too big", GIT_DIR_ENVIRONMENT);
	}
	if (!is_git_directory(gitdirenv)) {
		if (nongit_ok) {
			*nongit_ok = 1;
			return NULL;
		}
		die("Not a git repository: '%s'", gitdirenv);
	}

	if (!getcwd(cwd, sizeof(cwd)-1) || cwd[0] != '/')
		die("Unable to read current working directory");
	if (chdir(gitdirenv)) {
		if (nongit_ok) {
			*nongit_ok = 1;
			return NULL;
		}
		die("Cannot change directory to $%s '%s'",
			GIT_DIR_ENVIRONMENT, gitdirenv);
	}
	if (!getcwd(gitdir, sizeof(gitdir)-1) || gitdir[0] != '/')
		die("Unable to read current working directory");
	if (chdir(cwd))
		die("Cannot come back to cwd");

	/*
	 * In case there is a work tree we may change the directory,
	 * therefore make GIT_DIR an absolute path.
	 */
	if (gitdirenv[0] != '/') {
		setenv(GIT_DIR_ENVIRONMENT, gitdir, 1);
		gitdirenv = getenv(GIT_DIR_ENVIRONMENT);
		if (!gitdirenv)
			die("getenv after setenv failed");
		if (PATH_MAX - 40 < strlen(gitdirenv)) {
			if (nongit_ok) {
				*nongit_ok = 1;
				return NULL;
			}
			die("$%s too big after expansion to absolute path",
				GIT_DIR_ENVIRONMENT);
		}
	}

	strcat(cwd, "/");
	strcat(gitdir, "/");
	inside_git_dir = !prefixcmp(cwd, gitdir);

	gitworktree = getenv(GIT_WORK_TREE_ENVIRONMENT);
	if (!gitworktree) {
		gitworktree_config = worktree;
		worktree[0] = '\0';
	}
	git_config(git_setup_config);
	if (!gitworktree) {
		gitworktree_config = NULL;
		if (worktree[0])
			gitworktree = worktree;
		if (gitworktree && gitworktree[0] != '/')
			wt_rel_gitdir = 1;
	}

	if (wt_rel_gitdir && chdir(gitdirenv))
		die("Cannot change directory to $%s '%s'",
			GIT_DIR_ENVIRONMENT, gitdirenv);
	if (gitworktree && chdir(gitworktree)) {
		if (nongit_ok) {
			if (wt_rel_gitdir && chdir(cwd))
				die("Cannot come back to cwd");
			*nongit_ok = 1;
			return NULL;
		}
		if (wt_rel_gitdir)
			die("Cannot change directory to working tree '%s'"
				" from $%s", gitworktree, GIT_DIR_ENVIRONMENT);
		else
			die("Cannot change directory to working tree '%s'",
				gitworktree);
	}
	if (!getcwd(worktree, sizeof(worktree)-1) || worktree[0] != '/')
		die("Unable to read current working directory");
	strcat(worktree, "/");
	inside_work_tree = !prefixcmp(cwd, worktree);

	if (gitworktree && inside_work_tree && !prefixcmp(worktree, gitdir) &&
	    strcmp(worktree, gitdir)) {
		inside_git_dir = 0;
	}

	if (!inside_work_tree) {
		if (chdir(cwd))
			die("Cannot come back to cwd");
		return NULL;
	}

	if (!strcmp(cwd, worktree))
		return NULL;
	return cwd+strlen(worktree);
}

int git_config_perm(const char *var, const char *value)
{
	if (value) {
		if (!strcmp(value, "umask"))
			return PERM_UMASK;
		if (!strcmp(value, "group"))
			return PERM_GROUP;
		if (!strcmp(value, "all") ||
		    !strcmp(value, "world") ||
		    !strcmp(value, "everybody"))
			return PERM_EVERYBODY;
	}
	return git_config_bool(var, value);
}

int check_repository_format_version(const char *var, const char *value)
{
       if (strcmp(var, "core.repositoryformatversion") == 0)
               repository_format_version = git_config_int(var, value);
	else if (strcmp(var, "core.sharedrepository") == 0)
		shared_repository = git_config_perm(var, value);
       return 0;
}

int check_repository_format(void)
{
	git_config(check_repository_format_version);
	if (GIT_REPO_VERSION < repository_format_version)
		die ("Expected git repo version <= %d, found %d",
		     GIT_REPO_VERSION, repository_format_version);
	return 0;
}

const char *setup_git_directory(void)
{
	const char *retval = setup_git_directory_gently(NULL);
	check_repository_format();
	return retval;
}
