#include "cache.h"
#include "dir.h"
#include "string-list.h"

static int inside_git_dir = -1;
static int inside_work_tree = -1;
static int work_tree_config_is_bogus;
static struct string_list unknown_extensions = STRING_LIST_INIT_DUP;

/*
 * The input parameter must contain an absolute path, and it must already be
 * normalized.
 *
 * Find the part of an absolute path that lies inside the work tree by
 * dereferencing symlinks outside the work tree, for example:
 * /dir1/repo/dir2/file   (work tree is /dir1/repo)      -> dir2/file
 * /dir/file              (work tree is /)               -> dir/file
 * /dir/symlink1/symlink2 (symlink1 points to work tree) -> symlink2
 * /dir/repolink/file     (repolink points to /dir/repo) -> file
 * /dir/repo              (exactly equal to work tree)   -> (empty string)
 */
static int abspath_part_inside_repo(char *path)
{
	size_t len;
	size_t wtlen;
	char *path0;
	int off;
	const char *work_tree = get_git_work_tree();

	if (!work_tree)
		return -1;
	wtlen = strlen(work_tree);
	len = strlen(path);
	off = offset_1st_component(path);

	/* check if work tree is already the prefix */
	if (wtlen <= len && !strncmp(path, work_tree, wtlen)) {
		if (path[wtlen] == '/') {
			memmove(path, path + wtlen + 1, len - wtlen);
			return 0;
		} else if (path[wtlen - 1] == '/' || path[wtlen] == '\0') {
			/* work tree is the root, or the whole path */
			memmove(path, path + wtlen, len - wtlen + 1);
			return 0;
		}
		/* work tree might match beginning of a symlink to work tree */
		off = wtlen;
	}
	path0 = path;
	path += off;

	/* check each '/'-terminated level */
	while (*path) {
		path++;
		if (*path == '/') {
			*path = '\0';
			if (strcmp(real_path(path0), work_tree) == 0) {
				memmove(path0, path + 1, len - (path - path0));
				return 0;
			}
			*path = '/';
		}
	}

	/* check whole path */
	if (strcmp(real_path(path0), work_tree) == 0) {
		*path0 = '\0';
		return 0;
	}

	return -1;
}

/*
 * Normalize "path", prepending the "prefix" for relative paths. If
 * remaining_prefix is not NULL, return the actual prefix still
 * remains in the path. For example, prefix = sub1/sub2/ and path is
 *
 *  foo          -> sub1/sub2/foo  (full prefix)
 *  ../foo       -> sub1/foo       (remaining prefix is sub1/)
 *  ../../bar    -> bar            (no remaining prefix)
 *  ../../sub1/sub2/foo -> sub1/sub2/foo (but no remaining prefix)
 *  `pwd`/../bar -> sub1/bar       (no remaining prefix)
 */
char *prefix_path_gently(const char *prefix, int len,
			 int *remaining_prefix, const char *path)
{
	const char *orig = path;
	char *sanitized;
	if (is_absolute_path(orig)) {
		sanitized = xmalloc(strlen(path) + 1);
		if (remaining_prefix)
			*remaining_prefix = 0;
		if (normalize_path_copy_len(sanitized, path, remaining_prefix)) {
			free(sanitized);
			return NULL;
		}
		if (abspath_part_inside_repo(sanitized)) {
			free(sanitized);
			return NULL;
		}
	} else {
		sanitized = xmalloc(len + strlen(path) + 1);
		if (len)
			memcpy(sanitized, prefix, len);
		strcpy(sanitized + len, path);
		if (remaining_prefix)
			*remaining_prefix = len;
		if (normalize_path_copy_len(sanitized, sanitized, remaining_prefix)) {
			free(sanitized);
			return NULL;
		}
	}
	return sanitized;
}

char *prefix_path(const char *prefix, int len, const char *path)
{
	char *r = prefix_path_gently(prefix, len, NULL, path);
	if (!r)
		die("'%s' is outside repository", path);
	return r;
}

int path_inside_repo(const char *prefix, const char *path)
{
	int len = prefix ? strlen(prefix) : 0;
	char *r = prefix_path_gently(prefix, len, NULL, path);
	if (r) {
		free(r);
		return 1;
	}
	return 0;
}

int check_filename(const char *prefix, const char *arg)
{
	const char *name;
	struct stat st;

	if (starts_with(arg, ":/")) {
		if (arg[2] == '\0') /* ":/" is root dir, always exists */
			return 1;
		name = arg + 2;
	} else if (!no_wildcard(arg))
		return 1;
	else if (prefix)
		name = prefix_filename(prefix, strlen(prefix), arg);
	else
		name = arg;
	if (!lstat(name, &st))
		return 1; /* file exists */
	if (errno == ENOENT || errno == ENOTDIR)
		return 0; /* file does not exist */
	die_errno("failed to stat '%s'", arg);
}

static void NORETURN die_verify_filename(const char *prefix,
					 const char *arg,
					 int diagnose_misspelt_rev)
{
	if (!diagnose_misspelt_rev)
		die("%s: no such path in the working tree.\n"
		    "Use 'git <command> -- <path>...' to specify paths that do not exist locally.",
		    arg);
	/*
	 * Saying "'(icase)foo' does not exist in the index" when the
	 * user gave us ":(icase)foo" is just stupid.  A magic pathspec
	 * begins with a colon and is followed by a non-alnum; do not
	 * let maybe_die_on_misspelt_object_name() even trigger.
	 */
	if (!(arg[0] == ':' && !isalnum(arg[1])))
		maybe_die_on_misspelt_object_name(arg, prefix);

	/* ... or fall back the most general message. */
	die("ambiguous argument '%s': unknown revision or path not in the working tree.\n"
	    "Use '--' to separate paths from revisions, like this:\n"
	    "'git <command> [<revision>...] -- [<file>...]'", arg);

}

/*
 * Verify a filename that we got as an argument for a pathspec
 * entry. Note that a filename that begins with "-" never verifies
 * as true, because even if such a filename were to exist, we want
 * it to be preceded by the "--" marker (or we want the user to
 * use a format like "./-filename")
 *
 * The "diagnose_misspelt_rev" is used to provide a user-friendly
 * diagnosis when dying upon finding that "name" is not a pathname.
 * If set to 1, the diagnosis will try to diagnose "name" as an
 * invalid object name (e.g. HEAD:foo). If set to 0, the diagnosis
 * will only complain about an inexisting file.
 *
 * This function is typically called to check that a "file or rev"
 * argument is unambiguous. In this case, the caller will want
 * diagnose_misspelt_rev == 1 when verifying the first non-rev
 * argument (which could have been a revision), and
 * diagnose_misspelt_rev == 0 for the next ones (because we already
 * saw a filename, there's not ambiguity anymore).
 */
void verify_filename(const char *prefix,
		     const char *arg,
		     int diagnose_misspelt_rev)
{
	if (*arg == '-')
		die("bad flag '%s' used after filename", arg);
	if (check_filename(prefix, arg))
		return;
	die_verify_filename(prefix, arg, diagnose_misspelt_rev);
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
	    "Use '--' to separate paths from revisions, like this:\n"
	    "'git <command> [<revision>...] -- [<file>...]'", arg);
}

int get_common_dir(struct strbuf *sb, const char *gitdir)
{
	const char *git_env_common_dir = getenv(GIT_COMMON_DIR_ENVIRONMENT);
	if (git_env_common_dir) {
		strbuf_addstr(sb, git_env_common_dir);
		return 1;
	} else {
		return get_common_dir_noenv(sb, gitdir);
	}
}

int get_common_dir_noenv(struct strbuf *sb, const char *gitdir)
{
	struct strbuf data = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	int ret = 0;

	strbuf_addf(&path, "%s/commondir", gitdir);
	if (file_exists(path.buf)) {
		if (strbuf_read_file(&data, path.buf, 0) <= 0)
			die_errno(_("failed to read %s"), path.buf);
		while (data.len && (data.buf[data.len - 1] == '\n' ||
				    data.buf[data.len - 1] == '\r'))
			data.len--;
		data.buf[data.len] = '\0';
		strbuf_reset(&path);
		if (!is_absolute_path(data.buf))
			strbuf_addf(&path, "%s/", gitdir);
		strbuf_addbuf(&path, &data);
		strbuf_addstr(sb, real_path(path.buf));
		ret = 1;
	} else
		strbuf_addstr(sb, gitdir);
	strbuf_release(&data);
	strbuf_release(&path);
	return ret;
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
int is_git_directory(const char *suspect)
{
	struct strbuf path = STRBUF_INIT;
	int ret = 0;
	size_t len;

	/* Check worktree-related signatures */
	strbuf_addf(&path, "%s/HEAD", suspect);
	if (validate_headref(path.buf))
		goto done;

	strbuf_reset(&path);
	get_common_dir(&path, suspect);
	len = path.len;

	/* Check non-worktree-related signatures */
	if (getenv(DB_ENVIRONMENT)) {
		if (access(getenv(DB_ENVIRONMENT), X_OK))
			goto done;
	}
	else {
		strbuf_setlen(&path, len);
		strbuf_addstr(&path, "/objects");
		if (access(path.buf, X_OK))
			goto done;
	}

	strbuf_setlen(&path, len);
	strbuf_addstr(&path, "/refs");
	if (access(path.buf, X_OK))
		goto done;

	ret = 1;
done:
	strbuf_release(&path);
	return ret;
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

	if (work_tree_config_is_bogus)
		die("unable to set up work tree using invalid config");

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

	set_git_dir(remove_leading_path(git_dir, work_tree));
	initialized = 1;
}

static int check_repo_format(const char *var, const char *value, void *cb)
{
	const char *ext;

	if (strcmp(var, "core.repositoryformatversion") == 0)
		repository_format_version = git_config_int(var, value);
	else if (strcmp(var, "core.sharedrepository") == 0)
		shared_repository = git_config_perm(var, value);
	else if (skip_prefix(var, "extensions.", &ext)) {
		/*
		 * record any known extensions here; otherwise,
		 * we fall through to recording it as unknown, and
		 * check_repository_format will complain
		 */
		if (!strcmp(ext, "noop"))
			;
		else if (!strcmp(ext, "preciousobjects"))
			repository_format_precious_objects = git_config_bool(var, value);
		else
			string_list_append(&unknown_extensions, ext);
	}
	return 0;
}

static int check_repository_format_gently(const char *gitdir, int *nongit_ok)
{
	struct strbuf sb = STRBUF_INIT;
	const char *repo_config;
	config_fn_t fn;
	int ret = 0;

	string_list_clear(&unknown_extensions, 0);

	if (get_common_dir(&sb, gitdir))
		fn = check_repo_format;
	else
		fn = check_repository_format_version;
	strbuf_addstr(&sb, "/config");
	repo_config = sb.buf;

	/*
	 * git_config() can't be used here because it calls git_pathdup()
	 * to get $GIT_CONFIG/config. That call will make setup_git_env()
	 * set git_dir to ".git".
	 *
	 * We are in gitdir setup, no git dir has been found useable yet.
	 * Use a gentler version of git_config() to check if this repo
	 * is a good one.
	 */
	git_config_early(fn, NULL, repo_config);
	if (GIT_REPO_VERSION_READ < repository_format_version) {
		if (!nongit_ok)
			die ("Expected git repo version <= %d, found %d",
			     GIT_REPO_VERSION_READ, repository_format_version);
		warning("Expected git repo version <= %d, found %d",
			GIT_REPO_VERSION_READ, repository_format_version);
		warning("Please upgrade Git");
		*nongit_ok = -1;
		ret = -1;
	}

	if (repository_format_version >= 1 && unknown_extensions.nr) {
		int i;

		if (!nongit_ok)
			die("unknown repository extension: %s",
			    unknown_extensions.items[0].string);

		for (i = 0; i < unknown_extensions.nr; i++)
			warning("unknown repository extension: %s",
				unknown_extensions.items[i].string);
		*nongit_ok = -1;
		ret = -1;
	}

	strbuf_release(&sb);
	return ret;
}

static void update_linked_gitdir(const char *gitfile, const char *gitdir)
{
	struct strbuf path = STRBUF_INIT;
	struct stat st;

	strbuf_addf(&path, "%s/gitdir", gitdir);
	if (stat(path.buf, &st) || st.st_mtime + 24 * 3600 < time(NULL))
		write_file(path.buf, "%s", gitfile);
	strbuf_release(&path);
}

/*
 * Try to read the location of the git directory from the .git file,
 * return path to git directory if found.
 *
 * On failure, if return_error_code is not NULL, return_error_code
 * will be set to an error code and NULL will be returned. If
 * return_error_code is NULL the function will die instead (for most
 * cases).
 */
const char *read_gitfile_gently(const char *path, int *return_error_code)
{
	const int max_file_size = 1 << 20;  /* 1MB */
	int error_code = 0;
	char *buf = NULL;
	char *dir = NULL;
	const char *slash;
	struct stat st;
	int fd;
	ssize_t len;

	if (stat(path, &st)) {
		error_code = READ_GITFILE_ERR_STAT_FAILED;
		goto cleanup_return;
	}
	if (!S_ISREG(st.st_mode)) {
		error_code = READ_GITFILE_ERR_NOT_A_FILE;
		goto cleanup_return;
	}
	if (st.st_size > max_file_size) {
		error_code = READ_GITFILE_ERR_TOO_LARGE;
		goto cleanup_return;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		error_code = READ_GITFILE_ERR_OPEN_FAILED;
		goto cleanup_return;
	}
	buf = xmalloc(st.st_size + 1);
	len = read_in_full(fd, buf, st.st_size);
	close(fd);
	if (len != st.st_size) {
		error_code = READ_GITFILE_ERR_READ_FAILED;
		goto cleanup_return;
	}
	buf[len] = '\0';
	if (!starts_with(buf, "gitdir: ")) {
		error_code = READ_GITFILE_ERR_INVALID_FORMAT;
		goto cleanup_return;
	}
	while (buf[len - 1] == '\n' || buf[len - 1] == '\r')
		len--;
	if (len < 9) {
		error_code = READ_GITFILE_ERR_NO_PATH;
		goto cleanup_return;
	}
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
	if (!is_git_directory(dir)) {
		error_code = READ_GITFILE_ERR_NOT_A_REPO;
		goto cleanup_return;
	}
	update_linked_gitdir(path, dir);
	path = real_path(dir);

cleanup_return:
	if (return_error_code)
		*return_error_code = error_code;
	else if (error_code) {
		switch (error_code) {
		case READ_GITFILE_ERR_STAT_FAILED:
		case READ_GITFILE_ERR_NOT_A_FILE:
			/* non-fatal; follow return path */
			break;
		case READ_GITFILE_ERR_OPEN_FAILED:
			die_errno("Error opening '%s'", path);
		case READ_GITFILE_ERR_TOO_LARGE:
			die("Too large to be a .git file: '%s'", path);
		case READ_GITFILE_ERR_READ_FAILED:
			die("Error reading %s", path);
		case READ_GITFILE_ERR_INVALID_FORMAT:
			die("Invalid gitfile format: %s", path);
		case READ_GITFILE_ERR_NO_PATH:
			die("No path in gitfile: %s", path);
		case READ_GITFILE_ERR_NOT_A_REPO:
			die("Not a git repository: %s", dir);
		default:
			assert(0);
		}
	}

	free(buf);
	return error_code ? NULL : path;
}

static const char *setup_explicit_git_dir(const char *gitdirenv,
					  struct strbuf *cwd,
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
		if (git_work_tree_cfg) {
			/* #22.2, #30 */
			warning("core.bare and core.worktree do not make sense");
			work_tree_config_is_bogus = 1;
		}

		/* #18, #26 */
		set_git_dir(gitdirenv);
		free(gitfile);
		return NULL;
	}
	else if (git_work_tree_cfg) { /* #6, #14 */
		if (is_absolute_path(git_work_tree_cfg))
			set_git_work_tree(git_work_tree_cfg);
		else {
			char *core_worktree;
			if (chdir(gitdirenv))
				die_errno("Could not chdir to '%s'", gitdirenv);
			if (chdir(git_work_tree_cfg))
				die_errno("Could not chdir to '%s'", git_work_tree_cfg);
			core_worktree = xgetcwd();
			if (chdir(cwd->buf))
				die_errno("Could not come back to cwd");
			set_git_work_tree(core_worktree);
			free(core_worktree);
		}
	}
	else if (!git_env_bool(GIT_IMPLICIT_WORK_TREE_ENVIRONMENT, 1)) {
		/* #16d */
		set_git_dir(gitdirenv);
		free(gitfile);
		return NULL;
	}
	else /* #2, #10 */
		set_git_work_tree(".");

	/* set_git_work_tree() must have been called by now */
	worktree = get_git_work_tree();

	/* both get_git_work_tree() and cwd are already normalized */
	if (!strcmp(cwd->buf, worktree)) { /* cwd == worktree */
		set_git_dir(gitdirenv);
		free(gitfile);
		return NULL;
	}

	offset = dir_inside_of(cwd->buf, worktree);
	if (offset >= 0) {	/* cwd inside worktree? */
		set_git_dir(real_path(gitdirenv));
		if (chdir(worktree))
			die_errno("Could not chdir to '%s'", worktree);
		strbuf_addch(cwd, '/');
		free(gitfile);
		return cwd->buf + offset;
	}

	/* cwd outside worktree */
	set_git_dir(gitdirenv);
	free(gitfile);
	return NULL;
}

static const char *setup_discovered_git_dir(const char *gitdir,
					    struct strbuf *cwd, int offset,
					    int *nongit_ok)
{
	if (check_repository_format_gently(gitdir, nongit_ok))
		return NULL;

	/* --work-tree is set without --git-dir; use discovered one */
	if (getenv(GIT_WORK_TREE_ENVIRONMENT) || git_work_tree_cfg) {
		if (offset != cwd->len && !is_absolute_path(gitdir))
			gitdir = xstrdup(real_path(gitdir));
		if (chdir(cwd->buf))
			die_errno("Could not come back to cwd");
		return setup_explicit_git_dir(gitdir, cwd, nongit_ok);
	}

	/* #16.2, #17.2, #20.2, #21.2, #24, #25, #28, #29 (see t1510) */
	if (is_bare_repository_cfg > 0) {
		set_git_dir(offset == cwd->len ? gitdir : real_path(gitdir));
		if (chdir(cwd->buf))
			die_errno("Could not come back to cwd");
		return NULL;
	}

	/* #0, #1, #5, #8, #9, #12, #13 */
	set_git_work_tree(".");
	if (strcmp(gitdir, DEFAULT_GIT_DIR_ENVIRONMENT))
		set_git_dir(gitdir);
	inside_git_dir = 0;
	inside_work_tree = 1;
	if (offset == cwd->len)
		return NULL;

	/* Make "offset" point to past the '/', and add a '/' at the end */
	offset++;
	strbuf_addch(cwd, '/');
	return cwd->buf + offset;
}

/* #16.1, #17.1, #20.1, #21.1, #22.1 (see t1510) */
static const char *setup_bare_git_dir(struct strbuf *cwd, int offset,
				      int *nongit_ok)
{
	int root_len;

	if (check_repository_format_gently(".", nongit_ok))
		return NULL;

	setenv(GIT_IMPLICIT_WORK_TREE_ENVIRONMENT, "0", 1);

	/* --work-tree is set without --git-dir; use discovered one */
	if (getenv(GIT_WORK_TREE_ENVIRONMENT) || git_work_tree_cfg) {
		const char *gitdir;

		gitdir = offset == cwd->len ? "." : xmemdupz(cwd->buf, offset);
		if (chdir(cwd->buf))
			die_errno("Could not come back to cwd");
		return setup_explicit_git_dir(gitdir, cwd, nongit_ok);
	}

	inside_git_dir = 1;
	inside_work_tree = 0;
	if (offset != cwd->len) {
		if (chdir(cwd->buf))
			die_errno("Cannot come back to cwd");
		root_len = offset_1st_component(cwd->buf);
		strbuf_setlen(cwd, offset > root_len ? offset : root_len);
		set_git_dir(cwd->buf);
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

static dev_t get_device_or_die(const char *path, const char *prefix, int prefix_len)
{
	struct stat buf;
	if (stat(path, &buf)) {
		die_errno("failed to stat '%*s%s%s'",
				prefix_len,
				prefix ? prefix : "",
				prefix ? "/" : "", path);
	}
	return buf.st_dev;
}

/*
 * A "string_list_each_func_t" function that canonicalizes an entry
 * from GIT_CEILING_DIRECTORIES using real_path_if_valid(), or
 * discards it if unusable.  The presence of an empty entry in
 * GIT_CEILING_DIRECTORIES turns off canonicalization for all
 * subsequent entries.
 */
static int canonicalize_ceiling_entry(struct string_list_item *item,
				      void *cb_data)
{
	int *empty_entry_found = cb_data;
	char *ceil = item->string;

	if (!*ceil) {
		*empty_entry_found = 1;
		return 0;
	} else if (!is_absolute_path(ceil)) {
		return 0;
	} else if (*empty_entry_found) {
		/* Keep entry but do not canonicalize it */
		return 1;
	} else {
		const char *real_path = real_path_if_valid(ceil);
		if (!real_path)
			return 0;
		free(item->string);
		item->string = xstrdup(real_path);
		return 1;
	}
}

/*
 * We cannot decide in this function whether we are in the work tree or
 * not, since the config can only be read _after_ this function was called.
 */
static const char *setup_git_directory_gently_1(int *nongit_ok)
{
	const char *env_ceiling_dirs = getenv(CEILING_DIRECTORIES_ENVIRONMENT);
	struct string_list ceiling_dirs = STRING_LIST_INIT_DUP;
	static struct strbuf cwd = STRBUF_INIT;
	const char *gitdirenv, *ret;
	char *gitfile;
	int offset, offset_parent, ceil_offset = -1;
	dev_t current_device = 0;
	int one_filesystem = 1;

	/*
	 * We may have read an incomplete configuration before
	 * setting-up the git directory. If so, clear the cache so
	 * that the next queries to the configuration reload complete
	 * configuration (including the per-repo config file that we
	 * ignored previously).
	 */
	git_config_clear();

	/*
	 * Let's assume that we are in a git repository.
	 * If it turns out later that we are somewhere else, the value will be
	 * updated accordingly.
	 */
	if (nongit_ok)
		*nongit_ok = 0;

	if (strbuf_getcwd(&cwd))
		die_errno("Unable to read current working directory");
	offset = cwd.len;

	/*
	 * If GIT_DIR is set explicitly, we're not going
	 * to do any discovery, but we still do repository
	 * validation.
	 */
	gitdirenv = getenv(GIT_DIR_ENVIRONMENT);
	if (gitdirenv)
		return setup_explicit_git_dir(gitdirenv, &cwd, nongit_ok);

	if (env_ceiling_dirs) {
		int empty_entry_found = 0;

		string_list_split(&ceiling_dirs, env_ceiling_dirs, PATH_SEP, -1);
		filter_string_list(&ceiling_dirs, 0,
				   canonicalize_ceiling_entry, &empty_entry_found);
		ceil_offset = longest_ancestor_length(cwd.buf, &ceiling_dirs);
		string_list_clear(&ceiling_dirs, 0);
	}

	if (ceil_offset < 0 && has_dos_drive_prefix(cwd.buf))
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
		current_device = get_device_or_die(".", NULL, 0);
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
						       &cwd, offset,
						       nongit_ok);
			free(gitfile);
			return ret;
		}
		free(gitfile);

		if (is_git_directory("."))
			return setup_bare_git_dir(&cwd, offset, nongit_ok);

		offset_parent = offset;
		while (--offset_parent > ceil_offset && cwd.buf[offset_parent] != '/');
		if (offset_parent <= ceil_offset)
			return setup_nongit(cwd.buf, nongit_ok);
		if (one_filesystem) {
			dev_t parent_device = get_device_or_die("..", cwd.buf,
								offset);
			if (parent_device != current_device) {
				if (nongit_ok) {
					if (chdir(cwd.buf))
						die_errno("Cannot come back to cwd");
					*nongit_ok = 1;
					return NULL;
				}
				strbuf_setlen(&cwd, offset);
				die("Not a git repository (or any parent up to mount point %s)\n"
				"Stopping at filesystem boundary (GIT_DISCOVERY_ACROSS_FILESYSTEM not set).",
				    cwd.buf);
			}
		}
		if (chdir("..")) {
			strbuf_setlen(&cwd, offset);
			die_errno("Cannot change to '%s/..'", cwd.buf);
		}
		offset = offset_parent;
	}
}

const char *setup_git_directory_gently(int *nongit_ok)
{
	const char *prefix;

	prefix = setup_git_directory_gently_1(nongit_ok);
	if (prefix)
		setenv(GIT_PREFIX_ENVIRONMENT, prefix, 1);
	else
		setenv(GIT_PREFIX_ENVIRONMENT, "", 1);

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
	int ret = check_repo_format(var, value, cb);
	if (ret)
		return ret;
	if (strcmp(var, "core.bare") == 0) {
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

/* if any standard file descriptor is missing open it to /dev/null */
void sanitize_stdfds(void)
{
	int fd = open("/dev/null", O_RDWR, 0);
	while (fd != -1 && fd < 2)
		fd = dup(fd);
	if (fd == -1)
		die_errno("open /dev/null or dup failed");
	if (fd > 2)
		close(fd);
}

int daemonize(void)
{
#ifdef NO_POSIX_GOODIES
	errno = ENOSYS;
	return -1;
#else
	switch (fork()) {
		case 0:
			break;
		case -1:
			die_errno("fork failed");
		default:
			exit(0);
	}
	if (setsid() == -1)
		die_errno("setsid failed");
	close(0);
	close(1);
	close(2);
	sanitize_stdfds();
	return 0;
#endif
}
