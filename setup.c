#include "cache.h"
#include "dir.h"

static int inside_git_dir = -1;
static int inside_work_tree = -1;

const char *prefix_path(const char *prefix, int len, const char *path)
{
	const char *orig = path;
	char *sanitized = xmalloc(len + strlen(path) + 1);
	if (is_absolute_path(orig))
		strcpy(sanitized, path);
	else {
		if (len)
			memcpy(sanitized, prefix, len);
		strcpy(sanitized + len, path);
	}
	if (normalize_path_copy(sanitized, sanitized))
		goto error_out;
	if (is_absolute_path(orig)) {
		size_t root_len, len, total;
		const char *work_tree = get_git_work_tree();
		if (!work_tree)
			goto error_out;
		len = strlen(work_tree);
		root_len = offset_1st_component(work_tree);
		total = strlen(sanitized) + 1;
		if (strncmp(sanitized, work_tree, len) ||
		    (len > root_len && sanitized[len] != '\0' && sanitized[len] != '/')) {
		error_out:
			die("'%s' is outside repository", orig);
		}
		if (sanitized[len] == '/')
			len++;
		memmove(sanitized, sanitized + len, total - len);
	}
	return sanitized;
}

/*
 * Unlike prefix_path, this should be used if the named file does
 * not have to interact with index entry; i.e. name of a random file
 * on the filesystem.
 */
const char *prefix_filename(const char *pfx, int pfx_len, const char *arg)
{
	static char path[PATH_MAX];
#ifndef WIN32
	if (!pfx || !*pfx || is_absolute_path(arg))
		return arg;
	memcpy(path, pfx, pfx_len);
	strcpy(path + pfx_len, arg);
#else
	char *p;
	/* don't add prefix to absolute paths, but still replace '\' by '/' */
	if (is_absolute_path(arg))
		pfx_len = 0;
	else
		memcpy(path, pfx, pfx_len);
	strcpy(path + pfx_len, arg);
	for (p = path + pfx_len; *p; p++)
		if (*p == '\\')
			*p = '/';
#endif
	return path;
}

int check_filename(const char *prefix, const char *arg)
{
	const char *name;
	struct stat st;

	name = prefix ? prefix_filename(prefix, strlen(prefix), arg) : arg;
	if (!lstat(name, &st))
		return 1; /* file exists */
	if (errno == ENOENT || errno == ENOTDIR)
		return 0; /* file does not exist */
	die_errno("failed to stat '%s'", arg);
}

static void NORETURN die_verify_filename(const char *prefix, const char *arg)
{
	unsigned char sha1[20];
	unsigned mode;
	/* try a detailed diagnostic ... */
	get_sha1_with_mode_1(arg, sha1, &mode, 0, prefix);
	/* ... or fall back the most general message. */
	die("ambiguous argument '%s': unknown revision or path not in the working tree.\n"
	    "Use '--' to separate paths from revisions", arg);

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
	if (*arg == '-')
		die("bad flag '%s' used after filename", arg);
	if (check_filename(prefix, arg))
		return;
	die_verify_filename(prefix, arg);
}

/*
 * Opposite of the above: the command line did not have -- marker
 * and we parsed the arg as a refname.  It should not be interpretable
 * as a filename.
 */
void verify_non_filename(const char *prefix, const char *arg)
{
	if (!is_inside_work_tree() || is_inside_git_dir())
		return;
	if (*arg == '-')
		return; /* flag */
	if (!check_filename(prefix, arg))
		return;
	die("ambiguous argument '%s': both revision and filename\n"
	    "Use '--' to separate filenames from revisions", arg);
}

const char **get_pathspec(const char *prefix, const char **pathspec)
{
	const char *entry = *pathspec;
	const char **src, **dst;
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
	src = pathspec;
	dst = pathspec;
	prefixlen = prefix ? strlen(prefix) : 0;
	while (*src) {
		const char *p = prefix_path(prefix, prefixlen, *src);
		*(dst++) = p;
		src++;
	}
	*dst = NULL;
	if (!*pathspec)
		return NULL;
	return pathspec;
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

	if (PATH_MAX <= len + strlen("/objects"))
		die("Too long path: %.*s", 60, suspect);
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
 * set_work_tree() is only ever called if you set GIT_DIR explicitly.
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
		git_dir = make_absolute_path(git_dir);
	if (!work_tree || chdir(work_tree))
		die("This operation must be run in a work tree");
	set_git_dir(make_relative_path(git_dir, work_tree));
	initialized = 1;
}

static int check_repository_format_gently(int *nongit_ok)
{
	git_config(check_repository_format_version, NULL);
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
 * Try to read the location of the git directory from the .git file,
 * return path to git directory if found.
 */
const char *read_gitfile_gently(const char *path)
{
	char *buf;
	char *dir;
	const char *slash;
	struct stat st;
	int fd;
	size_t len;

	if (stat(path, &st))
		return NULL;
	if (!S_ISREG(st.st_mode))
		return NULL;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		die_errno("Error opening '%s'", path);
	buf = xmalloc(st.st_size + 1);
	len = read_in_full(fd, buf, st.st_size);
	close(fd);
	if (len != st.st_size)
		die("Error reading %s", path);
	buf[len] = '\0';
	if (prefixcmp(buf, "gitdir: "))
		die("Invalid gitfile format: %s", path);
	while (buf[len - 1] == '\n' || buf[len - 1] == '\r')
		len--;
	if (len < 9)
		die("No path in gitfile: %s", path);
	buf[len] = '\0';
	dir = buf + 8;

	if (!is_absolute_path(dir) && (slash = strrchr(path, '/'))) {
		size_t pathlen = slash+1 - path;
		size_t dirlen = pathlen + len - 8;
		dir = xmalloc(dirlen + 1);
		strncpy(dir, path, pathlen);
		strncpy(dir + pathlen, buf + 8, len - 8);
		dir[dirlen] = '\0';
		free(buf);
		buf = dir;
	}

	if (!is_git_directory(dir))
		die("Not a git repository: %s", dir);
	path = make_absolute_path(dir);

	free(buf);
	return path;
}

/*
 * We cannot decide in this function whether we are in the work tree or
 * not, since the config can only be read _after_ this function was called.
 */
const char *setup_git_directory_gently(int *nongit_ok)
{
	const char *work_tree_env = getenv(GIT_WORK_TREE_ENVIRONMENT);
	const char *env_ceiling_dirs = getenv(CEILING_DIRECTORIES_ENVIRONMENT);
	static char cwd[PATH_MAX+1];
	const char *gitdirenv;
	const char *gitfile_dir;
	int len, offset, ceil_offset, root_len;
	dev_t current_device = 0;
	int one_filesystem = 1;
	struct stat buf;

	/*
	 * Let's assume that we are in a git repository.
	 * If it turns out later that we are somewhere else, the value will be
	 * updated accordingly.
	 */
	if (nongit_ok)
		*nongit_ok = 0;

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
				die_errno ("Could not chdir to '%s'", work_tree_env);
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
		die_errno("Unable to read current working directory");

	ceil_offset = longest_ancestor_length(cwd, env_ceiling_dirs);
	if (ceil_offset < 0 && has_dos_drive_prefix(cwd))
		ceil_offset = 1;

	/*
	 * Test in the following order (relative to the cwd):
	 * - .git (file containing "gitdir: <path>")
	 * - .git/
	 * - ./ (bare)
	 * - ../.git
	 * - ../.git/
	 * - ../ (bare)
	 * - ../../.git/
	 *   etc.
	 */
	offset = len = strlen(cwd);
	one_filesystem = !git_env_bool("GIT_DISCOVERY_ACROSS_FILESYSTEM", 0);
	if (one_filesystem) {
		if (stat(".", &buf))
			die_errno("failed to stat '.'");
		current_device = buf.st_dev;
	}
	for (;;) {
		gitfile_dir = read_gitfile_gently(DEFAULT_GIT_DIR_ENVIRONMENT);
		if (gitfile_dir) {
			if (set_git_dir(gitfile_dir))
				die("Repository setup failed");
			break;
		}
		if (is_git_directory(DEFAULT_GIT_DIR_ENVIRONMENT))
			break;
		if (is_git_directory(".")) {
			inside_git_dir = 1;
			if (!work_tree_env)
				inside_work_tree = 0;
			if (offset != len) {
				root_len = offset_1st_component(cwd);
				cwd[offset > root_len ? offset : root_len] = '\0';
				set_git_dir(cwd);
			} else
				set_git_dir(".");
			check_repository_format_gently(nongit_ok);
			return NULL;
		}
		while (--offset > ceil_offset && cwd[offset] != '/');
		if (offset <= ceil_offset) {
			if (nongit_ok) {
				if (chdir(cwd))
					die_errno("Cannot come back to cwd");
				*nongit_ok = 1;
				return NULL;
			}
			die("Not a git repository (or any of the parent directories): %s", DEFAULT_GIT_DIR_ENVIRONMENT);
		}
		if (one_filesystem) {
			if (stat("..", &buf)) {
				cwd[offset] = '\0';
				die_errno("failed to stat '%s/..'", cwd);
			}
			if (buf.st_dev != current_device) {
				if (nongit_ok) {
					if (chdir(cwd))
						die_errno("Cannot come back to cwd");
					*nongit_ok = 1;
					return NULL;
				}
				cwd[offset] = '\0';
				die("Not a git repository (or any parent up to mount parent %s)\n"
				"Stopping at filesystem boundary (GIT_DISCOVERY_ACROSS_FILESYSTEM not set).", cwd);
			}
		}
		if (chdir("..")) {
			cwd[offset] = '\0';
			die_errno("Cannot change to '%s/..'", cwd);
		}
	}

	inside_git_dir = 0;
	if (!work_tree_env)
		inside_work_tree = 1;
	root_len = offset_1st_component(cwd);
	git_work_tree_cfg = xstrndup(cwd, offset > root_len ? offset : root_len);
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
	int i;
	char *endptr;

	if (value == NULL)
		return PERM_GROUP;

	if (!strcmp(value, "umask"))
		return PERM_UMASK;
	if (!strcmp(value, "group"))
		return PERM_GROUP;
	if (!strcmp(value, "all") ||
	    !strcmp(value, "world") ||
	    !strcmp(value, "everybody"))
		return PERM_EVERYBODY;

	/* Parse octal numbers */
	i = strtol(value, &endptr, 8);

	/* If not an octal number, maybe true/false? */
	if (*endptr != 0)
		return git_config_bool(var, value) ? PERM_GROUP : PERM_UMASK;

	/*
	 * Treat values 0, 1 and 2 as compatibility cases, otherwise it is
	 * a chmod value to restrict to.
	 */
	switch (i) {
	case PERM_UMASK:               /* 0 */
		return PERM_UMASK;
	case OLD_PERM_GROUP:           /* 1 */
		return PERM_GROUP;
	case OLD_PERM_EVERYBODY:       /* 2 */
		return PERM_EVERYBODY;
	}

	/* A filemode value was given: 0xxx */

	if ((i & 0600) != 0600)
		die("Problem with core.sharedRepository filemode value "
		    "(0%.3o).\nThe owner of files must always have "
		    "read and write permissions.", i);

	/*
	 * Mask filemode value. Others can not get write permission.
	 * x flags for directories are handled separately.
	 */
	return -(i & 0666);
}

int check_repository_format_version(const char *var, const char *value, void *cb)
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

/*
 * Returns the "prefix", a path to the current working directory
 * relative to the work tree root, or NULL, if the current working
 * directory is not a strict subdirectory of the work tree root. The
 * prefix always ends with a '/' character.
 */
const char *setup_git_directory(void)
{
	const char *retval = setup_git_directory_gently(NULL);

	/* If the work tree is not the default one, recompute prefix */
	if (inside_work_tree < 0) {
		static char buffer[PATH_MAX + 1];
		char *rel;
		if (retval && chdir(retval))
			die_errno ("Could not jump back into original cwd");
		rel = get_relative_cwd(buffer, PATH_MAX, get_git_work_tree());
		if (rel && *rel && chdir(get_git_work_tree()))
			die_errno ("Could not jump to working directory");
		return rel && *rel ? strcat(rel, "/") : NULL;
	}

	return retval;
}
