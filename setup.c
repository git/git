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
 * Test if it looks like we're at the top level git directory.
 * We want to see:
 *
 *  - either a .git/objects/ directory _or_ the proper
 *    GIT_OBJECT_DIRECTORY environment variable
 *  - a refs/ directory under ".git"
 *  - either a HEAD symlink or a HEAD file that is formatted as
 *    a proper "ref:".
 */
static int is_toplevel_directory(void)
{
	if (access(".git/refs/", X_OK) ||
	    access(getenv(DB_ENVIRONMENT) ?
		   getenv(DB_ENVIRONMENT) : ".git/objects/", X_OK) ||
	    validate_symref(".git/HEAD"))
		return 0;
	return 1;
}

const char *setup_git_directory_gently(int *nongit_ok)
{
	static char cwd[PATH_MAX+1];
	int len, offset;

	/*
	 * If GIT_DIR is set explicitly, we're not going
	 * to do any discovery, but we still do repository
	 * validation.
	 */
	if (getenv(GIT_DIR_ENVIRONMENT)) {
		char path[PATH_MAX];
		int len = strlen(getenv(GIT_DIR_ENVIRONMENT));
		if (sizeof(path) - 40 < len)
			die("'$%s' too big", GIT_DIR_ENVIRONMENT);
		memcpy(path, getenv(GIT_DIR_ENVIRONMENT), len);
		
		strcpy(path + len, "/refs");
		if (access(path, X_OK))
			goto bad_dir_environ;
		strcpy(path + len, "/HEAD");
		if (validate_symref(path))
			goto bad_dir_environ;
		if (getenv(DB_ENVIRONMENT)) {
			if (access(getenv(DB_ENVIRONMENT), X_OK))
				goto bad_dir_environ;
		}
		else {
			strcpy(path + len, "/objects");
			if (access(path, X_OK))
				goto bad_dir_environ;
		}
		return NULL;
	bad_dir_environ:
		path[len] = 0;
		die("Not a git repository: '%s'", path);
	}

	if (!getcwd(cwd, sizeof(cwd)) || cwd[0] != '/')
		die("Unable to read current working directory");

	offset = len = strlen(cwd);
	for (;;) {
		if (is_toplevel_directory())
			break;
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

	if (offset == len)
		return NULL;

	/* Make "offset" point to past the '/', and add a '/' at the end */
	offset++;
	cwd[len++] = '/';
	cwd[len] = 0;
	return cwd + offset;
}

int check_repository_format_version(const char *var, const char *value)
{
       if (strcmp(var, "core.repositoryformatversion") == 0)
               repository_format_version = git_config_int(var, value);
	else if (strcmp(var, "core.sharedrepository") == 0)
		shared_repository = git_config_bool(var, value);
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
