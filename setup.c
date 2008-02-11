#include "cache.h"
#include "dir.h"

static int inside_git_dir = -1;
static int inside_work_tree = -1;

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
	if (!pfx || !*pfx || is_absolute_path(arg))
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
	if (errno != ENOENT && errno != ENOTDIR)
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
 *  - either an objects/ directory _or_ the proper
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

int is_inside_git_dir(void)
{
	if (inside_git_dir < 0)
		inside_git_dir = is_inside_dir(get_git_dir());
	return inside_git_dir;
}

int is_inside_work_tree(void)
{
	if (inside_work_tree < 0)
		inside_work_tree = is_inside_dir(get_git_work_tree());
	return inside_work_tree;
}

/*
 * set_work_tree() is only ever called if you set GIT_DIR explicitely.
 * The old behaviour (which we retain here) is to set the work tree root
 * to the cwd, unless overridden by the config, the command line, or
 * GIT_WORK_TREE.
 */
static const char *set_work_tree(const char *dir)
{
	char buffer[PATH_MAX + 1];

	if (!getcwd(buffer, sizeof(buffer)))
		die ("Could not get the current working directory");
	git_work_tree_cfg = xstrdup(buffer);
	inside_work_tree = 1;

	return NULL;
}

void setup_work_tree(void)
{
	const char *work_tree, *git_dir;
	static int initialized = 0;

	if (initialized)
		return;
	work_tree = get_git_work_tree();
	git_dir = get_git_dir();
	if (!is_absolute_path(git_dir))
		set_git_dir(make_absolute_path(git_dir));
	if (!work_tree || chdir(work_tree))
		die("This operation must be run in a work tree");
	initialized = 1;
}

static int check_repository_format_gently(int *nongit_ok)
{
	git_config(check_repository_format_version);
	if (GIT_REPO_VERSION < repository_format_version) {
		if (!nongit_ok)
			die ("Expected git repo version <= %d, found %d",
			     GIT_REPO_VERSION, repository_format_version);
		warning("Expected git repo version <= %d, found %d",
			GIT_REPO_VERSION, repository_format_version);
		warning("Please upgrade Git");
		*nongit_ok = -1;
		return -1;
	}
	return 0;
}

/*
 * We cannot decide in this function whether we are in the work tree or
 * not, since the config can only be read _after_ this function was called.
 */
const char *setup_git_directory_gently(int *nongit_ok)
{
	const char *work_tree_env = getenv(GIT_WORK_TREE_ENVIRONMENT);
	static char cwd[PATH_MAX+1];
	const char *gitdirenv;
	int len, offset;

	/*
	 * If GIT_DIR is set explicitly, we're not going
	 * to do any discovery, but we still do repository
	 * validation.
	 */
	gitdirenv = getenv(GIT_DIR_ENVIRONMENT);
	if (gitdirenv) {
		if (PATH_MAX - 40 < strlen(gitdirenv))
			die("'$%s' too big", GIT_DIR_ENVIRONMENT);
		if (is_git_directory(gitdirenv)) {
			static char buffer[1024 + 1];
			const char *retval;

			if (!work_tree_env) {
				retval = set_work_tree(gitdirenv);
				/* config may override worktree */
				if (check_repository_format_gently(nongit_ok))
					return NULL;
				return retval;
			}
			if (check_repository_format_gently(nongit_ok))
				return NULL;
			retval = get_relative_cwd(buffer, sizeof(buffer) - 1,
					get_git_work_tree());
			if (!retval || !*retval)
				return NULL;
			set_git_dir(make_absolute_path(gitdirenv));
			if (chdir(work_tree_env) < 0)
				die ("Could not chdir to %s", work_tree_env);
			strcat(buffer, "/");
			return retval;
		}
		if (nongit_ok) {
			*nongit_ok = 1;
			return NULL;
		}
		die("Not a git repository: '%s'", gitdirenv);
	}

	if (!getcwd(cwd, sizeof(cwd)-1))
		die("Unable to read current working directory");

	/*
	 * Test in the following order (relative to the cwd):
	 * - .git/
	 * - ./ (bare)
	 * - ../.git/
	 * - ../ (bare)
	 * - ../../.git/
	 *   etc.
	 */
	offset = len = strlen(cwd);
	for (;;) {
		if (is_git_directory(DEFAULT_GIT_DIR_ENVIRONMENT))
			break;
		if (is_git_directory(".")) {
			inside_git_dir = 1;
			if (!work_tree_env)
				inside_work_tree = 0;
			setenv(GIT_DIR_ENVIRONMENT, ".", 1);
			check_repository_format_gently(nongit_ok);
			return NULL;
		}
		chdir("..");
		do {
			if (!offset) {
				if (nongit_ok) {
					if (chdir(cwd))
						die("Cannot come back to cwd");
					*nongit_ok = 1;
					return NULL;
				}
				die("Not a git repository");
			}
		} while (cwd[--offset] != '/');
	}

	inside_git_dir = 0;
	if (!work_tree_env)
		inside_work_tree = 1;
	git_work_tree_cfg = xstrndup(cwd, offset);
	if (check_repository_format_gently(nongit_ok))
		return NULL;
	if (offset == len)
		return NULL;

	/* Make "offset" point to past the '/', and add a '/' at the end */
	offset++;
	cwd[len++] = '/';
	cwd[len] = 0;
	return cwd + offset;
}

int git_config_perm(const char *var, const char *value)
{
	if (value) {
		int i;
		if (!strcmp(value, "umask"))
			return PERM_UMASK;
		if (!strcmp(value, "group"))
			return PERM_GROUP;
		if (!strcmp(value, "all") ||
		    !strcmp(value, "world") ||
		    !strcmp(value, "everybody"))
			return PERM_EVERYBODY;
		i = atoi(value);
		if (i > 1)
			return i;
	}
	return git_config_bool(var, value);
}

int check_repository_format_version(const char *var, const char *value)
{
	if (strcmp(var, "core.repositoryformatversion") == 0)
		repository_format_version = git_config_int(var, value);
	else if (strcmp(var, "core.sharedrepository") == 0)
		shared_repository = git_config_perm(var, value);
	else if (strcmp(var, "core.bare") == 0) {
		is_bare_repository_cfg = git_config_bool(var, value);
		if (is_bare_repository_cfg == 1)
			inside_work_tree = -1;
	} else if (strcmp(var, "core.worktree") == 0) {
		if (!value)
			return config_error_nonbool(var);
		if (git_work_tree_cfg)
			free(git_work_tree_cfg);
		git_work_tree_cfg = xstrdup(value);
		inside_work_tree = -1;
	}
	return 0;
}

int check_repository_format(void)
{
	return check_repository_format_gently(NULL);
}

const char *setup_git_directory(void)
{
	const char *retval = setup_git_directory_gently(NULL);

	/* If the work tree is not the default one, recompute prefix */
	if (inside_work_tree < 0) {
		static char buffer[PATH_MAX + 1];
		char *rel;
		if (retval && chdir(retval))
			die ("Could not jump back into original cwd");
		rel = get_relative_cwd(buffer, PATH_MAX, get_git_work_tree());
		return rel && *rel ? strcat(rel, "/") : NULL;
	}

	return retval;
}
