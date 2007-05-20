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
int trust_executable_bit = 1;
int has_symlinks = 1;
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
int zlib_compression_level = Z_DEFAULT_COMPRESSION;
size_t packed_git_window_size = DEFAULT_PACKED_GIT_WINDOW_SIZE;
size_t packed_git_limit = DEFAULT_PACKED_GIT_LIMIT;
size_t delta_base_cache_limit = 16 * 1024 * 1024;
int pager_in_use;
int pager_use_color = 1;
int auto_crlf = 0;	/* 1: both ways, -1: only when adding git objects */

static const char *git_dir;
static char *git_object_dir, *git_index_file, *git_refs_dir, *git_graft_file;

static void setup_git_env(void)
{
	git_dir = getenv(GIT_DIR_ENVIRONMENT);
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
	const char *dir, *s;
	if (0 <= is_bare_repository_cfg)
		return is_bare_repository_cfg;

	dir = get_git_dir();
	if (!strcmp(dir, DEFAULT_GIT_DIR_ENVIRONMENT))
		return 0;
	s = strrchr(dir, '/');
	return !s || strcmp(s + 1, DEFAULT_GIT_DIR_ENVIRONMENT);
}

const char *get_git_dir(void)
{
	if (!git_dir)
		setup_git_env();
	return git_dir;
}

char *get_object_directory(void)
{
	if (!git_object_dir)
		setup_git_env();
	return git_object_dir;
}

char *get_refs_directory(void)
{
	if (!git_refs_dir)
		setup_git_env();
	return git_refs_dir;
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


