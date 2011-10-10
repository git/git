#include "cache.h"
#include "dir.h"

static int inside_git_dir = -1;
static int inside_work_tree = -1;

char *prefix_path(const char *prefix, int len, const char *path)
{
	const char *orig = path;
	char *sanitized;
	if (is_absolute_path(orig)) {
		const char *temp = real_path(path);
		sanitized = xmalloc(len + strlen(temp) + 1);
		strcpy(sanitized, temp);
	} else {
		sanitized = xmalloc(len + strlen(path) + 1);
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

	/*
	 * Saying "'(icase)foo' does not exist in the index" when the
	 * user gave us ":(icase)foo" is just stupid.  A magic pathspec
	 * begins with a colon and is followed by a non-alnum; do not
	 * let get_sha1_with_mode_1(only_to_die=1) to even trigger.
	 */
	if (!(arg[0] == ':' && !isalnum(arg[1])))
		/* try a detailed diagnostic ... */
		get_sha1_with_mode_1(arg, sha1, &mode, 1, prefix);

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

/*
 * Magic pathspec
 *
 * NEEDSWORK: These need to be moved to dir.h or even to a new
 * pathspec.h when we restructure get_pathspec() users to use the
 * "struct pathspec" interface.
 *
 * Possible future magic semantics include stuff like:
 *
 *	{ PATHSPEC_NOGLOB, '!', "noglob" },
 *	{ PATHSPEC_ICASE, '\0', "icase" },
 *	{ PATHSPEC_RECURSIVE, '*', "recursive" },
 *	{ PATHSPEC_REGEXP, '\0', "regexp" },
 *
 */
#define PATHSPEC_FROMTOP    (1<<0)

static struct pathspec_magic {
	unsigned bit;
	char mnemonic; /* this cannot be ':'! */
	const char *name;
} pathspec_magic[] = {
	{ PATHSPEC_FROMTOP, '/', "top" },
};

/*
 * Take an element of a pathspec and check for magic signatures.
 * Append the result to the prefix.
 *
 * For now, we only parse the syntax and throw out anything other than
 * "top" magic.
 *
 * NEEDSWORK: This needs to be rewritten when we start migrating
 * get_pathspec() users to use the "struct pathspec" interface.  For
 * example, a pathspec element may be marked as case-insensitive, but
 * the prefix part must always match literally, and a single stupid
 * string cannot express such a case.
 */
static const char *prefix_pathspec(const char *prefix, int prefixlen, const char *elt)
{
	unsigned magic = 0;
	const char *copyfrom = elt;
	int i;

	if (elt[0] != ':') {
		; /* nothing to do */
	} else if (elt[1] == '(') {
		/* longhand */
		const char *nextat;
		for (copyfrom = elt + 2;
		     *copyfrom && *copyfrom != ')';
		     copyfrom = nextat) {
			size_t len = strcspn(copyfrom, ",)");
			if (copyfrom[len] == ')')
				nextat = copyfrom + len;
			else
				nextat = copyfrom + len + 1;
			if (!len)
				continue;
			for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++)
				if (strlen(pathspec_magic[i].name) == len &&
				    !strncmp(pathspec_magic[i].name, copyfrom, len)) {
					magic |= pathspec_magic[i].bit;
					break;
				}
			if (ARRAY_SIZE(pathspec_magic) <= i)
				die("Invalid pathspec magic '%.*s' in '%s'",
				    (int) len, copyfrom, elt);
		}
		if (*copyfrom == ')')
			copyfrom++;
	} else {
		/* shorthand */
		for (copyfrom = elt + 1;
		     *copyfrom && *copyfrom != ':';
		     copyfrom++) {
			char ch = *copyfrom;

			if (!is_pathspec_magic(ch))
				break;
			for (i = 0; i < ARRAY_SIZE(pathspec_magic); i++)
				if (pathspec_magic[i].mnemonic == ch) {
					magic |= pathspec_magic[i].bit;
					break;
				}
			if (ARRAY_SIZE(pathspec_magic) <= i)
				die("Unimplemented pathspec magic '%c' in '%s'",
				    ch, elt);
		}
		if (*copyfrom == ':')
			copyfrom++;
	}

	if (magic & PATHSPEC_FROMTOP)
		return xstrdup(copyfrom);
	else
		return prefix_path(prefix, prefixlen, copyfrom);
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
		*(dst++) = prefix_pathspec(prefix, prefixlen, *src);
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

void setup_work_tree(void)
{
	const char *work_tree, *git_dir;
	static int initialized = 0;

	if (initialized)
		return;
	work_tree = get_git_work_tree();
	git_dir = get_git_dir();
	if (!is_absolute_path(git_dir))
		git_dir = real_path(get_git_dir());
	if (!work_tree || chdir(work_tree))
		die("This operation must be run in a work tree");

	/*
	 * Make sure subsequent git processes find correct worktree
	 * if $GIT_WORK_TREE is set relative
	 */
	if (getenv(GIT_WORK_TREE_ENVIRONMENT))
		setenv(GIT_WORK_TREE_ENVIRONMENT, ".", 1);

	set_git_dir(relative_path(git_dir, work_tree));
	initialized = 1;
}

static int check_repository_format_gently(const char *gitdir, int *nongit_ok)
{
	char repo_config[PATH_MAX+1];

	/*
	 * git_config() can't be used here because it calls git_pathdup()
	 * to get $GIT_CONFIG/config. That call will make setup_git_env()
	 * set git_dir to ".git".
	 *
	 * We are in gitdir setup, no git dir has been found useable yet.
	 * Use a gentler version of git_config() to check if this repo
	 * is a good one.
	 */
	snprintf(repo_config, PATH_MAX, "%s/config", gitdir);
	git_config_early(check_repository_format_version, NULL, repo_config);
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
const char *read_gitfile(const char *path)
{
	char *buf;
	char *dir;
	const char *slash;
	struct stat st;
	int fd;
	ssize_t len;

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
	path = real_path(dir);

	free(buf);
	return path;
}

static const char *setup_explicit_git_dir(const char *gitdirenv,
					  char *cwd, int len,
					  int *nongit_ok)
{
	const char *work_tree_env = getenv(GIT_WORK_TREE_ENVIRONMENT);
	const char *worktree;
	char *gitfile;
	int offset;

	if (PATH_MAX - 40 < strlen(gitdirenv))
		die("'$%s' too big", GIT_DIR_ENVIRONMENT);

	gitfile = (char*)read_gitfile(gitdirenv);
	if (gitfile) {
		gitfile = xstrdup(gitfile);
		gitdirenv = gitfile;
	}

	if (!is_git_directory(gitdirenv)) {
		if (nongit_ok) {
			*nongit_ok = 1;
			free(gitfile);
			return NULL;
		}
		die("Not a git repository: '%s'", gitdirenv);
	}

	if (check_repository_format_gently(gitdirenv, nongit_ok)) {
		free(gitfile);
		return NULL;
	}

	/* #3, #7, #11, #15, #19, #23, #27, #31 (see t1510) */
	if (work_tree_env)
		set_git_work_tree(work_tree_env);
	else if (is_bare_repository_cfg > 0) {
		if (git_work_tree_cfg) /* #22.2, #30 */
			die("core.bare and core.worktree do not make sense");

		/* #18, #26 */
		set_git_dir(gitdirenv);
		free(gitfile);
		return NULL;
	}
	else if (git_work_tree_cfg) { /* #6, #14 */
		if (is_absolute_path(git_work_tree_cfg))
			set_git_work_tree(git_work_tree_cfg);
		else {
			char core_worktree[PATH_MAX];
			if (chdir(gitdirenv))
				die_errno("Could not chdir to '%s'", gitdirenv);
			if (chdir(git_work_tree_cfg))
				die_errno("Could not chdir to '%s'", git_work_tree_cfg);
			if (!getcwd(core_worktree, PATH_MAX))
				die_errno("Could not get directory '%s'", git_work_tree_cfg);
			if (chdir(cwd))
				die_errno("Could not come back to cwd");
			set_git_work_tree(core_worktree);
		}
	}
	else /* #2, #10 */
		set_git_work_tree(".");

	/* set_git_work_tree() must have been called by now */
	worktree = get_git_work_tree();

	/* both get_git_work_tree() and cwd are already normalized */
	if (!strcmp(cwd, worktree)) { /* cwd == worktree */
		set_git_dir(gitdirenv);
		free(gitfile);
		return NULL;
	}

	offset = dir_inside_of(cwd, worktree);
	if (offset >= 0) {	/* cwd inside worktree? */
		set_git_dir(real_path(gitdirenv));
		if (chdir(worktree))
			die_errno("Could not chdir to '%s'", worktree);
		cwd[len++] = '/';
		cwd[len] = '\0';
		free(gitfile);
		return cwd + offset;
	}

	/* cwd outside worktree */
	set_git_dir(gitdirenv);
	free(gitfile);
	return NULL;
}

static const char *setup_discovered_git_dir(const char *gitdir,
					    char *cwd, int offset, int len,
					    int *nongit_ok)
{
	if (check_repository_format_gently(gitdir, nongit_ok))
		return NULL;

	/* --work-tree is set without --git-dir; use discovered one */
	if (getenv(GIT_WORK_TREE_ENVIRONMENT) || git_work_tree_cfg) {
		if (offset != len && !is_absolute_path(gitdir))
			gitdir = xstrdup(real_path(gitdir));
		if (chdir(cwd))
			die_errno("Could not come back to cwd");
		return setup_explicit_git_dir(gitdir, cwd, len, nongit_ok);
	}

	/* #16.2, #17.2, #20.2, #21.2, #24, #25, #28, #29 (see t1510) */
	if (is_bare_repository_cfg > 0) {
		set_git_dir(offset == len ? gitdir : real_path(gitdir));
		if (chdir(cwd))
			die_errno("Could not come back to cwd");
		return NULL;
	}

	/* #0, #1, #5, #8, #9, #12, #13 */
	set_git_work_tree(".");
	if (strcmp(gitdir, DEFAULT_GIT_DIR_ENVIRONMENT))
		set_git_dir(gitdir);
	inside_git_dir = 0;
	inside_work_tree = 1;
	if (offset == len)
		return NULL;

	/* Make "offset" point to past the '/', and add a '/' at the end */
	offset++;
	cwd[len++] = '/';
	cwd[len] = 0;
	return cwd + offset;
}

/* #16.1, #17.1, #20.1, #21.1, #22.1 (see t1510) */
static const char *setup_bare_git_dir(char *cwd, int offset, int len, int *nongit_ok)
{
	int root_len;

	if (check_repository_format_gently(".", nongit_ok))
		return NULL;

	/* --work-tree is set without --git-dir; use discovered one */
	if (getenv(GIT_WORK_TREE_ENVIRONMENT) || git_work_tree_cfg) {
		const char *gitdir;

		gitdir = offset == len ? "." : xmemdupz(cwd, offset);
		if (chdir(cwd))
			die_errno("Could not come back to cwd");
		return setup_explicit_git_dir(gitdir, cwd, len, nongit_ok);
	}

	inside_git_dir = 1;
	inside_work_tree = 0;
	if (offset != len) {
		if (chdir(cwd))
			die_errno("Cannot come back to cwd");
		root_len = offset_1st_component(cwd);
		cwd[offset > root_len ? offset : root_len] = '\0';
		set_git_dir(cwd);
	}
	else
		set_git_dir(".");
	return NULL;
}

static const char *setup_nongit(const char *cwd, int *nongit_ok)
{
	if (!nongit_ok)
		die("Not a git repository (or any of the parent directories): %s", DEFAULT_GIT_DIR_ENVIRONMENT);
	if (chdir(cwd))
		die_errno("Cannot come back to cwd");
	*nongit_ok = 1;
	return NULL;
}

static dev_t get_device_or_die(const char *path, const char *prefix)
{
	struct stat buf;
	if (stat(path, &buf))
		die_errno("failed to stat '%s%s%s'",
				prefix ? prefix : "",
				prefix ? "/" : "", path);
	return buf.st_dev;
}

/*
 * We cannot decide in this function whether we are in the work tree or
 * not, since the config can only be read _after_ this function was called.
 */
static const char *setup_git_directory_gently_1(int *nongit_ok)
{
	const char *env_ceiling_dirs = getenv(CEILING_DIRECTORIES_ENVIRONMENT);
	static char cwd[PATH_MAX+1];
	const char *gitdirenv, *ret;
	char *gitfile;
	int len, offset, ceil_offset;
	dev_t current_device = 0;
	int one_filesystem = 1;

	/*
	 * Let's assume that we are in a git repository.
	 * If it turns out later that we are somewhere else, the value will be
	 * updated accordingly.
	 */
	if (nongit_ok)
		*nongit_ok = 0;

	if (!getcwd(cwd, sizeof(cwd)-1))
		die_errno("Unable to read current working directory");
	offset = len = strlen(cwd);

	/*
	 * If GIT_DIR is set explicitly, we're not going
	 * to do any discovery, but we still do repository
	 * validation.
	 */
	gitdirenv = getenv(GIT_DIR_ENVIRONMENT);
	if (gitdirenv)
		return setup_explicit_git_dir(gitdirenv, cwd, len, nongit_ok);

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
	one_filesystem = !git_env_bool("GIT_DISCOVERY_ACROSS_FILESYSTEM", 0);
	if (one_filesystem)
		current_device = get_device_or_die(".", NULL);
	for (;;) {
		gitfile = (char*)read_gitfile(DEFAULT_GIT_DIR_ENVIRONMENT);
		if (gitfile)
			gitdirenv = gitfile = xstrdup(gitfile);
		else {
			if (is_git_directory(DEFAULT_GIT_DIR_ENVIRONMENT))
				gitdirenv = DEFAULT_GIT_DIR_ENVIRONMENT;
		}

		if (gitdirenv) {
			ret = setup_discovered_git_dir(gitdirenv,
						       cwd, offset, len,
						       nongit_ok);
			free(gitfile);
			return ret;
		}
		free(gitfile);

		if (is_git_directory("."))
			return setup_bare_git_dir(cwd, offset, len, nongit_ok);

		while (--offset > ceil_offset && cwd[offset] != '/');
		if (offset <= ceil_offset)
			return setup_nongit(cwd, nongit_ok);
		if (one_filesystem) {
			dev_t parent_device = get_device_or_die("..", cwd);
			if (parent_device != current_device) {
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
}

const char *setup_git_directory_gently(int *nongit_ok)
{
	const char *prefix;

	prefix = setup_git_directory_gently_1(nongit_ok);
	if (prefix)
		setenv("GIT_PREFIX", prefix, 1);
	else
		setenv("GIT_PREFIX", "", 1);

	if (startup_info) {
		startup_info->have_repository = !nongit_ok || !*nongit_ok;
		startup_info->prefix = prefix;
	}
	return prefix;
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
	return check_repository_format_gently(get_git_dir(), NULL);
}

/*
 * Returns the "prefix", a path to the current working directory
 * relative to the work tree root, or NULL, if the current working
 * directory is not a strict subdirectory of the work tree root. The
 * prefix always ends with a '/' character.
 */
const char *setup_git_directory(void)
{
	return setup_git_directory_gently(NULL);
}

const char *resolve_gitdir(const char *suspect)
{
	if (is_git_directory(suspect))
		return suspect;
	return read_gitfile(suspect);
}
