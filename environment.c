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
#include "repository.h"
#include "config.h"
#include "refs.h"
#include "fmt-merge-msg.h"
#include "commit.h"
#include "argv-array.h"
#include "object-store.h"
#include "chdir-notify.h"

int trust_executable_bit = 1;
int trust_ctime = 1;
int check_stat = 1;
int has_symlinks = 1;
int minimum_abbrev = 4, default_abbrev = -1;
int ignore_case;
int assume_unchanged;
int prefer_symlink_refs;
int is_bare_repository_cfg = -1; /* unspecified */
int warn_ambiguous_refs = 1;
int warn_on_object_refname_ambiguity = 1;
int ref_paranoia = -1;
int repository_format_precious_objects;
char *repository_format_partial_clone;
const char *core_partial_clone_filter_default;
const char *git_commit_encoding;
const char *git_log_output_encoding;
const char *apply_default_whitespace;
const char *apply_default_ignorewhitespace;
const char *git_attributes_file;
const char *git_hooks_path;
int zlib_compression_level = Z_BEST_SPEED;
int core_compression_level;
int pack_compression_level = Z_DEFAULT_COMPRESSION;
int fsync_object_files;
size_t packed_git_window_size = DEFAULT_PACKED_GIT_WINDOW_SIZE;
size_t packed_git_limit = DEFAULT_PACKED_GIT_LIMIT;
size_t delta_base_cache_limit = 96 * 1024 * 1024;
unsigned long big_file_threshold = 512 * 1024 * 1024;
int pager_use_color = 1;
const char *editor_program;
const char *askpass_program;
const char *excludes_file;
enum auto_crlf auto_crlf = AUTO_CRLF_FALSE;
int check_replace_refs = 1; /* NEEDSWORK: rename to read_replace_refs */
char *git_replace_ref_base;
enum eol core_eol = EOL_UNSET;
int global_conv_flags_eol = CONV_EOL_RNDTRP_WARN;
unsigned whitespace_rule_cfg = WS_DEFAULT_RULE;
enum branch_track git_branch_track = BRANCH_TRACK_REMOTE;
enum rebase_setup_type autorebase = AUTOREBASE_NEVER;
enum push_default_type push_default = PUSH_DEFAULT_UNSPECIFIED;
#ifndef OBJECT_CREATION_MODE
#define OBJECT_CREATION_MODE OBJECT_CREATION_USES_HARDLINKS
#endif
enum object_creation_mode object_creation_mode = OBJECT_CREATION_MODE;
char *notes_ref_name;
int grafts_replace_parents = 1;
int core_commit_graph;
int core_apply_sparse_checkout;
int merge_log_config = -1;
int precomposed_unicode = -1; /* see probe_utf8_pathname_composition() */
unsigned long pack_size_limit_cfg;
enum hide_dotfiles_type hide_dotfiles = HIDE_DOTFILES_DOTGITONLY;
enum log_refs_config log_all_ref_updates = LOG_REFS_UNSET;

#ifndef PROTECT_HFS_DEFAULT
#define PROTECT_HFS_DEFAULT 0
#endif
int protect_hfs = PROTECT_HFS_DEFAULT;

#ifndef PROTECT_NTFS_DEFAULT
#define PROTECT_NTFS_DEFAULT 0
#endif
int protect_ntfs = PROTECT_NTFS_DEFAULT;
const char *core_fsmonitor;

/*
 * The character that begins a commented line in user-editable file
 * that is subject to stripspace.
 */
char comment_line_char = '#';
int auto_comment_line_char;

/* Parallel index stat data preload? */
int core_preload_index = 1;

/*
 * This is a hack for test programs like test-dump-untracked-cache to
 * ensure that they do not modify the untracked cache when reading it.
 * Do not use it otherwise!
 */
int ignore_untracked_cache_config;

/* This is set by setup_git_dir_gently() and/or git_default_config() */
char *git_work_tree_cfg;

static char *git_namespace;

static const char *super_prefix;

/*
 * Repository-local GIT_* environment variables; see cache.h for details.
 */
const char * const local_repo_env[] = {
	ALTERNATE_DB_ENVIRONMENT,
	CONFIG_ENVIRONMENT,
	CONFIG_DATA_ENVIRONMENT,
	DB_ENVIRONMENT,
	GIT_DIR_ENVIRONMENT,
	GIT_WORK_TREE_ENVIRONMENT,
	GIT_IMPLICIT_WORK_TREE_ENVIRONMENT,
	GRAFT_ENVIRONMENT,
	INDEX_ENVIRONMENT,
	NO_REPLACE_OBJECTS_ENVIRONMENT,
	GIT_REPLACE_REF_BASE_ENVIRONMENT,
	GIT_PREFIX_ENVIRONMENT,
	GIT_SUPER_PREFIX_ENVIRONMENT,
	GIT_SHALLOW_FILE_ENVIRONMENT,
	GIT_COMMON_DIR_ENVIRONMENT,
	NULL
};

static char *expand_namespace(const char *raw_namespace)
{
	struct strbuf buf = STRBUF_INIT;
	struct strbuf **components, **c;

	if (!raw_namespace || !*raw_namespace)
		return xstrdup("");

	strbuf_addstr(&buf, raw_namespace);
	components = strbuf_split(&buf, '/');
	strbuf_reset(&buf);
	for (c = components; *c; c++)
		if (strcmp((*c)->buf, "/") != 0)
			strbuf_addf(&buf, "refs/namespaces/%s", (*c)->buf);
	strbuf_list_free(components);
	if (check_refname_format(buf.buf, 0))
		die("bad git namespace path \"%s\"", raw_namespace);
	strbuf_addch(&buf, '/');
	return strbuf_detach(&buf, NULL);
}

/*
 * Wrapper of getenv() that returns a strdup value. This value is kept
 * in argv to be freed later.
 */
static const char *getenv_safe(struct argv_array *argv, const char *name)
{
	const char *value = getenv(name);

	if (!value)
		return NULL;

	argv_array_push(argv, value);
	return argv->argv[argv->argc - 1];
}

void setup_git_env(const char *git_dir)
{
	const char *shallow_file;
	const char *replace_ref_base;
	struct set_gitdir_args args = { NULL };
	struct argv_array to_free = ARGV_ARRAY_INIT;

	args.commondir = getenv_safe(&to_free, GIT_COMMON_DIR_ENVIRONMENT);
	args.object_dir = getenv_safe(&to_free, DB_ENVIRONMENT);
	args.graft_file = getenv_safe(&to_free, GRAFT_ENVIRONMENT);
	args.index_file = getenv_safe(&to_free, INDEX_ENVIRONMENT);
	args.alternate_db = getenv_safe(&to_free, ALTERNATE_DB_ENVIRONMENT);
	repo_set_gitdir(the_repository, git_dir, &args);
	argv_array_clear(&to_free);

	if (getenv(NO_REPLACE_OBJECTS_ENVIRONMENT))
		check_replace_refs = 0;
	replace_ref_base = getenv(GIT_REPLACE_REF_BASE_ENVIRONMENT);
	free(git_replace_ref_base);
	git_replace_ref_base = xstrdup(replace_ref_base ? replace_ref_base
							  : "refs/replace/");
	free(git_namespace);
	git_namespace = expand_namespace(getenv(GIT_NAMESPACE_ENVIRONMENT));
	shallow_file = getenv(GIT_SHALLOW_FILE_ENVIRONMENT);
	if (shallow_file)
		set_alternate_shallow_file(shallow_file, 0);
}

int is_bare_repository(void)
{
	/* if core.bare is not 'false', let's see if there is a work tree */
	return is_bare_repository_cfg && !get_git_work_tree();
}

int have_git_dir(void)
{
	return startup_info->have_repository
		|| the_repository->gitdir;
}

const char *get_git_dir(void)
{
	if (!the_repository->gitdir)
		BUG("git environment hasn't been setup");
	return the_repository->gitdir;
}

const char *get_git_common_dir(void)
{
	if (!the_repository->commondir)
		BUG("git environment hasn't been setup");
	return the_repository->commondir;
}

const char *get_git_namespace(void)
{
	if (!git_namespace)
		BUG("git environment hasn't been setup");
	return git_namespace;
}

const char *strip_namespace(const char *namespaced_ref)
{
	const char *out;
	if (skip_prefix(namespaced_ref, get_git_namespace(), &out))
		return out;
	return NULL;
}

const char *get_super_prefix(void)
{
	static int initialized;
	if (!initialized) {
		super_prefix = getenv(GIT_SUPER_PREFIX_ENVIRONMENT);
		initialized = 1;
	}
	return super_prefix;
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
		new_work_tree = real_path(new_work_tree);
		if (strcmp(new_work_tree, the_repository->worktree))
			die("internal error: work tree has already been set\n"
			    "Current worktree: %s\nNew worktree: %s",
			    the_repository->worktree, new_work_tree);
		return;
	}
	git_work_tree_initialized = 1;
	repo_set_worktree(the_repository, new_work_tree);
}

const char *get_git_work_tree(void)
{
	return the_repository->worktree;
}

char *get_object_directory(void)
{
	if (!the_repository->objects->objectdir)
		BUG("git environment hasn't been setup");
	return the_repository->objects->objectdir;
}

int odb_mkstemp(struct strbuf *temp_filename, const char *pattern)
{
	int fd;
	/*
	 * we let the umask do its job, don't try to be more
	 * restrictive except to remove write permission.
	 */
	int mode = 0444;
	git_path_buf(temp_filename, "objects/%s", pattern);
	fd = git_mkstemp_mode(temp_filename->buf, mode);
	if (0 <= fd)
		return fd;

	/* slow path */
	/* some mkstemp implementations erase temp_filename on failure */
	git_path_buf(temp_filename, "objects/%s", pattern);
	safe_create_leading_directories(temp_filename->buf);
	return xmkstemp_mode(temp_filename->buf, mode);
}

int odb_pack_keep(const char *name)
{
	int fd;

	fd = open(name, O_RDWR|O_CREAT|O_EXCL, 0600);
	if (0 <= fd)
		return fd;

	/* slow path */
	safe_create_leading_directories_const(name);
	return open(name, O_RDWR|O_CREAT|O_EXCL, 0600);
}

char *get_index_file(void)
{
	if (!the_repository->index_file)
		BUG("git environment hasn't been setup");
	return the_repository->index_file;
}

char *get_graft_file(void)
{
	if (!the_repository->graft_file)
		BUG("git environment hasn't been setup");
	return the_repository->graft_file;
}

static void set_git_dir_1(const char *path)
{
	if (setenv(GIT_DIR_ENVIRONMENT, path, 1))
		die("could not set GIT_DIR to '%s'", path);
	setup_git_env(path);
}

static void update_relative_gitdir(const char *name,
				   const char *old_cwd,
				   const char *new_cwd,
				   void *data)
{
	char *path = reparent_relative_path(old_cwd, new_cwd, get_git_dir());
	trace_printf_key(&trace_setup_key,
			 "setup: move $GIT_DIR to '%s'",
			 path);
	set_git_dir_1(path);
	free(path);
}

void set_git_dir(const char *path)
{
	set_git_dir_1(path);
	if (!is_absolute_path(path))
		chdir_notify_register(NULL, update_relative_gitdir, NULL);
}

const char *get_log_output_encoding(void)
{
	return git_log_output_encoding ? git_log_output_encoding
		: get_commit_output_encoding();
}

const char *get_commit_output_encoding(void)
{
	return git_commit_encoding ? git_commit_encoding : "UTF-8";
}

static int the_shared_repository = PERM_UMASK;
static int need_shared_repository_from_config = 1;

void set_shared_repository(int value)
{
	the_shared_repository = value;
	need_shared_repository_from_config = 0;
}

int get_shared_repository(void)
{
	if (need_shared_repository_from_config) {
		const char *var = "core.sharedrepository";
		const char *value;
		if (!git_config_get_value(var, &value))
			the_shared_repository = git_config_perm(var, value);
		need_shared_repository_from_config = 0;
	}
	return the_shared_repository;
}

void reset_shared_repository(void)
{
	need_shared_repository_from_config = 1;
}

int use_optional_locks(void)
{
	return git_env_bool(GIT_OPTIONAL_LOCKS_ENVIRONMENT, 1);
}

int print_sha1_ellipsis(void)
{
	/*
	 * Determine if the calling environment contains the variable
	 * GIT_PRINT_SHA1_ELLIPSIS set to "yes".
	 */
	static int cached_result = -1; /* unknown */

	if (cached_result < 0) {
		const char *v = getenv("GIT_PRINT_SHA1_ELLIPSIS");
		cached_result = (v && !strcasecmp(v, "yes"));
	}
	return cached_result;
}
