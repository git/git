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
int use_legacy_headers = 1;
int trust_executable_bit = 1;
int assume_unchanged;
int prefer_symlink_refs;
int log_all_ref_updates;
int warn_ambiguous_refs = 1;
int repository_format_version;
char *git_commit_encoding;
char *git_log_output_encoding;
int shared_repository = PERM_UMASK;
const char *apply_default_whitespace;
int zlib_compression_level = Z_DEFAULT_COMPRESSION;
size_t packed_git_window_size = 32 * 1024 * 1024;
size_t packed_git_limit = 256 * 1024 * 1024;
int pager_in_use;
int pager_use_color = 1;

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
	log_all_ref_updates = !is_bare_git_dir(git_dir);
}

int is_bare_git_dir (const char *dir)
{
	const char *s;
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


