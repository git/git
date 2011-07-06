#include "cache.h"
#include "dir.h"

static int inside_git_dir = -1;
static int inside_work_tree = -1;

static int sanitary_path_copy(char *dst, const char *src)
{
	char *dst0;

	if (has_dos_drive_prefix(src)) {
		*dst++ = *src++;
		*dst++ = *src++;
	}
	dst0 = dst;

	if (is_dir_sep(*src)) {
		*dst++ = '/';
		while (is_dir_sep(*src))
			src++;
	}

	for (;;) {
		char c = *src;

		/*
		 * A path component that begins with . could be
		 * special:
		 * (1) "." and ends   -- ignore and terminate.
		 * (2) "./"           -- ignore them, eat slash and continue.
		 * (3) ".." and ends  -- strip one and terminate.
		 * (4) "../"          -- strip one, eat slash and continue.
		 */
		if (c == '.') {
			if (!src[1]) {
				/* (1) */
				src++;
			} else if (is_dir_sep(src[1])) {
				/* (2) */
				src += 2;
				while (is_dir_sep(*src))
					src++;
				continue;
			} else if (src[1] == '.') {
				if (!src[2]) {
					/* (3) */
					src += 2;
					goto up_one;
				} else if (is_dir_sep(src[2])) {
					/* (4) */
					src += 3;
					while (is_dir_sep(*src))
						src++;
					goto up_one;
				}
			}
		}

		/* copy up to the next '/', and eat all '/' */
		while ((c = *src++) != '\0' && !is_dir_sep(c))
			*dst++ = c;
		if (is_dir_sep(c)) {
			*dst++ = '/';
			while (is_dir_sep(c))
				c = *src++;
			src--;
		} else if (!c)
			break;
		continue;

	up_one:
		/*
		 * dst0..dst is prefix portion, and dst[-1] is '/';
		 * go up one level.
		 */
		dst -= 2; /* go past trailing '/' if any */
		if (dst < dst0)
			return -1;
		while (1) {
			if (dst <= dst0)
				break;
			c = *dst--;
			if (c == '/') {	/* MinGW: cannot be '\\' anymore */
				dst += 2;
				break;
			}
		}
	}
	*dst = '\0';
	return 0;
}

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
	if (sanitary_path_copy(sanitized, sanitized))
		goto error_out;
	if (is_absolute_path(orig)) {
		const char *work_tree = get_git_work_tree();
		size_t len = strlen(work_tree);
		size_t total = strlen(sanitized) + 1;
		if (strncmp(sanitized, work_tree, len) ||
		    (sanitized[len] != '\0' && sanitized[len] != '/')) {
		error_out:
			error("'%s' is outside repository", orig);
			free(sanitized);
			return NULL;
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
#ifndef __MINGW32__
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
		if (p)
			*(dst++) = p;
		else
			exit(128); /* error message already given */
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
	struct stat st;
	int fd;
	size_t len;

	if (stat(path, &st))
		return NULL;
	if (!S_ISREG(st.st_mode))
		return NULL;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		die("Error opening %s: %s", path, strerror(errno));
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
	if (!is_git_directory(buf + 8))
		die("Not a git repository: %s", buf + 8);
	path = make_absolute_path(buf + 8);
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
	int len, offset, ceil_offset;

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
			setenv(GIT_DIR_ENVIRONMENT, ".", 1);
			check_repository_format_gently(nongit_ok);
			return NULL;
		}
		while (--offset > ceil_offset && cwd[offset] != '/');
		if (offset <= ceil_offset) {
			if (nongit_ok) {
				if (chdir(cwd))
					die("Cannot come back to cwd");
				*nongit_ok = 1;
				return NULL;
			}
			die("Not a git repository");
		}
		chdir("..");
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
	 * a chmod value.
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
	return i & 0666;
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
