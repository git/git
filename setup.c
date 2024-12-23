#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "abspath.h"
#include "copy.h"
#include "environment.h"
#include "exec-cmd.h"
#include "gettext.h"
#include "hex.h"
#include "object-file.h"
#include "object-name.h"
#include "refs.h"
#include "replace-object.h"
#include "repository.h"
#include "config.h"
#include "dir.h"
#include "setup.h"
#include "shallow.h"
#include "string-list.h"
#include "strvec.h"
#include "chdir-notify.h"
#include "path.h"
#include "quote.h"
#include "tmp-objdir.h"
#include "trace.h"
#include "trace2.h"
#include "worktree.h"
#include "exec-cmd.h"

static int inside_git_dir = -1;
static int inside_work_tree = -1;
static int work_tree_config_is_bogus;
enum allowed_bare_repo {
	ALLOWED_BARE_REPO_EXPLICIT = 0,
	ALLOWED_BARE_REPO_ALL,
};

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
	const char *work_tree = precompose_string_if_needed(repo_get_work_tree(the_repository));
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
		const char *hint_path = repo_get_work_tree(the_repository);
		if (!hint_path)
			hint_path = repo_get_git_dir(the_repository);
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
		      "Use 'git <command> -- <path>...' to specify paths that do not exist locally."),
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
	      "'git <command> [<revision>...] -- [<file>...]'"), arg);

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
	if (!is_inside_work_tree() || is_inside_git_dir())
		return;
	if (*arg == '-')
		return; /* flag */
	if (!check_filename(prefix, arg))
		return;
	die(_("ambiguous argument '%s': both revision and filename\n"
	      "Use '--' to separate paths from revisions, like this:\n"
	      "'git <command> [<revision>...] -- [<file>...]'"), arg);
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
		strbuf_add_real_path(sb, path.buf);
		ret = 1;
	} else {
		strbuf_addstr(sb, gitdir);
	}

	strbuf_release(&data);
	strbuf_release(&path);
	return ret;
}

static int validate_headref(const char *path)
{
	struct stat st;
	char buffer[256];
	const char *refname;
	struct object_id oid;
	int fd;
	ssize_t len;

	if (lstat(path, &st) < 0)
		return -1;

	/* Make sure it is a "refs/.." symlink */
	if (S_ISLNK(st.st_mode)) {
		len = readlink(path, buffer, sizeof(buffer)-1);
		if (len >= 5 && !memcmp("refs/", buffer, 5))
			return 0;
		return -1;
	}

	/*
	 * Anything else, just open it and try to see if it is a symbolic ref.
	 */
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	len = read_in_full(fd, buffer, sizeof(buffer)-1);
	close(fd);

	if (len < 0)
		return -1;
	buffer[len] = '\0';

	/*
	 * Is it a symbolic ref?
	 */
	if (skip_prefix(buffer, "ref:", &refname)) {
		while (isspace(*refname))
			refname++;
		if (starts_with(refname, "refs/"))
			return 0;
	}

	/*
	 * Is this a detached HEAD?
	 */
	if (get_oid_hex_any(buffer, &oid) != GIT_HASH_UNKNOWN)
		return 0;

	return -1;
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
	int gitfile_error;
	size_t orig_path_len = path->len;
	assert(orig_path_len != 0);
	strbuf_complete(path, '/');
	strbuf_addstr(path, ".git");
	if (read_gitfile_gently(path->buf, &gitfile_error) || is_git_directory(path->buf))
		ret = 1;
	if (gitfile_error == READ_GITFILE_ERR_OPEN_FAILED ||
	    gitfile_error == READ_GITFILE_ERR_READ_FAILED)
		ret = 1;
	strbuf_setlen(path, orig_path_len);
	return ret;
}

int is_inside_git_dir(void)
{
	if (inside_git_dir < 0)
		inside_git_dir = is_inside_dir(repo_get_git_dir(the_repository));
	return inside_git_dir;
}

int is_inside_work_tree(void)
{
	if (inside_work_tree < 0)
		inside_work_tree = is_inside_dir(repo_get_work_tree(the_repository));
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

	work_tree = repo_get_work_tree(the_repository);
	if (!work_tree || chdir_notify(work_tree))
		die(_("this operation must be run in a work tree"));

	/*
	 * Make sure subsequent git processes find correct worktree
	 * if $GIT_WORK_TREE is set relative
	 */
	if (getenv(GIT_WORK_TREE_ENVIRONMENT))
		setenv(GIT_WORK_TREE_ENVIRONMENT, ".", 1);

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
	 * For convenience, we would like to have the path relative to the
	 * worktree instead of an absolute path.
	 *
	 * Yes, startup_info->original_cwd is usually the same as 'prefix',
	 * but differs in two ways:
	 *   - prefix has a trailing '/'
	 *   - if the user passes '-C' to git, that modifies the prefix but
	 *     not startup_info->original_cwd.
	 */

	/* Normalize the directory */
	if (!strbuf_realpath(&tmp, tmp_original_cwd, 0)) {
		trace2_data_string("setup", the_repository,
				   "realpath-path", tmp_original_cwd);
		trace2_data_string("setup", the_repository,
				   "realpath-failure", strerror(errno));
		free((char*)tmp_original_cwd);
		tmp_original_cwd = NULL;
		return;
	}

	free((char*)tmp_original_cwd);
	tmp_original_cwd = NULL;
	startup_info->original_cwd = strbuf_detach(&tmp, NULL);

	/*
	 * Get our worktree; we only protect the current working directory
	 * if it's in the worktree.
	 */
	worktree = repo_get_work_tree(the_repository);
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

static int read_worktree_config(const char *var, const char *value,
				const struct config_context *ctx UNUSED,
				void *vdata)
{
	struct repository_format *data = vdata;

	if (strcmp(var, "core.bare") == 0) {
		data->is_bare = git_config_bool(var, value);
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
			data->precious_objects = git_config_bool(var, value);
			return EXTENSION_OK;
		} else if (!strcmp(ext, "partialclone")) {
			if (!value)
				return config_error_nonbool(var);
			data->partial_clone = xstrdup(value);
			return EXTENSION_OK;
		} else if (!strcmp(ext, "worktreeconfig")) {
			data->worktree_config = git_config_bool(var, value);
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
		if (format == GIT_HASH_UNKNOWN)
			return error(_("invalid value for '%s': '%s'"),
				     "extensions.objectformat", value);
		data->hash_algo = format;
		return EXTENSION_OK;
	} else if (!strcmp(ext, "compatobjectformat")) {
		struct string_list_item *item;
		int format;

		if (!value)
			return config_error_nonbool(var);
		format = hash_algo_by_name(value);
		if (format == GIT_HASH_UNKNOWN)
			return error(_("invalid value for '%s': '%s'"),
				     "extensions.compatobjectformat", value);
		/* For now only support compatObjectFormat being specified once. */
		for_each_string_list_item(item, &data->v1_only_extensions) {
			if (!strcmp(item->string, "compatobjectformat"))
				return error(_("'%s' already specified as '%s'"),
					"extensions.compatobjectformat",
					hash_algos[data->compat_hash_algo].name);
		}
		data->compat_hash_algo = format;
		return EXTENSION_OK;
	} else if (!strcmp(ext, "refstorage")) {
		unsigned int format;

		if (!value)
			return config_error_nonbool(var);
		format = ref_storage_format_by_name(value);
		if (format == REF_STORAGE_FORMAT_UNKNOWN)
			return error(_("invalid value for '%s': '%s'"),
				     "extensions.refstorage", value);
		data->ref_storage_format = format;
		return EXTENSION_OK;
	} else if (!strcmp(ext, "relativeworktrees")) {
		data->relative_worktrees = git_config_bool(var, value);
		return EXTENSION_OK;
	}
	return EXTENSION_UNKNOWN;
}

static int check_repo_format(const char *var, const char *value,
			     const struct config_context *ctx, void *vdata)
{
	struct repository_format *data = vdata;
	const char *ext;

	if (strcmp(var, "core.repositoryformatversion") == 0)
		data->version = git_config_int(var, value, ctx->kvi);
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

	return read_worktree_config(var, value, ctx, vdata);
}

static int check_repository_format_gently(const char *gitdir, struct repository_format *candidate, int *nongit_ok)
{
	struct strbuf sb = STRBUF_INIT;
	struct strbuf err = STRBUF_INIT;
	int has_common;

	has_common = get_common_dir(&sb, gitdir);
	strbuf_addstr(&sb, "/config");
	read_repository_format(candidate, sb.buf);
	strbuf_release(&sb);

	/*
	 * For historical use of check_repository_format() in git-init,
	 * we treat a missing config as a silent "ok", even when nongit_ok
	 * is unset.
	 */
	if (candidate->version < 0)
		return 0;

	if (verify_repository_format(candidate, &err) < 0) {
		if (nongit_ok) {
			warning("%s", err.buf);
			strbuf_release(&err);
			*nongit_ok = -1;
			return -1;
		}
		die("%s", err.buf);
	}

	repository_format_precious_objects = candidate->precious_objects;
	string_list_clear(&candidate->unknown_extensions, 0);
	string_list_clear(&candidate->v1_only_extensions, 0);

	if (candidate->worktree_config) {
		/*
		 * pick up core.bare and core.worktree from per-worktree
		 * config if present
		 */
		strbuf_addf(&sb, "%s/config.worktree", gitdir);
		git_config_from_file(read_worktree_config, sb.buf, candidate);
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
			free(git_work_tree_cfg);
			git_work_tree_cfg = xstrdup(candidate->work_tree);
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
	int ret;

	strbuf_git_common_path(&sb, the_repository, "config");
	read_repository_format(&repo_fmt, sb.buf);
	strbuf_release(&sb);

	if (repo_fmt.version >= target_version) {
		ret = 0;
		goto out;
	}

	if (verify_repository_format(&repo_fmt, &err) < 0) {
		ret = error("cannot upgrade repository format from %d to %d: %s",
			    repo_fmt.version, target_version, err.buf);
		goto out;
	}
	if (!repo_fmt.version && repo_fmt.unknown_extensions.nr) {
		ret = error("cannot upgrade repository format: "
			    "unknown extension %s",
			    repo_fmt.unknown_extensions.items[0].string);
		goto out;
	}

	strbuf_addf(&repo_version, "%d", target_version);
	git_config_set("core.repositoryformatversion", repo_version.buf);

	ret = 1;

out:
	clear_repository_format(&repo_fmt);
	strbuf_release(&repo_version);
	strbuf_release(&err);
	return ret;
}

static void init_repository_format(struct repository_format *format)
{
	const struct repository_format fresh = REPOSITORY_FORMAT_INIT;

	memcpy(format, &fresh, sizeof(fresh));
}

int read_repository_format(struct repository_format *format, const char *path)
{
	clear_repository_format(format);
	git_config_from_file(check_repo_format, path, format);
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
	if (GIT_REPO_VERSION_READ < format->version) {
		strbuf_addf(err, _("Expected git repo version <= %d, found %d"),
			    GIT_REPO_VERSION_READ, format->version);
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

void read_gitfile_error_die(int error_code, const char *path, const char *dir)
{
	switch (error_code) {
	case READ_GITFILE_ERR_STAT_FAILED:
	case READ_GITFILE_ERR_NOT_A_FILE:
		/* non-fatal; follow return path */
		break;
	case READ_GITFILE_ERR_OPEN_FAILED:
		die_errno(_("error opening '%s'"), path);
	case READ_GITFILE_ERR_TOO_LARGE:
		die(_("too large to be a .git file: '%s'"), path);
	case READ_GITFILE_ERR_READ_FAILED:
		die(_("error reading %s"), path);
	case READ_GITFILE_ERR_INVALID_FORMAT:
		die(_("invalid gitfile format: %s"), path);
	case READ_GITFILE_ERR_NO_PATH:
		die(_("no path in gitfile: %s"), path);
	case READ_GITFILE_ERR_NOT_A_REPO:
		die(_("not a git repository: %s"), dir);
	default:
		BUG("unknown error code");
	}
}

/*
 * Try to read the location of the git directory from the .git file,
 * return path to git directory if found. The return value comes from
 * a shared buffer.
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
	static struct strbuf realpath = STRBUF_INIT;

	if (stat(path, &st)) {
		/* NEEDSWORK: discern between ENOENT vs other errors */
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
	buf = xmallocz(st.st_size);
	len = read_in_full(fd, buf, st.st_size);
	close(fd);
	if (len != st.st_size) {
		error_code = READ_GITFILE_ERR_READ_FAILED;
		goto cleanup_return;
	}
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
		dir = xstrfmt("%.*s%.*s", (int)pathlen, path,
			      (int)(len - 8), buf + 8);
		free(buf);
		buf = dir;
	}
	if (!is_git_directory(dir)) {
		error_code = READ_GITFILE_ERR_NOT_A_REPO;
		goto cleanup_return;
	}

	strbuf_realpath(&realpath, dir, 1);
	path = realpath.buf;

cleanup_return:
	if (return_error_code)
		*return_error_code = error_code;
	else if (error_code)
		read_gitfile_error_die(error_code, path, dir);

	free(buf);
	return error_code ? NULL : path;
}

static const char *setup_explicit_git_dir(const char *gitdirenv,
					  struct strbuf *cwd,
					  struct repository_format *repo_fmt,
					  int *nongit_ok)
{
	const char *work_tree_env = getenv(GIT_WORK_TREE_ENVIRONMENT);
	const char *worktree;
	char *gitfile;
	int offset;

	if (PATH_MAX - 40 < strlen(gitdirenv))
		die(_("'$%s' too big"), GIT_DIR_ENVIRONMENT);

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
		die(_("not a git repository: '%s'"), gitdirenv);
	}

	if (check_repository_format_gently(gitdirenv, repo_fmt, nongit_ok)) {
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
		set_git_dir(gitdirenv, 0);
		free(gitfile);
		return NULL;
	}
	else if (git_work_tree_cfg) { /* #6, #14 */
		if (is_absolute_path(git_work_tree_cfg))
			set_git_work_tree(git_work_tree_cfg);
		else {
			char *core_worktree;
			if (chdir(gitdirenv))
				die_errno(_("cannot chdir to '%s'"), gitdirenv);
			if (chdir(git_work_tree_cfg))
				die_errno(_("cannot chdir to '%s'"), git_work_tree_cfg);
			core_worktree = xgetcwd();
			if (chdir(cwd->buf))
				die_errno(_("cannot come back to cwd"));
			set_git_work_tree(core_worktree);
			free(core_worktree);
		}
	}
	else if (!git_env_bool(GIT_IMPLICIT_WORK_TREE_ENVIRONMENT, 1)) {
		/* #16d */
		set_git_dir(gitdirenv, 0);
		free(gitfile);
		return NULL;
	}
	else /* #2, #10 */
		set_git_work_tree(".");

	/* set_git_work_tree() must have been called by now */
	worktree = repo_get_work_tree(the_repository);

	/* both repo_get_work_tree() and cwd are already normalized */
	if (!strcmp(cwd->buf, worktree)) { /* cwd == worktree */
		set_git_dir(gitdirenv, 0);
		free(gitfile);
		return NULL;
	}

	offset = dir_inside_of(cwd->buf, worktree);
	if (offset >= 0) {	/* cwd inside worktree? */
		set_git_dir(gitdirenv, 1);
		if (chdir(worktree))
			die_errno(_("cannot chdir to '%s'"), worktree);
		strbuf_addch(cwd, '/');
		free(gitfile);
		return cwd->buf + offset;
	}

	/* cwd outside worktree */
	set_git_dir(gitdirenv, 0);
	free(gitfile);
	return NULL;
}

static const char *setup_discovered_git_dir(const char *gitdir,
					    struct strbuf *cwd, int offset,
					    struct repository_format *repo_fmt,
					    int *nongit_ok)
{
	if (check_repository_format_gently(gitdir, repo_fmt, nongit_ok))
		return NULL;

	/* --work-tree is set without --git-dir; use discovered one */
	if (getenv(GIT_WORK_TREE_ENVIRONMENT) || git_work_tree_cfg) {
		char *to_free = NULL;
		const char *ret;

		if (offset != cwd->len && !is_absolute_path(gitdir))
			gitdir = to_free = real_pathdup(gitdir, 1);
		if (chdir(cwd->buf))
			die_errno(_("cannot come back to cwd"));
		ret = setup_explicit_git_dir(gitdir, cwd, repo_fmt, nongit_ok);
		free(to_free);
		return ret;
	}

	/* #16.2, #17.2, #20.2, #21.2, #24, #25, #28, #29 (see t1510) */
	if (is_bare_repository_cfg > 0) {
		set_git_dir(gitdir, (offset != cwd->len));
		if (chdir(cwd->buf))
			die_errno(_("cannot come back to cwd"));
		return NULL;
	}

	/* #0, #1, #5, #8, #9, #12, #13 */
	set_git_work_tree(".");
	if (strcmp(gitdir, DEFAULT_GIT_DIR_ENVIRONMENT))
		set_git_dir(gitdir, 0);
	inside_git_dir = 0;
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
static const char *setup_bare_git_dir(struct strbuf *cwd, int offset,
				      struct repository_format *repo_fmt,
				      int *nongit_ok)
{
	int root_len;

	if (check_repository_format_gently(".", repo_fmt, nongit_ok))
		return NULL;

	setenv(GIT_IMPLICIT_WORK_TREE_ENVIRONMENT, "0", 1);

	/* --work-tree is set without --git-dir; use discovered one */
	if (getenv(GIT_WORK_TREE_ENVIRONMENT) || git_work_tree_cfg) {
		static const char *gitdir;

		gitdir = offset == cwd->len ? "." : xmemdupz(cwd->buf, offset);
		if (chdir(cwd->buf))
			die_errno(_("cannot come back to cwd"));
		return setup_explicit_git_dir(gitdir, cwd, repo_fmt, nongit_ok);
	}

	inside_git_dir = 1;
	inside_work_tree = 0;
	if (offset != cwd->len) {
		if (chdir(cwd->buf))
			die_errno(_("cannot come back to cwd"));
		root_len = offset_1st_component(cwd->buf);
		strbuf_setlen(cwd, offset > root_len ? offset : root_len);
		set_git_dir(cwd->buf, 0);
	}
	else
		set_git_dir(".", 0);
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
 * from GIT_CEILING_DIRECTORIES using real_pathdup(), or
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
	char *path;
	int is_safe;
};

static int safe_directory_cb(const char *key, const char *value,
			     const struct config_context *ctx UNUSED, void *d)
{
	struct safe_directory_data *data = d;

	if (strcmp(key, "safe.directory"))
		return 0;

	if (!value || !*value) {
		data->is_safe = 0;
	} else if (!strcmp(value, "*")) {
		data->is_safe = 1;
	} else {
		char *allowed = NULL;

		if (!git_config_pathname(&allowed, key, value)) {
			char *normalized = NULL;

			/*
			 * Setting safe.directory to a non-absolute path
			 * makes little sense---it won't be relative to
			 * the configuration file the item is defined in.
			 * Except for ".", which means "if we are at the top
			 * level of a repository, then it is OK", which is
			 * slightly tighter than "*" that allows discovery.
			 */
			if (!is_absolute_path(allowed) && strcmp(allowed, ".")) {
				warning(_("safe.directory '%s' not absolute"),
					allowed);
				goto next;
			}

			/*
			 * A .gitconfig in $HOME may be shared across
			 * different machines and safe.directory entries
			 * may or may not exist as paths on all of these
			 * machines.  In other words, it is not a warning
			 * worthy event when there is no such path on this
			 * machine---the entry may be useful elsewhere.
			 */
			normalized = real_pathdup(allowed, 0);
			if (!normalized)
				goto next;

			if (ends_with(normalized, "/*")) {
				size_t len = strlen(normalized);
				if (!fspathncmp(normalized, data->path, len - 1))
					data->is_safe = 1;
			} else if (!fspathcmp(data->path, normalized)) {
				data->is_safe = 1;
			}
		next:
			free(normalized);
			free(allowed);
		}
	}

	return 0;
}

/*
 * Check if a repository is safe, by verifying the ownership of the
 * worktree (if any), the git directory, and the gitfile (if any).
 *
 * Exemptions for known-safe repositories can be added via `safe.directory`
 * config settings; for non-bare repositories, their worktree needs to be
 * added, for bare ones their git directory.
 */
static int ensure_valid_ownership(const char *gitfile,
				  const char *worktree, const char *gitdir,
				  struct strbuf *report)
{
	struct safe_directory_data data = { 0 };

	if (!git_env_bool("GIT_TEST_ASSUME_DIFFERENT_OWNER", 0) &&
	    (!gitfile || is_path_owned_by_current_user(gitfile, report)) &&
	    (!worktree || is_path_owned_by_current_user(worktree, report)) &&
	    (!gitdir || is_path_owned_by_current_user(gitdir, report)))
		return 1;

	/*
	 * normalize the data.path for comparison with normalized paths
	 * that come from the configuration file.  The path is unsafe
	 * if it cannot be normalized.
	 */
	data.path = real_pathdup(worktree ? worktree : gitdir, 0);
	if (!data.path)
		return 0;

	/*
	 * data.path is the "path" that identifies the repository and it is
	 * constant regardless of what failed above. data.is_safe should be
	 * initialized to false, and might be changed by the callback.
	 */
	git_protected_config(safe_directory_cb, &data);

	free(data.path);
	return data.is_safe;
}

void die_upon_dubious_ownership(const char *gitfile, const char *worktree,
				const char *gitdir)
{
	struct strbuf report = STRBUF_INIT, quoted = STRBUF_INIT;
	const char *path;

	if (ensure_valid_ownership(gitfile, worktree, gitdir, &report))
		return;

	strbuf_complete(&report, '\n');
	path = gitfile ? gitfile : gitdir;
	sq_quote_buf_pretty(&quoted, path);

	die(_("detected dubious ownership in repository at '%s'\n"
	      "%s"
	      "To add an exception for this directory, call:\n"
	      "\n"
	      "\tgit config --global --add safe.directory %s"),
	    path, report.buf, quoted.buf);
}

static int allowed_bare_repo_cb(const char *key, const char *value,
				const struct config_context *ctx UNUSED,
				void *d)
{
	enum allowed_bare_repo *allowed_bare_repo = d;

	if (strcasecmp(key, "safe.bareRepository"))
		return 0;

	if (!strcmp(value, "explicit")) {
		*allowed_bare_repo = ALLOWED_BARE_REPO_EXPLICIT;
		return 0;
	}
	if (!strcmp(value, "all")) {
		*allowed_bare_repo = ALLOWED_BARE_REPO_ALL;
		return 0;
	}
	return -1;
}

static enum allowed_bare_repo get_allowed_bare_repo(void)
{
	enum allowed_bare_repo result = ALLOWED_BARE_REPO_ALL;
	git_protected_config(allowed_bare_repo_cb, &result);
	return result;
}

static const char *allowed_bare_repo_to_string(
	enum allowed_bare_repo allowed_bare_repo)
{
	switch (allowed_bare_repo) {
	case ALLOWED_BARE_REPO_EXPLICIT:
		return "explicit";
	case ALLOWED_BARE_REPO_ALL:
		return "all";
	default:
		BUG("invalid allowed_bare_repo %d",
		    allowed_bare_repo);
	}
	return NULL;
}

static int is_implicit_bare_repo(const char *path)
{
	/*
	 * what we found is a ".git" directory at the root of
	 * the working tree.
	 */
	if (ends_with_path_components(path, ".git"))
		return 1;

	/*
	 * we are inside $GIT_DIR of a secondary worktree of a
	 * non-bare repository.
	 */
	if (strstr(path, "/.git/worktrees/"))
		return 1;

	/*
	 * we are inside $GIT_DIR of a worktree of a non-embedded
	 * submodule, whose superproject is not a bare repository.
	 */
	if (strstr(path, "/.git/modules/"))
		return 1;

	return 0;
}

/*
 * We cannot decide in this function whether we are in the work tree or
 * not, since the config can only be read _after_ this function was called.
 *
 * Also, we avoid changing any global state (such as the current working
 * directory) to allow early callers.
 *
 * The directory where the search should start needs to be passed in via the
 * `dir` parameter; upon return, the `dir` buffer will contain the path of
 * the directory where the search ended, and `gitdir` will contain the path of
 * the discovered .git/ directory, if any. If `gitdir` is not absolute, it
 * is relative to `dir` (i.e. *not* necessarily the cwd).
 */
static enum discovery_result setup_git_directory_gently_1(struct strbuf *dir,
							  struct strbuf *gitdir,
							  struct strbuf *report,
							  int die_on_error)
{
	const char *env_ceiling_dirs = getenv(CEILING_DIRECTORIES_ENVIRONMENT);
	struct string_list ceiling_dirs = STRING_LIST_INIT_DUP;
	const char *gitdirenv;
	int ceil_offset = -1, min_offset = offset_1st_component(dir->buf);
	dev_t current_device = 0;
	int one_filesystem = 1;

	/*
	 * If GIT_DIR is set explicitly, we're not going
	 * to do any discovery, but we still do repository
	 * validation.
	 */
	gitdirenv = getenv(GIT_DIR_ENVIRONMENT);
	if (gitdirenv) {
		strbuf_addstr(gitdir, gitdirenv);
		return GIT_DIR_EXPLICIT;
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
	 * - .git (file containing "gitdir: <path>")
	 * - .git/
	 * - ./ (bare)
	 * - ../.git
	 * - ../.git/
	 * - ../ (bare)
	 * - ../../.git
	 *   etc.
	 */
	one_filesystem = !git_env_bool("GIT_DISCOVERY_ACROSS_FILESYSTEM", 0);
	if (one_filesystem)
		current_device = get_device_or_die(dir->buf, NULL, 0);
	for (;;) {
		int offset = dir->len, error_code = 0;
		char *gitdir_path = NULL;
		char *gitfile = NULL;

		if (offset > min_offset)
			strbuf_addch(dir, '/');
		strbuf_addstr(dir, DEFAULT_GIT_DIR_ENVIRONMENT);
		gitdirenv = read_gitfile_gently(dir->buf, die_on_error ?
						NULL : &error_code);
		if (!gitdirenv) {
			if (die_on_error ||
			    error_code == READ_GITFILE_ERR_NOT_A_FILE) {
				/* NEEDSWORK: fail if .git is not file nor dir */
				if (is_git_directory(dir->buf)) {
					gitdirenv = DEFAULT_GIT_DIR_ENVIRONMENT;
					gitdir_path = xstrdup(dir->buf);
				}
			} else if (error_code != READ_GITFILE_ERR_STAT_FAILED)
				return GIT_DIR_INVALID_GITFILE;
		} else
			gitfile = xstrdup(dir->buf);
		/*
		 * Earlier, we tentatively added DEFAULT_GIT_DIR_ENVIRONMENT
		 * to check that directory for a repository.
		 * Now trim that tentative addition away, because we want to
		 * focus on the real directory we are in.
		 */
		strbuf_setlen(dir, offset);
		if (gitdirenv) {
			enum discovery_result ret;
			const char *gitdir_candidate =
				gitdir_path ? gitdir_path : gitdirenv;

			if (ensure_valid_ownership(gitfile, dir->buf,
						   gitdir_candidate, report)) {
				strbuf_addstr(gitdir, gitdirenv);
				ret = GIT_DIR_DISCOVERED;
			} else
				ret = GIT_DIR_INVALID_OWNERSHIP;

			/*
			 * Earlier, during discovery, we might have allocated
			 * string copies for gitdir_path or gitfile so make
			 * sure we don't leak by freeing them now, before
			 * leaving the loop and function.
			 *
			 * Note: gitdirenv will be non-NULL whenever these are
			 * allocated, therefore we need not take care of releasing
			 * them outside of this conditional block.
			 */
			free(gitdir_path);
			free(gitfile);

			return ret;
		}

		if (is_git_directory(dir->buf)) {
			trace2_data_string("setup", NULL, "implicit-bare-repository", dir->buf);
			if (get_allowed_bare_repo() == ALLOWED_BARE_REPO_EXPLICIT &&
			    !is_implicit_bare_repo(dir->buf))
				return GIT_DIR_DISALLOWED_BARE;
			if (!ensure_valid_ownership(NULL, NULL, dir->buf, report))
				return GIT_DIR_INVALID_OWNERSHIP;
			strbuf_addstr(gitdir, ".");
			return GIT_DIR_BARE;
		}

		if (offset <= min_offset)
			return GIT_DIR_HIT_CEILING;

		while (--offset > ceil_offset && !is_dir_sep(dir->buf[offset]))
			; /* continue */
		if (offset <= ceil_offset)
			return GIT_DIR_HIT_CEILING;

		strbuf_setlen(dir, offset > min_offset ?  offset : min_offset);
		if (one_filesystem &&
		    current_device != get_device_or_die(dir->buf, NULL, offset))
			return GIT_DIR_HIT_MOUNT_POINT;
	}
}

enum discovery_result discover_git_directory_reason(struct strbuf *commondir,
						    struct strbuf *gitdir)
{
	struct strbuf dir = STRBUF_INIT, err = STRBUF_INIT;
	size_t gitdir_offset = gitdir->len, cwd_len;
	size_t commondir_offset = commondir->len;
	struct repository_format candidate = REPOSITORY_FORMAT_INIT;
	enum discovery_result result;

	if (strbuf_getcwd(&dir))
		return GIT_DIR_CWD_FAILURE;

	cwd_len = dir.len;
	result = setup_git_directory_gently_1(&dir, gitdir, NULL, 0);
	if (result <= 0) {
		strbuf_release(&dir);
		return result;
	}

	/*
	 * The returned gitdir is relative to dir, and if dir does not reflect
	 * the current working directory, we simply make the gitdir absolute.
	 */
	if (dir.len < cwd_len && !is_absolute_path(gitdir->buf + gitdir_offset)) {
		/* Avoid a trailing "/." */
		if (!strcmp(".", gitdir->buf + gitdir_offset))
			strbuf_setlen(gitdir, gitdir_offset);
		else
			strbuf_addch(&dir, '/');
		strbuf_insert(gitdir, gitdir_offset, dir.buf, dir.len);
	}

	get_common_dir(commondir, gitdir->buf + gitdir_offset);

	strbuf_reset(&dir);
	strbuf_addf(&dir, "%s/config", commondir->buf + commondir_offset);
	read_repository_format(&candidate, dir.buf);
	strbuf_release(&dir);

	if (verify_repository_format(&candidate, &err) < 0) {
		warning("ignoring git dir '%s': %s",
			gitdir->buf + gitdir_offset, err.buf);
		strbuf_release(&err);
		strbuf_setlen(commondir, commondir_offset);
		strbuf_setlen(gitdir, gitdir_offset);
		clear_repository_format(&candidate);
		return GIT_DIR_INVALID_FORMAT;
	}

	clear_repository_format(&candidate);
	return result;
}

void setup_git_env(const char *git_dir)
{
	char *git_replace_ref_base;
	const char *shallow_file;
	const char *replace_ref_base;
	struct set_gitdir_args args = { NULL };
	struct strvec to_free = STRVEC_INIT;

	args.commondir = getenv_safe(&to_free, GIT_COMMON_DIR_ENVIRONMENT);
	args.object_dir = getenv_safe(&to_free, DB_ENVIRONMENT);
	args.graft_file = getenv_safe(&to_free, GRAFT_ENVIRONMENT);
	args.index_file = getenv_safe(&to_free, INDEX_ENVIRONMENT);
	args.alternate_db = getenv_safe(&to_free, ALTERNATE_DB_ENVIRONMENT);
	if (getenv(GIT_QUARANTINE_ENVIRONMENT)) {
		args.disable_ref_updates = 1;
	}

	repo_set_gitdir(the_repository, git_dir, &args);
	strvec_clear(&to_free);

	if (getenv(NO_REPLACE_OBJECTS_ENVIRONMENT))
		disable_replace_refs();
	replace_ref_base = getenv(GIT_REPLACE_REF_BASE_ENVIRONMENT);
	git_replace_ref_base = xstrdup(replace_ref_base ? replace_ref_base
							  : "refs/replace/");
	update_ref_namespace(NAMESPACE_REPLACE, git_replace_ref_base);

	shallow_file = getenv(GIT_SHALLOW_FILE_ENVIRONMENT);
	if (shallow_file)
		set_alternate_shallow_file(the_repository, shallow_file, 0);

	if (git_env_bool(NO_LAZY_FETCH_ENVIRONMENT, 0))
		fetch_if_missing = 0;
}

static void set_git_dir_1(const char *path)
{
	xsetenv(GIT_DIR_ENVIRONMENT, path, 1);
	setup_git_env(path);
}

static void update_relative_gitdir(const char *name UNUSED,
				   const char *old_cwd,
				   const char *new_cwd,
				   void *data UNUSED)
{
	char *path = reparent_relative_path(old_cwd, new_cwd,
					    repo_get_git_dir(the_repository));
	struct tmp_objdir *tmp_objdir = tmp_objdir_unapply_primary_odb();

	trace_printf_key(&trace_setup_key,
			 "setup: move $GIT_DIR to '%s'",
			 path);
	set_git_dir_1(path);
	if (tmp_objdir)
		tmp_objdir_reapply_primary_odb(tmp_objdir, old_cwd, new_cwd);
	free(path);
}

void set_git_dir(const char *path, int make_realpath)
{
	struct strbuf realpath = STRBUF_INIT;

	if (make_realpath) {
		strbuf_realpath(&realpath, path, 1);
		path = realpath.buf;
	}

	set_git_dir_1(path);
	if (!is_absolute_path(path))
		chdir_notify_register(NULL, update_relative_gitdir, NULL);

	strbuf_release(&realpath);
}

static int git_work_tree_initialized;

/*
 * Note.  This works only before you used a work tree.  This was added
 * primarily to support git-clone to work in a new repository it just
 * created, and is not meant to flip between different work trees.
 */
void set_git_work_tree(const char *new_work_tree)
{
	if (git_work_tree_initialized) {
		struct strbuf realpath = STRBUF_INIT;

		strbuf_realpath(&realpath, new_work_tree, 1);
		new_work_tree = realpath.buf;
		if (strcmp(new_work_tree, the_repository->worktree))
			die("internal error: work tree has already been set\n"
			    "Current worktree: %s\nNew worktree: %s",
			    the_repository->worktree, new_work_tree);
		strbuf_release(&realpath);
		return;
	}
	git_work_tree_initialized = 1;
	repo_set_worktree(the_repository, new_work_tree);
}

const char *setup_git_directory_gently(int *nongit_ok)
{
	static struct strbuf cwd = STRBUF_INIT;
	struct strbuf dir = STRBUF_INIT, gitdir = STRBUF_INIT, report = STRBUF_INIT;
	const char *prefix = NULL;
	struct repository_format repo_fmt = REPOSITORY_FORMAT_INIT;

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
		die_errno(_("Unable to read current working directory"));
	strbuf_addbuf(&dir, &cwd);

	switch (setup_git_directory_gently_1(&dir, &gitdir, &report, 1)) {
	case GIT_DIR_EXPLICIT:
		prefix = setup_explicit_git_dir(gitdir.buf, &cwd, &repo_fmt, nongit_ok);
		break;
	case GIT_DIR_DISCOVERED:
		if (dir.len < cwd.len && chdir(dir.buf))
			die(_("cannot change to '%s'"), dir.buf);
		prefix = setup_discovered_git_dir(gitdir.buf, &cwd, dir.len,
						  &repo_fmt, nongit_ok);
		break;
	case GIT_DIR_BARE:
		if (dir.len < cwd.len && chdir(dir.buf))
			die(_("cannot change to '%s'"), dir.buf);
		prefix = setup_bare_git_dir(&cwd, dir.len, &repo_fmt, nongit_ok);
		break;
	case GIT_DIR_HIT_CEILING:
		if (!nongit_ok)
			die(_("not a git repository (or any of the parent directories): %s"),
			    DEFAULT_GIT_DIR_ENVIRONMENT);
		*nongit_ok = 1;
		break;
	case GIT_DIR_HIT_MOUNT_POINT:
		if (!nongit_ok)
			die(_("not a git repository (or any parent up to mount point %s)\n"
			      "Stopping at filesystem boundary (GIT_DISCOVERY_ACROSS_FILESYSTEM not set)."),
			    dir.buf);
		*nongit_ok = 1;
		break;
	case GIT_DIR_INVALID_OWNERSHIP:
		if (!nongit_ok) {
			struct strbuf quoted = STRBUF_INIT;

			strbuf_complete(&report, '\n');
			sq_quote_buf_pretty(&quoted, dir.buf);
			die(_("detected dubious ownership in repository at '%s'\n"
			      "%s"
			      "To add an exception for this directory, call:\n"
			      "\n"
			      "\tgit config --global --add safe.directory %s"),
			    dir.buf, report.buf, quoted.buf);
		}
		*nongit_ok = 1;
		break;
	case GIT_DIR_DISALLOWED_BARE:
		if (!nongit_ok) {
			die(_("cannot use bare repository '%s' (safe.bareRepository is '%s')"),
			    dir.buf,
			    allowed_bare_repo_to_string(get_allowed_bare_repo()));
		}
		*nongit_ok = 1;
		break;
	case GIT_DIR_CWD_FAILURE:
	case GIT_DIR_INVALID_FORMAT:
		/*
		 * As a safeguard against setup_git_directory_gently_1 returning
		 * these values, fallthrough to BUG. Otherwise it is possible to
		 * set startup_info->have_repository to 1 when we did nothing to
		 * find a repository.
		 */
	default:
		BUG("unhandled setup_git_directory_gently_1() result");
	}

	/*
	 * At this point, nongit_ok is stable. If it is non-NULL and points
	 * to a non-zero value, then this means that we haven't found a
	 * repository and that the caller expects startup_info to reflect
	 * this.
	 *
	 * Regardless of the state of nongit_ok, startup_info->prefix and
	 * the GIT_PREFIX environment variable must always match. For details
	 * see Documentation/config/alias.txt.
	 */
	if (nongit_ok && *nongit_ok)
		startup_info->have_repository = 0;
	else
		startup_info->have_repository = 1;

	/*
	 * Not all paths through the setup code will call 'set_git_dir()' (which
	 * directly sets up the environment) so in order to guarantee that the
	 * environment is in a consistent state after setup, explicitly setup
	 * the environment if we have a repository.
	 *
	 * NEEDSWORK: currently we allow bogus GIT_DIR values to be set in some
	 * code paths so we also need to explicitly setup the environment if
	 * the user has set GIT_DIR.  It may be beneficial to disallow bogus
	 * GIT_DIR values at some point in the future.
	 */
	if (/* GIT_DIR_EXPLICIT, GIT_DIR_DISCOVERED, GIT_DIR_BARE */
	    startup_info->have_repository ||
	    /* GIT_DIR_EXPLICIT */
	    getenv(GIT_DIR_ENVIRONMENT)) {
		if (!the_repository->gitdir) {
			const char *gitdir = getenv(GIT_DIR_ENVIRONMENT);
			if (!gitdir)
				gitdir = DEFAULT_GIT_DIR_ENVIRONMENT;
			setup_git_env(gitdir);
		}
		if (startup_info->have_repository) {
			repo_set_hash_algo(the_repository, repo_fmt.hash_algo);
			repo_set_compat_hash_algo(the_repository,
						  repo_fmt.compat_hash_algo);
			repo_set_ref_storage_format(the_repository,
						    repo_fmt.ref_storage_format);
			the_repository->repository_format_worktree_config =
				repo_fmt.worktree_config;
			the_repository->repository_format_relative_worktrees =
				repo_fmt.relative_worktrees;
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
	 * for calling git_config_get_bool().
	 */
	if (prefix) {
		prefix = precompose_string_if_needed(prefix);
		startup_info->prefix = prefix;
		setenv(GIT_PREFIX_ENVIRONMENT, prefix, 1);
	} else {
		startup_info->prefix = NULL;
		setenv(GIT_PREFIX_ENVIRONMENT, "", 1);
	}

	setup_original_cwd();

	strbuf_release(&dir);
	strbuf_release(&gitdir);
	strbuf_release(&report);
	clear_repository_format(&repo_fmt);

	return prefix;
}

int git_config_perm(const char *var, const char *value)
{
	int i;
	char *endptr;

	if (!value)
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
	check_repository_format_gently(repo_get_git_dir(the_repository), fmt, NULL);
	startup_info->have_repository = 1;
	repo_set_hash_algo(the_repository, fmt->hash_algo);
	repo_set_compat_hash_algo(the_repository, fmt->compat_hash_algo);
	repo_set_ref_storage_format(the_repository,
				    fmt->ref_storage_format);
	the_repository->repository_format_worktree_config =
		fmt->worktree_config;
	the_repository->repository_format_relative_worktrees =
		fmt->relative_worktrees;
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
const char *setup_git_directory(void)
{
	return setup_git_directory_gently(NULL);
}

const char *resolve_gitdir_gently(const char *suspect, int *return_error_code)
{
	if (is_git_directory(suspect))
		return suspect;
	return read_gitfile_gently(suspect, return_error_code);
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

struct template_dir_cb_data {
	char *path;
	int initialized;
};

static int template_dir_cb(const char *key, const char *value,
			   const struct config_context *ctx UNUSED, void *d)
{
	struct template_dir_cb_data *data = d;

	if (strcmp(key, "init.templatedir"))
		return 0;

	if (!value) {
		data->path = NULL;
	} else {
		char *path = NULL;

		FREE_AND_NULL(data->path);
		if (!git_config_pathname(&path, key, value))
			data->path = path ? path : xstrdup(value);
	}

	return 0;
}

const char *get_template_dir(const char *option_template)
{
	const char *template_dir = option_template;

	if (!template_dir)
		template_dir = getenv(TEMPLATE_DIR_ENVIRONMENT);
	if (!template_dir) {
		static struct template_dir_cb_data data;

		if (!data.initialized) {
			git_protected_config(template_dir_cb, &data);
			data.initialized = 1;
		}
		template_dir = data.path;
	}
	if (!template_dir) {
		static char *dir;

		if (!dir)
			dir = system_path(DEFAULT_GIT_TEMPLATE_DIR);
		template_dir = dir;
	}
	return template_dir;
}

#ifdef NO_TRUSTABLE_FILEMODE
#define TEST_FILEMODE 0
#else
#define TEST_FILEMODE 1
#endif

#define GIT_DEFAULT_HASH_ENVIRONMENT "GIT_DEFAULT_HASH"

static void copy_templates_1(struct strbuf *path, struct strbuf *template_path,
			     DIR *dir)
{
	size_t path_baselen = path->len;
	size_t template_baselen = template_path->len;
	struct dirent *de;

	/* Note: if ".git/hooks" file exists in the repository being
	 * re-initialized, /etc/core-git/templates/hooks/update would
	 * cause "git init" to fail here.  I think this is sane but
	 * it means that the set of templates we ship by default, along
	 * with the way the namespace under .git/ is organized, should
	 * be really carefully chosen.
	 */
	safe_create_dir(path->buf, 1);
	while ((de = readdir(dir)) != NULL) {
		struct stat st_git, st_template;
		int exists = 0;

		strbuf_setlen(path, path_baselen);
		strbuf_setlen(template_path, template_baselen);

		if (de->d_name[0] == '.')
			continue;
		strbuf_addstr(path, de->d_name);
		strbuf_addstr(template_path, de->d_name);
		if (lstat(path->buf, &st_git)) {
			if (errno != ENOENT)
				die_errno(_("cannot stat '%s'"), path->buf);
		}
		else
			exists = 1;

		if (lstat(template_path->buf, &st_template))
			die_errno(_("cannot stat template '%s'"), template_path->buf);

		if (S_ISDIR(st_template.st_mode)) {
			DIR *subdir = opendir(template_path->buf);
			if (!subdir)
				die_errno(_("cannot opendir '%s'"), template_path->buf);
			strbuf_addch(path, '/');
			strbuf_addch(template_path, '/');
			copy_templates_1(path, template_path, subdir);
			closedir(subdir);
		}
		else if (exists)
			continue;
		else if (S_ISLNK(st_template.st_mode)) {
			struct strbuf lnk = STRBUF_INIT;
			if (strbuf_readlink(&lnk, template_path->buf,
					    st_template.st_size) < 0)
				die_errno(_("cannot readlink '%s'"), template_path->buf);
			if (symlink(lnk.buf, path->buf))
				die_errno(_("cannot symlink '%s' '%s'"),
					  lnk.buf, path->buf);
			strbuf_release(&lnk);
		}
		else if (S_ISREG(st_template.st_mode)) {
			if (copy_file(path->buf, template_path->buf, st_template.st_mode))
				die_errno(_("cannot copy '%s' to '%s'"),
					  template_path->buf, path->buf);
		}
		else
			error(_("ignoring template %s"), template_path->buf);
	}
}

static void copy_templates(const char *option_template)
{
	const char *template_dir = get_template_dir(option_template);
	struct strbuf path = STRBUF_INIT;
	struct strbuf template_path = STRBUF_INIT;
	size_t template_len;
	struct repository_format template_format = REPOSITORY_FORMAT_INIT;
	struct strbuf err = STRBUF_INIT;
	DIR *dir;
	char *to_free = NULL;

	if (!template_dir || !*template_dir)
		return;

	strbuf_addstr(&template_path, template_dir);
	strbuf_complete(&template_path, '/');
	template_len = template_path.len;

	dir = opendir(template_path.buf);
	if (!dir) {
		warning(_("templates not found in %s"), template_dir);
		goto free_return;
	}

	/* Make sure that template is from the correct vintage */
	strbuf_addstr(&template_path, "config");
	read_repository_format(&template_format, template_path.buf);
	strbuf_setlen(&template_path, template_len);

	/*
	 * No mention of version at all is OK, but anything else should be
	 * verified.
	 */
	if (template_format.version >= 0 &&
	    verify_repository_format(&template_format, &err) < 0) {
		warning(_("not copying templates from '%s': %s"),
			  template_dir, err.buf);
		strbuf_release(&err);
		goto close_free_return;
	}

	strbuf_addstr(&path, repo_get_common_dir(the_repository));
	strbuf_complete(&path, '/');
	copy_templates_1(&path, &template_path, dir);
close_free_return:
	closedir(dir);
free_return:
	free(to_free);
	strbuf_release(&path);
	strbuf_release(&template_path);
	clear_repository_format(&template_format);
}

/*
 * If the git_dir is not directly inside the working tree, then git will not
 * find it by default, and we need to set the worktree explicitly.
 */
static int needs_work_tree_config(const char *git_dir, const char *work_tree)
{
	if (!strcmp(work_tree, "/") && !strcmp(git_dir, "/.git"))
		return 0;
	if (skip_prefix(git_dir, work_tree, &git_dir) &&
	    !strcmp(git_dir, "/.git"))
		return 0;
	return 1;
}

void initialize_repository_version(int hash_algo,
				   enum ref_storage_format ref_storage_format,
				   int reinit)
{
	struct strbuf repo_version = STRBUF_INIT;
	int target_version = GIT_REPO_VERSION;

	/*
	 * Note that we initialize the repository version to 1 when the ref
	 * storage format is unknown. This is on purpose so that we can add the
	 * correct object format to the config during git-clone(1). The format
	 * version will get adjusted by git-clone(1) once it has learned about
	 * the remote repository's format.
	 */
	if (hash_algo != GIT_HASH_SHA1 ||
	    ref_storage_format != REF_STORAGE_FORMAT_FILES)
		target_version = GIT_REPO_VERSION_READ;

	if (hash_algo != GIT_HASH_SHA1 && hash_algo != GIT_HASH_UNKNOWN)
		git_config_set("extensions.objectformat",
			       hash_algos[hash_algo].name);
	else if (reinit)
		git_config_set_gently("extensions.objectformat", NULL);

	if (ref_storage_format != REF_STORAGE_FORMAT_FILES)
		git_config_set("extensions.refstorage",
			       ref_storage_format_to_name(ref_storage_format));
	else if (reinit)
		git_config_set_gently("extensions.refstorage", NULL);

	if (reinit) {
		struct strbuf config = STRBUF_INIT;
		struct repository_format repo_fmt = REPOSITORY_FORMAT_INIT;

		strbuf_git_common_path(&config, the_repository, "config");
		read_repository_format(&repo_fmt, config.buf);

		if (repo_fmt.v1_only_extensions.nr)
			target_version = GIT_REPO_VERSION_READ;

		strbuf_release(&config);
		clear_repository_format(&repo_fmt);
	}

	strbuf_addf(&repo_version, "%d", target_version);
	git_config_set("core.repositoryformatversion", repo_version.buf);

	strbuf_release(&repo_version);
}

static int is_reinit(void)
{
	struct strbuf buf = STRBUF_INIT;
	char junk[2];
	int ret;

	git_path_buf(&buf, "HEAD");
	ret = !access(buf.buf, R_OK) || readlink(buf.buf, junk, sizeof(junk) - 1) != -1;
	strbuf_release(&buf);
	return ret;
}

void create_reference_database(enum ref_storage_format ref_storage_format,
			       const char *initial_branch, int quiet)
{
	struct strbuf err = STRBUF_INIT;
	char *to_free = NULL;
	int reinit = is_reinit();

	repo_set_ref_storage_format(the_repository, ref_storage_format);
	if (ref_store_create_on_disk(get_main_ref_store(the_repository), 0, &err))
		die("failed to set up refs db: %s", err.buf);

	/*
	 * Point the HEAD symref to the initial branch with if HEAD does
	 * not yet exist.
	 */
	if (!reinit) {
		char *ref;

		if (!initial_branch)
			initial_branch = to_free =
				repo_default_branch_name(the_repository, quiet);

		ref = xstrfmt("refs/heads/%s", initial_branch);
		if (check_refname_format(ref, 0) < 0)
			die(_("invalid initial branch name: '%s'"),
			    initial_branch);

		if (refs_update_symref(get_main_ref_store(the_repository), "HEAD", ref, NULL) < 0)
			exit(1);
		free(ref);
	}

	if (reinit && initial_branch)
		warning(_("re-init: ignored --initial-branch=%s"),
			initial_branch);

	strbuf_release(&err);
	free(to_free);
}

static int create_default_files(const char *template_path,
				const char *original_git_dir,
				const struct repository_format *fmt,
				int init_shared_repository)
{
	struct stat st1;
	struct strbuf buf = STRBUF_INIT;
	char *path;
	int reinit;
	int filemode;
	const char *work_tree = repo_get_work_tree(the_repository);

	/*
	 * First copy the templates -- we might have the default
	 * config file there, in which case we would want to read
	 * from it after installing.
	 *
	 * Before reading that config, we also need to clear out any cached
	 * values (since we've just potentially changed what's available on
	 * disk).
	 */
	copy_templates(template_path);
	git_config_clear();
	reset_shared_repository();
	git_config(git_default_config, NULL);

	reinit = is_reinit();

	/*
	 * We must make sure command-line options continue to override any
	 * values we might have just re-read from the config.
	 */
	if (init_shared_repository != -1)
		set_shared_repository(init_shared_repository);

	is_bare_repository_cfg = !work_tree;

	/*
	 * We would have created the above under user's umask -- under
	 * shared-repository settings, we would need to fix them up.
	 */
	if (get_shared_repository()) {
		adjust_shared_perm(repo_get_git_dir(the_repository));
	}

	initialize_repository_version(fmt->hash_algo, fmt->ref_storage_format, reinit);

	/* Check filemode trustability */
	path = git_path_buf(&buf, "config");
	filemode = TEST_FILEMODE;
	if (TEST_FILEMODE && !lstat(path, &st1)) {
		struct stat st2;
		filemode = (!chmod(path, st1.st_mode ^ S_IXUSR) &&
				!lstat(path, &st2) &&
				st1.st_mode != st2.st_mode &&
				!chmod(path, st1.st_mode));
		if (filemode && !reinit && (st1.st_mode & S_IXUSR))
			filemode = 0;
	}
	git_config_set("core.filemode", filemode ? "true" : "false");

	if (is_bare_repository())
		git_config_set("core.bare", "true");
	else {
		git_config_set("core.bare", "false");
		/* allow template config file to override the default */
		if (repo_settings_get_log_all_ref_updates(the_repository) == LOG_REFS_UNSET)
			git_config_set("core.logallrefupdates", "true");
		if (needs_work_tree_config(original_git_dir, work_tree))
			git_config_set("core.worktree", work_tree);
	}

	if (!reinit) {
		/* Check if symlink is supported in the work tree */
		path = git_path_buf(&buf, "tXXXXXX");
		if (!close(xmkstemp(path)) &&
		    !unlink(path) &&
		    !symlink("testing", path) &&
		    !lstat(path, &st1) &&
		    S_ISLNK(st1.st_mode))
			unlink(path); /* good */
		else
			git_config_set("core.symlinks", "false");

		/* Check if the filesystem is case-insensitive */
		path = git_path_buf(&buf, "CoNfIg");
		if (!access(path, F_OK))
			git_config_set("core.ignorecase", "true");
		probe_utf8_pathname_composition();
	}

	strbuf_release(&buf);
	return reinit;
}

static void create_object_directory(void)
{
	struct strbuf path = STRBUF_INIT;
	size_t baselen;

	strbuf_addstr(&path, repo_get_object_directory(the_repository));
	baselen = path.len;

	safe_create_dir(path.buf, 1);

	strbuf_setlen(&path, baselen);
	strbuf_addstr(&path, "/pack");
	safe_create_dir(path.buf, 1);

	strbuf_setlen(&path, baselen);
	strbuf_addstr(&path, "/info");
	safe_create_dir(path.buf, 1);

	strbuf_release(&path);
}

static void separate_git_dir(const char *git_dir, const char *git_link)
{
	struct stat st;

	if (!stat(git_link, &st)) {
		const char *src;

		if (S_ISREG(st.st_mode))
			src = read_gitfile(git_link);
		else if (S_ISDIR(st.st_mode))
			src = git_link;
		else
			die(_("unable to handle file type %d"), (int)st.st_mode);

		if (rename(src, git_dir))
			die_errno(_("unable to move %s to %s"), src, git_dir);
		repair_worktrees_after_gitdir_move(src);
	}

	write_file(git_link, "gitdir: %s", git_dir);
}

struct default_format_config {
	int hash;
	enum ref_storage_format ref_format;
};

static int read_default_format_config(const char *key, const char *value,
				      const struct config_context *ctx UNUSED,
				      void *payload)
{
	struct default_format_config *cfg = payload;
	char *str = NULL;
	int ret;

	if (!strcmp(key, "init.defaultobjectformat")) {
		ret = git_config_string(&str, key, value);
		if (ret)
			goto out;
		cfg->hash = hash_algo_by_name(str);
		if (cfg->hash == GIT_HASH_UNKNOWN)
			warning(_("unknown hash algorithm '%s'"), str);
		goto out;
	}

	if (!strcmp(key, "init.defaultrefformat")) {
		ret = git_config_string(&str, key, value);
		if (ret)
			goto out;
		cfg->ref_format = ref_storage_format_by_name(str);
		if (cfg->ref_format == REF_STORAGE_FORMAT_UNKNOWN)
			warning(_("unknown ref storage format '%s'"), str);
		goto out;
	}

	ret = 0;
out:
	free(str);
	return ret;
}

static void repository_format_configure(struct repository_format *repo_fmt,
					int hash, enum ref_storage_format ref_format)
{
	struct default_format_config cfg = {
		.hash = GIT_HASH_UNKNOWN,
		.ref_format = REF_STORAGE_FORMAT_UNKNOWN,
	};
	struct config_options opts = {
		.respect_includes = 1,
		.ignore_repo = 1,
		.ignore_worktree = 1,
	};
	const char *env;

	config_with_options(read_default_format_config, &cfg, NULL, NULL, &opts);

	/*
	 * If we already have an initialized repo, don't allow the user to
	 * specify a different algorithm, as that could cause corruption.
	 * Otherwise, if the user has specified one on the command line, use it.
	 */
	env = getenv(GIT_DEFAULT_HASH_ENVIRONMENT);
	if (repo_fmt->version >= 0 && hash != GIT_HASH_UNKNOWN && hash != repo_fmt->hash_algo)
		die(_("attempt to reinitialize repository with different hash"));
	else if (hash != GIT_HASH_UNKNOWN)
		repo_fmt->hash_algo = hash;
	else if (env) {
		int env_algo = hash_algo_by_name(env);
		if (env_algo == GIT_HASH_UNKNOWN)
			die(_("unknown hash algorithm '%s'"), env);
		repo_fmt->hash_algo = env_algo;
	} else if (cfg.hash != GIT_HASH_UNKNOWN) {
		repo_fmt->hash_algo = cfg.hash;
	}
	repo_set_hash_algo(the_repository, repo_fmt->hash_algo);

	env = getenv("GIT_DEFAULT_REF_FORMAT");
	if (repo_fmt->version >= 0 &&
	    ref_format != REF_STORAGE_FORMAT_UNKNOWN &&
	    ref_format != repo_fmt->ref_storage_format) {
		die(_("attempt to reinitialize repository with different reference storage format"));
	} else if (ref_format != REF_STORAGE_FORMAT_UNKNOWN) {
		repo_fmt->ref_storage_format = ref_format;
	} else if (env) {
		ref_format = ref_storage_format_by_name(env);
		if (ref_format == REF_STORAGE_FORMAT_UNKNOWN)
			die(_("unknown ref storage format '%s'"), env);
		repo_fmt->ref_storage_format = ref_format;
	} else if (cfg.ref_format != REF_STORAGE_FORMAT_UNKNOWN) {
		repo_fmt->ref_storage_format = cfg.ref_format;
	}
	repo_set_ref_storage_format(the_repository, repo_fmt->ref_storage_format);
}

int init_db(const char *git_dir, const char *real_git_dir,
	    const char *template_dir, int hash,
	    enum ref_storage_format ref_storage_format,
	    const char *initial_branch,
	    int init_shared_repository, unsigned int flags)
{
	int reinit;
	int exist_ok = flags & INIT_DB_EXIST_OK;
	char *original_git_dir = real_pathdup(git_dir, 1);
	struct repository_format repo_fmt = REPOSITORY_FORMAT_INIT;

	if (real_git_dir) {
		struct stat st;

		if (!exist_ok && !stat(git_dir, &st))
			die(_("%s already exists"), git_dir);

		if (!exist_ok && !stat(real_git_dir, &st))
			die(_("%s already exists"), real_git_dir);

		set_git_dir(real_git_dir, 1);
		git_dir = repo_get_git_dir(the_repository);
		separate_git_dir(git_dir, original_git_dir);
	}
	else {
		set_git_dir(git_dir, 1);
		git_dir = repo_get_git_dir(the_repository);
	}
	startup_info->have_repository = 1;

	/*
	 * Check to see if the repository version is right.
	 * Note that a newly created repository does not have
	 * config file, so this will not fail.  What we are catching
	 * is an attempt to reinitialize new repository with an old tool.
	 */
	check_repository_format(&repo_fmt);

	repository_format_configure(&repo_fmt, hash, ref_storage_format);

	/*
	 * Ensure `core.hidedotfiles` is processed. This must happen after we
	 * have set up the repository format such that we can evaluate
	 * includeIf conditions correctly in the case of re-initialization.
	 */
	git_config(platform_core_config, NULL);

	safe_create_dir(git_dir, 0);

	reinit = create_default_files(template_dir, original_git_dir,
				      &repo_fmt, init_shared_repository);

	if (!(flags & INIT_DB_SKIP_REFDB))
		create_reference_database(repo_fmt.ref_storage_format,
					  initial_branch, flags & INIT_DB_QUIET);
	create_object_directory();

	if (get_shared_repository()) {
		char buf[10];
		/* We do not spell "group" and such, so that
		 * the configuration can be read by older version
		 * of git. Note, we use octal numbers for new share modes,
		 * and compatibility values for PERM_GROUP and
		 * PERM_EVERYBODY.
		 */
		if (get_shared_repository() < 0)
			/* force to the mode value */
			xsnprintf(buf, sizeof(buf), "0%o", -get_shared_repository());
		else if (get_shared_repository() == PERM_GROUP)
			xsnprintf(buf, sizeof(buf), "%d", OLD_PERM_GROUP);
		else if (get_shared_repository() == PERM_EVERYBODY)
			xsnprintf(buf, sizeof(buf), "%d", OLD_PERM_EVERYBODY);
		else
			BUG("invalid value for shared_repository");
		git_config_set("core.sharedrepository", buf);
		git_config_set("receive.denyNonFastforwards", "true");
	}

	if (!(flags & INIT_DB_QUIET)) {
		int len = strlen(git_dir);

		if (reinit)
			printf(get_shared_repository()
			       ? _("Reinitialized existing shared Git repository in %s%s\n")
			       : _("Reinitialized existing Git repository in %s%s\n"),
			       git_dir, len && git_dir[len-1] != '/' ? "/" : "");
		else
			printf(get_shared_repository()
			       ? _("Initialized empty shared Git repository in %s%s\n")
			       : _("Initialized empty Git repository in %s%s\n"),
			       git_dir, len && git_dir[len-1] != '/' ? "/" : "");
	}

	clear_repository_format(&repo_fmt);
	free(original_git_dir);
	return 0;
}
