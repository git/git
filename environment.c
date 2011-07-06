/*
 * We put all the git config variables in this same object
 * file, so that programs can link against the config parser
 * without having to link against all the rest of git.
 *
 * In particular, no need to bring in libz etc unless needed,
 * even if you might want to know where the git directory etc
 * are.
 */
#include "cache.h"

char git_default_email[MAX_GITNAME];
char git_default_name[MAX_GITNAME];
int user_ident_explicitly_given;
int trust_executable_bit = 1;
int trust_ctime = 1;
int has_symlinks = 1;
int ignore_case;
int assume_unchanged;
int prefer_symlink_refs;
int is_bare_repository_cfg = -1; /* unspecified */
int log_all_ref_updates = -1; /* unspecified */
int warn_ambiguous_refs = 1;
int repository_format_version;
const char *git_commit_encoding;
const char *git_log_output_encoding;
int shared_repository = PERM_UMASK;
const char *apply_default_whitespace;
int zlib_compression_level = Z_BEST_SPEED;
int core_compression_level;
int core_compression_seen;
int fsync_object_files;
size_t packed_git_window_size = DEFAULT_PACKED_GIT_WINDOW_SIZE;
size_t packed_git_limit = DEFAULT_PACKED_GIT_LIMIT;
size_t delta_base_cache_limit = 16 * 1024 * 1024;
const char *pager_program;
int pager_use_color = 1;
const char *editor_program;
const char *excludes_file;
int auto_crlf = 0;	/* 1: both ways, -1: only when adding git objects */
enum safe_crlf safe_crlf = SAFE_CRLF_WARN;
unsigned whitespace_rule_cfg = WS_DEFAULT_RULE;
enum branch_track git_branch_track = BRANCH_TRACK_REMOTE;
enum rebase_setup_type autorebase = AUTOREBASE_NEVER;

/* This is set by setup_git_dir_gently() and/or git_default_config() */
char *git_work_tree_cfg;
static char *work_tree;

static const char *git_dir;
static char *git_object_dir, *git_index_file, *git_refs_dir, *git_graft_file;

static void setup_git_env(void)
{
	git_dir = getenv(GIT_DIR_ENVIRONMENT);
	if (!git_dir)
		git_dir = read_gitfile_gently(DEFAULT_GIT_DIR_ENVIRONMENT);
	if (!git_dir)
		git_dir = DEFAULT_GIT_DIR_ENVIRONMENT;
	git_object_dir = getenv(DB_ENVIRONMENT);
	if (!git_object_dir) {
		git_object_dir = xmalloc(strlen(git_dir) + 9);
		sprintf(git_object_dir, "%s/objects", git_dir);
	}
	git_refs_dir = xmalloc(strlen(git_dir) + 6);
	sprintf(git_refs_dir, "%s/refs", git_dir);
	git_index_file = getenv(INDEX_ENVIRONMENT);
	if (!git_index_file) {
		git_index_file = xmalloc(strlen(git_dir) + 7);
		sprintf(git_index_file, "%s/index", git_dir);
	}
	git_graft_file = getenv(GRAFT_ENVIRONMENT);
	if (!git_graft_file)
		git_graft_file = xstrdup(git_path("info/grafts"));
}

int is_bare_repository(void)
{
	/* if core.bare is not 'false', let's see if there is a work tree */
	return is_bare_repository_cfg && !get_git_work_tree();
}

const char *get_git_dir(void)
{
	if (!git_dir)
		setup_git_env();
	return git_dir;
}

static int git_work_tree_initialized;

/*
 * Note.  This works only before you used a work tree.  This was added
 * primarily to support git-clone to work in a new repository it just
 * created, and is not meant to flip between different work trees.
 */
void set_git_work_tree(const char *new_work_tree)
{
	if (is_bare_repository_cfg >= 0)
		die("cannot set work tree after initialization");
	git_work_tree_initialized = 1;
	free(work_tree);
	work_tree = xstrdup(make_absolute_path(new_work_tree));
	is_bare_repository_cfg = 0;
}

const char *get_git_work_tree(void)
{
	if (!git_work_tree_initialized) {
		work_tree = getenv(GIT_WORK_TREE_ENVIRONMENT);
		/* core.bare = true overrides implicit and config work tree */
		if (!work_tree && is_bare_repository_cfg < 1) {
			work_tree = git_work_tree_cfg;
			/* make_absolute_path also normalizes the path */
			if (work_tree && !is_absolute_path(work_tree))
				work_tree = xstrdup(make_absolute_path(git_path(work_tree)));
		} else if (work_tree)
			work_tree = xstrdup(make_absolute_path(work_tree));
		git_work_tree_initialized = 1;
		if (work_tree)
			is_bare_repository_cfg = 0;
	}
	return work_tree;
}

char *get_object_directory(void)
{
	if (!git_object_dir)
		setup_git_env();
	return git_object_dir;
}

char *get_index_file(void)
{
	if (!git_index_file)
		setup_git_env();
	return git_index_file;
}

char *get_graft_file(void)
{
	if (!git_graft_file)
		setup_git_env();
	return git_graft_file;
}

int set_git_dir(const char *path)
{
	if (setenv(GIT_DIR_ENVIRONMENT, path, 1))
		return error("Could not set GIT_DIR to '%s'", path);
	setup_git_env();
	return 0;
}
