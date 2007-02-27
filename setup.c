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

	if (is_inside_git_dir())
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
	if (inside_git_dir < 0) {
		char buffer[1024];

		if (is_bare_repository())
			return (inside_git_dir = 1);
		if (getcwd(buffer, sizeof(buffer))) {
			const char *git_dir = get_git_dir(), *cwd = buffer;
			while (*git_dir && *git_dir == *cwd) {
				git_dir++;
				cwd++;
			}
			inside_git_dir = !*git_dir;
		} else
			inside_git_dir = 0;
	}
	return inside_git_dir;
}

const char *setup_git_directory_gently(int *nongit_ok)
{
	static char cwd[PATH_MAX+1];
	const char *gitdirenv;
	int len, offset;
	int minoffset = 0;

	/*
	 * If GIT_DIR is set explicitly, we're not going
	 * to do any discovery, but we still do repository
	 * validation.
	 */
	gitdirenv = getenv(GIT_DIR_ENVIRONMENT);
	if (gitdirenv) {
		if (PATH_MAX - 40 < strlen(gitdirenv))
			die("'$%s' too big", GIT_DIR_ENVIRONMENT);
		if (is_git_directory(gitdirenv))
			return NULL;
		if (nongit_ok) {
			*nongit_ok = 1;
			return NULL;
		}
		die("Not a git repository: '%s'", gitdirenv);
	}

#ifdef __MINGW32__
	if (!getcwd(cwd, sizeof(cwd)) || !(cwd[0] == '/' || cwd[1] == ':'))
		die("Unable to read current working directory");
	if (cwd[1] == ':')
		minoffset = 2;
#else
	if (!getcwd(cwd, sizeof(cwd)) || cwd[0] != '/')
		die("Unable to read current working directory");
#endif

	offset = len = strlen(cwd);
	for (;;) {
		if (is_git_directory(".git"))
			break;
		chdir("..");
		do {
			if (offset <= minoffset) {
				if (is_git_directory(cwd)) {
					if (chdir(cwd))
						die("Cannot come back to cwd");
					setenv(GIT_DIR_ENVIRONMENT, cwd, 1);
					inside_git_dir = 1;
					return NULL;
				}
				if (nongit_ok) {
					if (chdir(cwd))
						die("Cannot come back to cwd");
					*nongit_ok = 1;
					return NULL;
				}
				die("Not a git repository");
			}
		} while (offset > minoffset && cwd[--offset] != '/');
	}

	if (offset == len)
		return NULL;

	/* Make "offset" point to past the '/', and add a '/' at the end */
	offset++;
	cwd[len++] = '/';
	cwd[len] = 0;
	inside_git_dir = !prefixcmp(cwd + offset, ".git/");
	return cwd + offset;
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
