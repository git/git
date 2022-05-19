#include "cache.h"
#include "repository.h"
#include "config.h"
#include "dir.h"
#include "string-list.h"
#include "chdir-notify.h"
#include "promisor-remote.h"
#include "quote.h"

static int inside_but_dir = -1;
static int inside_work_tree = -1;
static int work_tree_config_is_bogus;

static struct startup_info the_startup_info;
struct startup_info *startup_info = &the_startup_info;
const char *tmp_original_cwd;

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
	const char *work_tree = get_but_work_tree();
	struct strbuf realpath = STRBUF_INIT;

	if (!work_tree)
		return -1;
	wtlen = strlen(work_tree);
	len = strlen(path);
	off = offset_1st_component(path);

	/* check if work tree is already the prefix */
	if (wtlen <= len && !fspathncmp(path, work_tree, wtlen)) {
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
			strbuf_realpath(&realpath, path0, 1);
			if (fspathcmp(realpath.buf, work_tree) == 0) {
				memmove(path0, path + 1, len - (path - path0));
				strbuf_release(&realpath);
				return 0;
			}
			*path = '/';
		}
	}

	/* check whole path */
	strbuf_realpath(&realpath, path0, 1);
	if (fspathcmp(realpath.buf, work_tree) == 0) {
		*path0 = '\0';
		strbuf_release(&realpath);
		return 0;
	}

	strbuf_release(&realpath);
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
		sanitized = xmallocz(strlen(path));
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
		sanitized = xstrfmt("%.*s%s", len, len ? prefix : "", path);
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
	if (!r) {
		const char *hint_path = get_but_work_tree();
		if (!hint_path)
			hint_path = get_but_dir();
		die(_("'%s' is outside repository at '%s'"), path,
		    absolute_path(hint_path));
	}
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
	char *to_free = NULL;
	struct stat st;

	if (skip_prefix(arg, ":/", &arg)) {
		if (!*arg) /* ":/" is root dir, always exists */
			return 1;
		prefix = NULL;
	} else if (skip_prefix(arg, ":!", &arg) ||
		   skip_prefix(arg, ":^", &arg)) {
		if (!*arg) /* excluding everything is silly, but allowed */
			return 1;
	}

	if (prefix)
		arg = to_free = prefix_filename(prefix, arg);

	if (!lstat(arg, &st)) {
		free(to_free);
		return 1; /* file exists */
	}
	if (is_missing_file_error(errno)) {
		free(to_free);
		return 0; /* file does not exist */
	}
	die_errno(_("failed to stat '%s'"), arg);
}

static void NORETURN die_verify_filename(struct repository *r,
					 const char *prefix,
					 const char *arg,
					 int diagnose_misspelt_rev)
{
	if (!diagnose_misspelt_rev)
		die(_("%s: no such path in the working tree.\n"
		      "Use 'but <command> -- <path>...' to specify paths that do not exist locally."),
		    arg);
	/*
	 * Saying "'(icase)foo' does not exist in the index" when the
	 * user gave us ":(icase)foo" is just stupid.  A magic pathspec
	 * begins with a colon and is followed by a non-alnum; do not
	 * let maybe_die_on_misspelt_object_name() even trigger.
	 */
	if (!(arg[0] == ':' && !isalnum(arg[1])))
		maybe_die_on_misspelt_object_name(r, arg, prefix);

	/* ... or fall back the most general message. */
	die(_("ambiguous argument '%s': unknown revision or path not in the working tree.\n"
	      "Use '--' to separate paths from revisions, like this:\n"
	      "'but <command> [<revision>...] -- [<file>...]'"), arg);

}

/*
 * Check for arguments that don't resolve as actual files,
 * but which look sufficiently like pathspecs that we'll consider
 * them such for the purposes of rev/pathspec DWIM parsing.
 */
static int looks_like_pathspec(const char *arg)
{
	const char *p;
	int escaped = 0;

	/*
	 * Wildcard characters imply the user is looking to match pathspecs
	 * that aren't in the filesystem. Note that this doesn't include
	 * backslash even though it's a glob special; by itself it doesn't
	 * cause any increase in the match. Likewise ignore backslash-escaped
	 * wildcard characters.
	 */
	for (p = arg; *p; p++) {
		if (escaped) {
			escaped = 0;
		} else if (is_glob_special(*p)) {
			if (*p == '\\')
				escaped = 1;
			else
				return 1;
		}
	}

	/* long-form pathspec magic */
	if (starts_with(arg, ":("))
		return 1;

	return 0;
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
		die(_("option '%s' must come before non-option arguments"), arg);
	if (looks_like_pathspec(arg) || check_filename(prefix, arg))
		return;
	die_verify_filename(the_repository, prefix, arg, diagnose_misspelt_rev);
}

/*
 * Opposite of the above: the command line did not have -- marker
 * and we parsed the arg as a refname.  It should not be interpretable
 * as a filename.
 */
void verify_non_filename(const char *prefix, const char *arg)
{
	if (!is_inside_work_tree() || is_inside_but_dir())
		return;
	if (*arg == '-')
		return; /* flag */
	if (!check_filename(prefix, arg))
		return;
	die(_("ambiguous argument '%s': both revision and filename\n"
	      "Use '--' to separate paths from revisions, like this:\n"
	      "'but <command> [<revision>...] -- [<file>...]'"), arg);
}

int get_common_dir(struct strbuf *sb, const char *butdir)
{
	const char *but_env_common_dir = getenv(BUT_COMMON_DIR_ENVIRONMENT);
	if (but_env_common_dir) {
		strbuf_addstr(sb, but_env_common_dir);
		return 1;
	} else {
		return get_common_dir_noenv(sb, butdir);
	}
}

int get_common_dir_noenv(struct strbuf *sb, const char *butdir)
{
	struct strbuf data = STRBUF_INIT;
	struct strbuf path = STRBUF_INIT;
	int ret = 0;

	strbuf_addf(&path, "%s/commondir", butdir);
	if (file_exists(path.buf)) {
		if (strbuf_read_file(&data, path.buf, 0) <= 0)
			die_errno(_("failed to read %s"), path.buf);
		while (data.len && (data.buf[data.len - 1] == '\n' ||
				    data.buf[data.len - 1] == '\r'))
			data.len--;
		data.buf[data.len] = '\0';
		strbuf_reset(&path);
		if (!is_absolute_path(data.buf))
			strbuf_addf(&path, "%s/", butdir);
		strbuf_addbuf(&path, &data);
		strbuf_add_real_path(sb, path.buf);
		ret = 1;
	} else {
		strbuf_addstr(sb, butdir);
	}

	strbuf_release(&data);
	strbuf_release(&path);
	return ret;
}

/*
 * Test if it looks like we're at a but directory.
 * We want to see:
 *
 *  - either an objects/ directory _or_ the proper
 *    BUT_OBJECT_DIRECTORY environment variable
 *  - a refs/ directory
 *  - either a HEAD symlink or a HEAD file that is formatted as
 *    a proper "ref:", or a regular file HEAD that has a properly
 *    formatted sha1 object name.
 */
int is_but_directory(const char *suspect)
{
	struct strbuf path = STRBUF_INIT;
	int ret = 0;
	size_t len;

	/* Check worktree-related signatures */
	strbuf_addstr(&path, suspect);
	strbuf_complete(&path, '/');
	strbuf_addstr(&path, "HEAD");
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

int is_nonbare_repository_dir(struct strbuf *path)
{
	int ret = 0;
	int butfile_error;
	size_t orig_path_len = path->len;
	assert(orig_path_len != 0);
	strbuf_complete(path, '/');
	strbuf_addstr(path, ".but");
	if (read_butfile_gently(path->buf, &butfile_error) || is_but_directory(path->buf))
		ret = 1;
	if (butfile_error == READ_BUTFILE_ERR_OPEN_FAILED ||
	    butfile_error == READ_BUTFILE_ERR_READ_FAILED)
		ret = 1;
	strbuf_setlen(path, orig_path_len);
	return ret;
}

int is_inside_but_dir(void)
{
	if (inside_but_dir < 0)
		inside_but_dir = is_inside_dir(get_but_dir());
	return inside_but_dir;
}

int is_inside_work_tree(void)
{
	if (inside_work_tree < 0)
		inside_work_tree = is_inside_dir(get_but_work_tree());
	return inside_work_tree;
}

void setup_work_tree(void)
{
	const char *work_tree;
	static int initialized = 0;

	if (initialized)
		return;

	if (work_tree_config_is_bogus)
		die(_("unable to set up work tree using invalid config"));

	work_tree = get_but_work_tree();
	if (!work_tree || chdir_notify(work_tree))
		die(_("this operation must be run in a work tree"));

	/*
	 * Make sure subsequent but processes find correct worktree
	 * if $BUT_WORK_TREE is set relative
	 */
	if (getenv(BUT_WORK_TREE_ENVIRONMENT))
		setenv(BUT_WORK_TREE_ENVIRONMENT, ".", 1);

	initialized = 1;
}

static void setup_original_cwd(void)
{
	struct strbuf tmp = STRBUF_INIT;
	const char *worktree = NULL;
	int offset = -1;

	if (!tmp_original_cwd)
		return;

	/*
	 * startup_info->original_cwd points to the current working
	 * directory we inherited from our parent process, which is a
	 * directory we want to avoid removing.
	 *
	 * For convience, we would like to have the path relative to the
	 * worktree instead of an absolute path.
	 *
	 * Yes, startup_info->original_cwd is usually the same as 'prefix',
	 * but differs in two ways:
	 *   - prefix has a trailing '/'
	 *   - if the user passes '-C' to but, that modifies the prefix but
	 *     not startup_info->original_cwd.
	 */

	/* Normalize the directory */
	strbuf_realpath(&tmp, tmp_original_cwd, 1);
	free((char*)tmp_original_cwd);
	tmp_original_cwd = NULL;
	startup_info->original_cwd = strbuf_detach(&tmp, NULL);

	/*
	 * Get our worktree; we only protect the current working directory
	 * if it's in the worktree.
	 */
	worktree = get_but_work_tree();
	if (!worktree)
		goto no_prevention_needed;

	offset = dir_inside_of(startup_info->original_cwd, worktree);
	if (offset >= 0) {
		/*
		 * If startup_info->original_cwd == worktree, that is already
		 * protected and we don't need original_cwd as a secondary
		 * protection measure.
		 */
		if (!*(startup_info->original_cwd + offset))
			goto no_prevention_needed;

		/*
		 * original_cwd was inside worktree; precompose it just as
		 * we do prefix so that built up paths will match
		 */
		startup_info->original_cwd = \
			precompose_string_if_needed(startup_info->original_cwd
						    + offset);
		return;
	}

no_prevention_needed:
	free((char*)startup_info->original_cwd);
	startup_info->original_cwd = NULL;
}

static int read_worktree_config(const char *var, const char *value, void *vdata)
{
	struct repository_format *data = vdata;

	if (strcmp(var, "core.bare") == 0) {
		data->is_bare = but_config_bool(var, value);
	} else if (strcmp(var, "core.worktree") == 0) {
		if (!value)
			return config_error_nonbool(var);
		free(data->work_tree);
		data->work_tree = xstrdup(value);
	}
	return 0;
}

enum extension_result {
	EXTENSION_ERROR = -1, /* compatible with error(), etc */
	EXTENSION_UNKNOWN = 0,
	EXTENSION_OK = 1
};

/*
 * Do not add new extensions to this function. It handles extensions which are
 * respected even in v0-format repositories for historical compatibility.
 */
static enum extension_result handle_extension_v0(const char *var,
						 const char *value,
						 const char *ext,
						 struct repository_format *data)
{
		if (!strcmp(ext, "noop")) {
			return EXTENSION_OK;
		} else if (!strcmp(ext, "preciousobjects")) {
			data->precious_objects = but_config_bool(var, value);
			return EXTENSION_OK;
		} else if (!strcmp(ext, "partialclone")) {
			data->partial_clone = xstrdup(value);
			return EXTENSION_OK;
		} else if (!strcmp(ext, "worktreeconfig")) {
			data->worktree_config = but_config_bool(var, value);
			return EXTENSION_OK;
		}

		return EXTENSION_UNKNOWN;
}

/*
 * Record any new extensions in this function.
 */
static enum extension_result handle_extension(const char *var,
					      const char *value,
					      const char *ext,
					      struct repository_format *data)
{
	if (!strcmp(ext, "noop-v1")) {
		return EXTENSION_OK;
	} else if (!strcmp(ext, "objectformat")) {
		int format;

		if (!value)
			return config_error_nonbool(var);
		format = hash_algo_by_name(value);
		if (format == BUT_HASH_UNKNOWN)
			return error(_("invalid value for '%s': '%s'"),
				     "extensions.objectformat", value);
		data->hash_algo = format;
		return EXTENSION_OK;
	}
	return EXTENSION_UNKNOWN;
}

static int check_repo_format(const char *var, const char *value, void *vdata)
{
	struct repository_format *data = vdata;
	const char *ext;

	if (strcmp(var, "core.repositoryformatversion") == 0)
		data->version = but_config_int(var, value);
	else if (skip_prefix(var, "extensions.", &ext)) {
		switch (handle_extension_v0(var, value, ext, data)) {
		case EXTENSION_ERROR:
			return -1;
		case EXTENSION_OK:
			return 0;
		case EXTENSION_UNKNOWN:
			break;
		}

		switch (handle_extension(var, value, ext, data)) {
		case EXTENSION_ERROR:
			return -1;
		case EXTENSION_OK:
			string_list_append(&data->v1_only_extensions, ext);
			return 0;
		case EXTENSION_UNKNOWN:
			string_list_append(&data->unknown_extensions, ext);
			return 0;
		}
	}

	return read_worktree_config(var, value, vdata);
}

static int check_repository_format_gently(const char *butdir, struct repository_format *candidate, int *nonbut_ok)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;
	int has_common;

	has_common = get_common_dir(&sb, butdir);
	strbuf_addstr(&sb, "/config");
	read_repository_format(candidate, sb.buf);
	strbuf_release(&sb);

	/*
	 * For historical use of check_repository_format() in but-init,
	 * we treat a missing config as a silent "ok", even when nonbut_ok
	 * is unset.
	 */
	if (candidate->version < 0)
		return 0;

	if (verify_repository_format(candidate, &err) < 0) {
		if (nonbut_ok) {
			warning("%s", err.buf);
			strbuf_release(&err);
			*nonbut_ok = -1;
			return -1;
		}
		die("%s", err.buf);
	}

	repository_format_precious_objects = candidate->precious_objects;
	repository_format_worktree_config = candidate->worktree_config;
	string_list_clear(&candidate->unknown_extensions, 0);
	string_list_clear(&candidate->v1_only_extensions, 0);

	if (repository_format_worktree_config) {
		/*
		 * pick up core.bare and core.worktree from per-worktree
		 * config if present
		 */
		strbuf_addf(&sb, "%s/config.worktree", butdir);
		but_config_from_file(read_worktree_config, sb.buf, candidate);
		strbuf_release(&sb);
		has_common = 0;
	}

	if (!has_common) {
		if (candidate->is_bare != -1) {
			is_bare_repository_cfg = candidate->is_bare;
			if (is_bare_repository_cfg == 1)
				inside_work_tree = -1;
		}
		if (candidate->work_tree) {
			free(but_work_tree_cfg);
			but_work_tree_cfg = xstrdup(candidate->work_tree);
			inside_work_tree = -1;
		}
	}

	return 0;
}

int upgrade_repository_format(int target_version)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;
	struct strbuf repo_version = STRBUF_INIT;
	struct repository_format repo_fmt = REPOSITORY_FORMAT_INIT;

	strbuf_but_common_path(&sb, the_repository, "config");
	read_repository_format(&repo_fmt, sb.buf);
	strbuf_release(&sb);

	if (repo_fmt.version >= target_version)
		return 0;

	if (verify_repository_format(&repo_fmt, &err) < 0) {
		error("cannot upgrade repository format from %d to %d: %s",
		      repo_fmt.version, target_version, err.buf);
		strbuf_release(&err);
		return -1;
	}
	if (!repo_fmt.version && repo_fmt.unknown_extensions.nr)
		return error("cannot upgrade repository format: "
			     "unknown extension %s",
			     repo_fmt.unknown_extensions.items[0].string);

	strbuf_addf(&repo_version, "%d", target_version);
	but_config_set("core.repositoryformatversion", repo_version.buf);
	strbuf_release(&repo_version);
	return 1;
}

static void init_repository_format(struct repository_format *format)
{
	const struct repository_format fresh = REPOSITORY_FORMAT_INIT;

	memcpy(format, &fresh, sizeof(fresh));
}

int read_repository_format(struct repository_format *format, const char *path)
{
	clear_repository_format(format);
	but_config_from_file(check_repo_format, path, format);
	if (format->version == -1)
		clear_repository_format(format);
	return format->version;
}

void clear_repository_format(struct repository_format *format)
{
	string_list_clear(&format->unknown_extensions, 0);
	string_list_clear(&format->v1_only_extensions, 0);
	free(format->work_tree);
	free(format->partial_clone);
	init_repository_format(format);
}

int verify_repository_format(const struct repository_format *format,
			     struct strbuf *err)
{
	if (BUT_REPO_VERSION_READ < format->version) {
		strbuf_addf(err, _("Expected but repo version <= %d, found %d"),
			    BUT_REPO_VERSION_READ, format->version);
		return -1;
	}

	if (format->version >= 1 && format->unknown_extensions.nr) {
		int i;

		strbuf_addstr(err, Q_("unknown repository extension found:",
				      "unknown repository extensions found:",
				      format->unknown_extensions.nr));

		for (i = 0; i < format->unknown_extensions.nr; i++)
			strbuf_addf(err, "\n\t%s",
				    format->unknown_extensions.items[i].string);
		return -1;
	}

	if (format->version == 0 && format->v1_only_extensions.nr) {
		int i;

		strbuf_addstr(err,
			      Q_("repo version is 0, but v1-only extension found:",
				 "repo version is 0, but v1-only extensions found:",
				 format->v1_only_extensions.nr));

		for (i = 0; i < format->v1_only_extensions.nr; i++)
			strbuf_addf(err, "\n\t%s",
				    format->v1_only_extensions.items[i].string);
		return -1;
	}

	return 0;
}

void read_butfile_error_die(int error_code, const char *path, const char *dir)
{
	switch (error_code) {
	case READ_BUTFILE_ERR_STAT_FAILED:
	case READ_BUTFILE_ERR_NOT_A_FILE:
		/* non-fatal; follow return path */
		break;
	case READ_BUTFILE_ERR_OPEN_FAILED:
		die_errno(_("error opening '%s'"), path);
	case READ_BUTFILE_ERR_TOO_LARGE:
		die(_("too large to be a .but file: '%s'"), path);
	case READ_BUTFILE_ERR_READ_FAILED:
		die(_("error reading %s"), path);
	case READ_BUTFILE_ERR_INVALID_FORMAT:
		die(_("invalid butfile format: %s"), path);
	case READ_BUTFILE_ERR_NO_PATH:
		die(_("no path in butfile: %s"), path);
	case READ_BUTFILE_ERR_NOT_A_REPO:
		die(_("not a but repository: %s"), dir);
	default:
		BUG("unknown error code");
	}
}

/*
 * Try to read the location of the but directory from the .but file,
 * return path to but directory if found. The return value comes from
 * a shared buffer.
 *
 * On failure, if return_error_code is not NULL, return_error_code
 * will be set to an error code and NULL will be returned. If
 * return_error_code is NULL the function will die instead (for most
 * cases).
 */
const char *read_butfile_gently(const char *path, int *return_error_code)
{
	const int max_file_size = 1 << 20;  /* 1MB */
	int error_code = 0;
	char *buf = NULL;
	char *dir = NULL;
	const char *slash;
	struct stat st;
	int fd;
	ssize_t len;
	static struct strbuf realpath = STRBUF_INIT;

	if (stat(path, &st)) {
		/* NEEDSWORK: discern between ENOENT vs other errors */
		error_code = READ_BUTFILE_ERR_STAT_FAILED;
		goto cleanup_return;
	}
	if (!S_ISREG(st.st_mode)) {
		error_code = READ_BUTFILE_ERR_NOT_A_FILE;
		goto cleanup_return;
	}
	if (st.st_size > max_file_size) {
		error_code = READ_BUTFILE_ERR_TOO_LARGE;
		goto cleanup_return;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		error_code = READ_BUTFILE_ERR_OPEN_FAILED;
		goto cleanup_return;
	}
	buf = xmallocz(st.st_size);
	len = read_in_full(fd, buf, st.st_size);
	close(fd);
	if (len != st.st_size) {
		error_code = READ_BUTFILE_ERR_READ_FAILED;
		goto cleanup_return;
	}
	if (!starts_with(buf, "butdir: ")) {
		error_code = READ_BUTFILE_ERR_INVALID_FORMAT;
		goto cleanup_return;
	}
	while (buf[len - 1] == '\n' || buf[len - 1] == '\r')
		len--;
	if (len < 9) {
		error_code = READ_BUTFILE_ERR_NO_PATH;
		goto cleanup_return;
	}
	buf[len] = '\0';
	dir = buf + 8;

	if (!is_absolute_path(dir) && (slash = strrchr(path, '/'))) {
		size_t pathlen = slash+1 - path;
		dir = xstrfmt("%.*s%.*s", (int)pathlen, path,
			      (int)(len - 8), buf + 8);
		free(buf);
		buf = dir;
	}
	if (!is_but_directory(dir)) {
		error_code = READ_BUTFILE_ERR_NOT_A_REPO;
		goto cleanup_return;
	}

	strbuf_realpath(&realpath, dir, 1);
	path = realpath.buf;

cleanup_return:
	if (return_error_code)
		*return_error_code = error_code;
	else if (error_code)
		read_butfile_error_die(error_code, path, dir);

	free(buf);
	return error_code ? NULL : path;
}

static const char *setup_explicit_but_dir(const char *butdirenv,
					  struct strbuf *cwd,
					  struct repository_format *repo_fmt,
					  int *nonbut_ok)
{
	const char *work_tree_env = getenv(BUT_WORK_TREE_ENVIRONMENT);
	const char *worktree;
	char *butfile;
	int offset;

	if (PATH_MAX - 40 < strlen(butdirenv))
		die(_("'$%s' too big"), BUT_DIR_ENVIRONMENT);

	butfile = (char*)read_butfile(butdirenv);
	if (butfile) {
		butfile = xstrdup(butfile);
		butdirenv = butfile;
	}

	if (!is_but_directory(butdirenv)) {
		if (nonbut_ok) {
			*nonbut_ok = 1;
			free(butfile);
			return NULL;
		}
		die(_("not a but repository: '%s'"), butdirenv);
	}

	if (check_repository_format_gently(butdirenv, repo_fmt, nonbut_ok)) {
		free(butfile);
		return NULL;
	}

	/* #3, #7, #11, #15, #19, #23, #27, #31 (see t1510) */
	if (work_tree_env)
		set_but_work_tree(work_tree_env);
	else if (is_bare_repository_cfg > 0) {
		if (but_work_tree_cfg) {
			/* #22.2, #30 */
			warning("core.bare and core.worktree do not make sense");
			work_tree_config_is_bogus = 1;
		}

		/* #18, #26 */
		set_but_dir(butdirenv, 0);
		free(butfile);
		return NULL;
	}
	else if (but_work_tree_cfg) { /* #6, #14 */
		if (is_absolute_path(but_work_tree_cfg))
			set_but_work_tree(but_work_tree_cfg);
		else {
			char *core_worktree;
			if (chdir(butdirenv))
				die_errno(_("cannot chdir to '%s'"), butdirenv);
			if (chdir(but_work_tree_cfg))
				die_errno(_("cannot chdir to '%s'"), but_work_tree_cfg);
			core_worktree = xgetcwd();
			if (chdir(cwd->buf))
				die_errno(_("cannot come back to cwd"));
			set_but_work_tree(core_worktree);
			free(core_worktree);
		}
	}
	else if (!but_env_bool(BUT_IMPLICIT_WORK_TREE_ENVIRONMENT, 1)) {
		/* #16d */
		set_but_dir(butdirenv, 0);
		free(butfile);
		return NULL;
	}
	else /* #2, #10 */
		set_but_work_tree(".");

	/* set_but_work_tree() must have been called by now */
	worktree = get_but_work_tree();

	/* both get_but_work_tree() and cwd are already normalized */
	if (!strcmp(cwd->buf, worktree)) { /* cwd == worktree */
		set_but_dir(butdirenv, 0);
		free(butfile);
		return NULL;
	}

	offset = dir_inside_of(cwd->buf, worktree);
	if (offset >= 0) {	/* cwd inside worktree? */
		set_but_dir(butdirenv, 1);
		if (chdir(worktree))
			die_errno(_("cannot chdir to '%s'"), worktree);
		strbuf_addch(cwd, '/');
		free(butfile);
		return cwd->buf + offset;
	}

	/* cwd outside worktree */
	set_but_dir(butdirenv, 0);
	free(butfile);
	return NULL;
}

static const char *setup_discovered_but_dir(const char *butdir,
					    struct strbuf *cwd, int offset,
					    struct repository_format *repo_fmt,
					    int *nonbut_ok)
{
	if (check_repository_format_gently(butdir, repo_fmt, nonbut_ok))
		return NULL;

	/* --work-tree is set without --but-dir; use discovered one */
	if (getenv(BUT_WORK_TREE_ENVIRONMENT) || but_work_tree_cfg) {
		char *to_free = NULL;
		const char *ret;

		if (offset != cwd->len && !is_absolute_path(butdir))
			butdir = to_free = real_pathdup(butdir, 1);
		if (chdir(cwd->buf))
			die_errno(_("cannot come back to cwd"));
		ret = setup_explicit_but_dir(butdir, cwd, repo_fmt, nonbut_ok);
		free(to_free);
		return ret;
	}

	/* #16.2, #17.2, #20.2, #21.2, #24, #25, #28, #29 (see t1510) */
	if (is_bare_repository_cfg > 0) {
		set_but_dir(butdir, (offset != cwd->len));
		if (chdir(cwd->buf))
			die_errno(_("cannot come back to cwd"));
		return NULL;
	}

	/* #0, #1, #5, #8, #9, #12, #13 */
	set_but_work_tree(".");
	if (strcmp(butdir, DEFAULT_BUT_DIR_ENVIRONMENT))
		set_but_dir(butdir, 0);
	inside_but_dir = 0;
	inside_work_tree = 1;
	if (offset >= cwd->len)
		return NULL;

	/* Make "offset" point past the '/' (already the case for root dirs) */
	if (offset != offset_1st_component(cwd->buf))
		offset++;
	/* Add a '/' at the end */
	strbuf_addch(cwd, '/');
	return cwd->buf + offset;
}

/* #16.1, #17.1, #20.1, #21.1, #22.1 (see t1510) */
static const char *setup_bare_but_dir(struct strbuf *cwd, int offset,
				      struct repository_format *repo_fmt,
				      int *nonbut_ok)
{
	int root_len;

	if (check_repository_format_gently(".", repo_fmt, nonbut_ok))
		return NULL;

	setenv(BUT_IMPLICIT_WORK_TREE_ENVIRONMENT, "0", 1);

	/* --work-tree is set without --but-dir; use discovered one */
	if (getenv(BUT_WORK_TREE_ENVIRONMENT) || but_work_tree_cfg) {
		static const char *butdir;

		butdir = offset == cwd->len ? "." : xmemdupz(cwd->buf, offset);
		if (chdir(cwd->buf))
			die_errno(_("cannot come back to cwd"));
		return setup_explicit_but_dir(butdir, cwd, repo_fmt, nonbut_ok);
	}

	inside_but_dir = 1;
	inside_work_tree = 0;
	if (offset != cwd->len) {
		if (chdir(cwd->buf))
			die_errno(_("cannot come back to cwd"));
		root_len = offset_1st_component(cwd->buf);
		strbuf_setlen(cwd, offset > root_len ? offset : root_len);
		set_but_dir(cwd->buf, 0);
	}
	else
		set_but_dir(".", 0);
	return NULL;
}

static dev_t get_device_or_die(const char *path, const char *prefix, int prefix_len)
{
	struct stat buf;
	if (stat(path, &buf)) {
		die_errno(_("failed to stat '%*s%s%s'"),
				prefix_len,
				prefix ? prefix : "",
				prefix ? "/" : "", path);
	}
	return buf.st_dev;
}

/*
 * A "string_list_each_func_t" function that canonicalizes an entry
 * from BUT_CEILING_DIRECTORIES using real_pathdup(), or
 * discards it if unusable.  The presence of an empty entry in
 * BUT_CEILING_DIRECTORIES turns off canonicalization for all
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
		char *real_path = real_pathdup(ceil, 0);
		if (!real_path) {
			return 0;
		}
		free(item->string);
		item->string = real_path;
		return 1;
	}
}

struct safe_directory_data {
	const char *path;
	int is_safe;
};

static int safe_directory_cb(const char *key, const char *value, void *d)
{
	struct safe_directory_data *data = d;

	if (strcmp(key, "safe.directory"))
		return 0;

	if (!value || !*value) {
		data->is_safe = 0;
	} else if (!strcmp(value, "*")) {
		data->is_safe = 1;
	} else {
		const char *interpolated = NULL;

		if (!but_config_pathname(&interpolated, key, value) &&
		    !fspathcmp(data->path, interpolated ? interpolated : value))
			data->is_safe = 1;

		free((char *)interpolated);
	}

	return 0;
}

static int ensure_valid_ownership(const char *path)
{
	struct safe_directory_data data = { .path = path };

	if (!but_env_bool("BUT_TEST_ASSUME_DIFFERENT_OWNER", 0) &&
	    is_path_owned_by_current_user(path))
		return 1;

	read_very_early_config(safe_directory_cb, &data);

	return data.is_safe;
}

enum discovery_result {
	BUT_DIR_NONE = 0,
	BUT_DIR_EXPLICIT,
	BUT_DIR_DISCOVERED,
	BUT_DIR_BARE,
	/* these are errors */
	BUT_DIR_HIT_CEILING = -1,
	BUT_DIR_HIT_MOUNT_POINT = -2,
	BUT_DIR_INVALID_BUTFILE = -3,
	BUT_DIR_INVALID_OWNERSHIP = -4
};

/*
 * We cannot decide in this function whether we are in the work tree or
 * not, since the config can only be read _after_ this function was called.
 *
 * Also, we avoid changing any global state (such as the current working
 * directory) to allow early callers.
 *
 * The directory where the search should start needs to be passed in via the
 * `dir` parameter; upon return, the `dir` buffer will contain the path of
 * the directory where the search ended, and `butdir` will contain the path of
 * the discovered .but/ directory, if any. If `butdir` is not absolute, it
 * is relative to `dir` (i.e. *not* necessarily the cwd).
 */
static enum discovery_result setup_but_directory_gently_1(struct strbuf *dir,
							  struct strbuf *butdir,
							  int die_on_error)
{
	const char *env_ceiling_dirs = getenv(CEILING_DIRECTORIES_ENVIRONMENT);
	struct string_list ceiling_dirs = STRING_LIST_INIT_DUP;
	const char *butdirenv;
	int ceil_offset = -1, min_offset = offset_1st_component(dir->buf);
	dev_t current_device = 0;
	int one_filesystem = 1;

	/*
	 * If BUT_DIR is set explicitly, we're not going
	 * to do any discovery, but we still do repository
	 * validation.
	 */
	butdirenv = getenv(BUT_DIR_ENVIRONMENT);
	if (butdirenv) {
		strbuf_addstr(butdir, butdirenv);
		return BUT_DIR_EXPLICIT;
	}

	if (env_ceiling_dirs) {
		int empty_entry_found = 0;

		string_list_split(&ceiling_dirs, env_ceiling_dirs, PATH_SEP, -1);
		filter_string_list(&ceiling_dirs, 0,
				   canonicalize_ceiling_entry, &empty_entry_found);
		ceil_offset = longest_ancestor_length(dir->buf, &ceiling_dirs);
		string_list_clear(&ceiling_dirs, 0);
	}

	if (ceil_offset < 0)
		ceil_offset = min_offset - 2;

	if (min_offset && min_offset == dir->len &&
	    !is_dir_sep(dir->buf[min_offset - 1])) {
		strbuf_addch(dir, '/');
		min_offset++;
	}

	/*
	 * Test in the following order (relative to the dir):
	 * - .but (file containing "butdir: <path>")
	 * - .but/
	 * - ./ (bare)
	 * - ../.but
	 * - ../.but/
	 * - ../ (bare)
	 * - ../../.but
	 *   etc.
	 */
	one_filesystem = !but_env_bool("BUT_DISCOVERY_ACROSS_FILESYSTEM", 0);
	if (one_filesystem)
		current_device = get_device_or_die(dir->buf, NULL, 0);
	for (;;) {
		int offset = dir->len, error_code = 0;

		if (offset > min_offset)
			strbuf_addch(dir, '/');
		strbuf_addstr(dir, DEFAULT_BUT_DIR_ENVIRONMENT);
		butdirenv = read_butfile_gently(dir->buf, die_on_error ?
						NULL : &error_code);
		if (!butdirenv) {
			if (die_on_error ||
			    error_code == READ_BUTFILE_ERR_NOT_A_FILE) {
				/* NEEDSWORK: fail if .but is not file nor dir */
				if (is_but_directory(dir->buf))
					butdirenv = DEFAULT_BUT_DIR_ENVIRONMENT;
			} else if (error_code != READ_BUTFILE_ERR_STAT_FAILED)
				return BUT_DIR_INVALID_BUTFILE;
		}
		strbuf_setlen(dir, offset);
		if (butdirenv) {
			if (!ensure_valid_ownership(dir->buf))
				return BUT_DIR_INVALID_OWNERSHIP;
			strbuf_addstr(butdir, butdirenv);
			return BUT_DIR_DISCOVERED;
		}

		if (is_but_directory(dir->buf)) {
			if (!ensure_valid_ownership(dir->buf))
				return BUT_DIR_INVALID_OWNERSHIP;
			strbuf_addstr(butdir, ".");
			return BUT_DIR_BARE;
		}

		if (offset <= min_offset)
			return BUT_DIR_HIT_CEILING;

		while (--offset > ceil_offset && !is_dir_sep(dir->buf[offset]))
			; /* continue */
		if (offset <= ceil_offset)
			return BUT_DIR_HIT_CEILING;

		strbuf_setlen(dir, offset > min_offset ?  offset : min_offset);
		if (one_filesystem &&
		    current_device != get_device_or_die(dir->buf, NULL, offset))
			return BUT_DIR_HIT_MOUNT_POINT;
	}
}

int discover_but_directory(struct strbuf *commondir,
			   struct strbuf *butdir)
{
	struct strbuf dir = STRBUF_INIT, err = STRBUF_INIT;
	size_t butdir_offset = butdir->len, cwd_len;
	size_t commondir_offset = commondir->len;
	struct repository_format candidate = REPOSITORY_FORMAT_INIT;

	if (strbuf_getcwd(&dir))
		return -1;

	cwd_len = dir.len;
	if (setup_but_directory_gently_1(&dir, butdir, 0) <= 0) {
		strbuf_release(&dir);
		return -1;
	}

	/*
	 * The returned butdir is relative to dir, and if dir does not reflect
	 * the current working directory, we simply make the butdir absolute.
	 */
	if (dir.len < cwd_len && !is_absolute_path(butdir->buf + butdir_offset)) {
		/* Avoid a trailing "/." */
		if (!strcmp(".", butdir->buf + butdir_offset))
			strbuf_setlen(butdir, butdir_offset);
		else
			strbuf_addch(&dir, '/');
		strbuf_insert(butdir, butdir_offset, dir.buf, dir.len);
	}

	get_common_dir(commondir, butdir->buf + butdir_offset);

	strbuf_reset(&dir);
	strbuf_addf(&dir, "%s/config", commondir->buf + commondir_offset);
	read_repository_format(&candidate, dir.buf);
	strbuf_release(&dir);

	if (verify_repository_format(&candidate, &err) < 0) {
		warning("ignoring but dir '%s': %s",
			butdir->buf + butdir_offset, err.buf);
		strbuf_release(&err);
		strbuf_setlen(commondir, commondir_offset);
		strbuf_setlen(butdir, butdir_offset);
		clear_repository_format(&candidate);
		return -1;
	}

	/* take ownership of candidate.partial_clone */
	the_repository->repository_format_partial_clone =
		candidate.partial_clone;
	candidate.partial_clone = NULL;

	clear_repository_format(&candidate);
	return 0;
}

const char *setup_but_directory_gently(int *nonbut_ok)
{
	static struct strbuf cwd = STRBUF_INIT;
	struct strbuf dir = STRBUF_INIT, butdir = STRBUF_INIT;
	const char *prefix = NULL;
	struct repository_format repo_fmt = REPOSITORY_FORMAT_INIT;

	/*
	 * We may have read an incomplete configuration before
	 * setting-up the but directory. If so, clear the cache so
	 * that the next queries to the configuration reload complete
	 * configuration (including the per-repo config file that we
	 * ignored previously).
	 */
	but_config_clear();

	/*
	 * Let's assume that we are in a but repository.
	 * If it turns out later that we are somewhere else, the value will be
	 * updated accordingly.
	 */
	if (nonbut_ok)
		*nonbut_ok = 0;

	if (strbuf_getcwd(&cwd))
		die_errno(_("Unable to read current working directory"));
	strbuf_addbuf(&dir, &cwd);

	switch (setup_but_directory_gently_1(&dir, &butdir, 1)) {
	case BUT_DIR_EXPLICIT:
		prefix = setup_explicit_but_dir(butdir.buf, &cwd, &repo_fmt, nonbut_ok);
		break;
	case BUT_DIR_DISCOVERED:
		if (dir.len < cwd.len && chdir(dir.buf))
			die(_("cannot change to '%s'"), dir.buf);
		prefix = setup_discovered_but_dir(butdir.buf, &cwd, dir.len,
						  &repo_fmt, nonbut_ok);
		break;
	case BUT_DIR_BARE:
		if (dir.len < cwd.len && chdir(dir.buf))
			die(_("cannot change to '%s'"), dir.buf);
		prefix = setup_bare_but_dir(&cwd, dir.len, &repo_fmt, nonbut_ok);
		break;
	case BUT_DIR_HIT_CEILING:
		if (!nonbut_ok)
			die(_("not a but repository (or any of the parent directories): %s"),
			    DEFAULT_BUT_DIR_ENVIRONMENT);
		*nonbut_ok = 1;
		break;
	case BUT_DIR_HIT_MOUNT_POINT:
		if (!nonbut_ok)
			die(_("not a but repository (or any parent up to mount point %s)\n"
			      "Stopping at filesystem boundary (BUT_DISCOVERY_ACROSS_FILESYSTEM not set)."),
			    dir.buf);
		*nonbut_ok = 1;
		break;
	case BUT_DIR_INVALID_OWNERSHIP:
		if (!nonbut_ok) {
			struct strbuf quoted = STRBUF_INIT;

			sq_quote_buf_pretty(&quoted, dir.buf);
			die(_("unsafe repository ('%s' is owned by someone else)\n"
			      "To add an exception for this directory, call:\n"
			      "\n"
			      "\tbut config --global --add safe.directory %s"),
			    dir.buf, quoted.buf);
		}
		*nonbut_ok = 1;
		break;
	case BUT_DIR_NONE:
		/*
		 * As a safeguard against setup_but_directory_gently_1 returning
		 * this value, fallthrough to BUG. Otherwise it is possible to
		 * set startup_info->have_repository to 1 when we did nothing to
		 * find a repository.
		 */
	default:
		BUG("unhandled setup_but_directory_1() result");
	}

	/*
	 * At this point, nonbut_ok is stable. If it is non-NULL and points
	 * to a non-zero value, then this means that we haven't found a
	 * repository and that the caller expects startup_info to reflect
	 * this.
	 *
	 * Regardless of the state of nonbut_ok, startup_info->prefix and
	 * the BUT_PREFIX environment variable must always match. For details
	 * see Documentation/config/alias.txt.
	 */
	if (nonbut_ok && *nonbut_ok)
		startup_info->have_repository = 0;
	else
		startup_info->have_repository = 1;

	/*
	 * Not all paths through the setup code will call 'set_but_dir()' (which
	 * directly sets up the environment) so in order to guarantee that the
	 * environment is in a consistent state after setup, explicitly setup
	 * the environment if we have a repository.
	 *
	 * NEEDSWORK: currently we allow bogus BUT_DIR values to be set in some
	 * code paths so we also need to explicitly setup the environment if
	 * the user has set BUT_DIR.  It may be beneficial to disallow bogus
	 * BUT_DIR values at some point in the future.
	 */
	if (/* BUT_DIR_EXPLICIT, BUT_DIR_DISCOVERED, BUT_DIR_BARE */
	    startup_info->have_repository ||
	    /* BUT_DIR_EXPLICIT */
	    getenv(BUT_DIR_ENVIRONMENT)) {
		if (!the_repository->butdir) {
			const char *butdir = getenv(BUT_DIR_ENVIRONMENT);
			if (!butdir)
				butdir = DEFAULT_BUT_DIR_ENVIRONMENT;
			setup_but_env(butdir);
		}
		if (startup_info->have_repository) {
			repo_set_hash_algo(the_repository, repo_fmt.hash_algo);
			/* take ownership of repo_fmt.partial_clone */
			the_repository->repository_format_partial_clone =
				repo_fmt.partial_clone;
			repo_fmt.partial_clone = NULL;
		}
	}
	/*
	 * Since precompose_string_if_needed() needs to look at
	 * the core.precomposeunicode configuration, this
	 * has to happen after the above block that finds
	 * out where the repository is, i.e. a preparation
	 * for calling but_config_get_bool().
	 */
	if (prefix) {
		prefix = precompose_string_if_needed(prefix);
		startup_info->prefix = prefix;
		setenv(BUT_PREFIX_ENVIRONMENT, prefix, 1);
	} else {
		startup_info->prefix = NULL;
		setenv(BUT_PREFIX_ENVIRONMENT, "", 1);
	}

	setup_original_cwd();

	strbuf_release(&dir);
	strbuf_release(&butdir);
	clear_repository_format(&repo_fmt);

	return prefix;
}

int but_config_perm(const char *var, const char *value)
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
		return but_config_bool(var, value) ? PERM_GROUP : PERM_UMASK;

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
		die(_("problem with core.sharedRepository filemode value "
		    "(0%.3o).\nThe owner of files must always have "
		    "read and write permissions."), i);

	/*
	 * Mask filemode value. Others can not get write permission.
	 * x flags for directories are handled separately.
	 */
	return -(i & 0666);
}

void check_repository_format(struct repository_format *fmt)
{
	struct repository_format repo_fmt = REPOSITORY_FORMAT_INIT;
	if (!fmt)
		fmt = &repo_fmt;
	check_repository_format_gently(get_but_dir(), fmt, NULL);
	startup_info->have_repository = 1;
	repo_set_hash_algo(the_repository, fmt->hash_algo);
	the_repository->repository_format_partial_clone =
		xstrdup_or_null(fmt->partial_clone);
	clear_repository_format(&repo_fmt);
}

/*
 * Returns the "prefix", a path to the current working directory
 * relative to the work tree root, or NULL, if the current working
 * directory is not a strict subdirectory of the work tree root. The
 * prefix always ends with a '/' character.
 */
const char *setup_but_directory(void)
{
	return setup_but_directory_gently(NULL);
}

const char *resolve_butdir_gently(const char *suspect, int *return_error_code)
{
	if (is_but_directory(suspect))
		return suspect;
	return read_butfile_gently(suspect, return_error_code);
}

/* if any standard file descriptor is missing open it to /dev/null */
void sanitize_stdfds(void)
{
	int fd = xopen("/dev/null", O_RDWR);
	while (fd < 2)
		fd = xdup(fd);
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
			die_errno(_("fork failed"));
		default:
			exit(0);
	}
	if (setsid() == -1)
		die_errno(_("setsid failed"));
	close(0);
	close(1);
	close(2);
	sanitize_stdfds();
	return 0;
#endif
}
