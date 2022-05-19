/*
 * We put all the but config variables in this same object
 * file, so that programs can link against the config parser
 * without having to link against all the rest of but.
 *
 * In particular, no need to bring in libz etc unless needed,
 * even if you might want to know where the but directory etc
 * are.
 */
#include "cache.h"
#include "branch.h"
#include "environment.h"
#include "repository.h"
#include "config.h"
#include "refs.h"
#include "fmt-merge-msg.h"
#include "cummit.h"
#include "strvec.h"
#include "object-store.h"
#include "tmp-objdir.h"
#include "chdir-notify.h"
#include "shallow.h"

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
int repository_format_precious_objects;
int repository_format_worktree_config;
const char *but_cummit_encoding;
const char *but_log_output_encoding;
char *apply_default_whitespace;
char *apply_default_ignorewhitespace;
const char *but_attributes_file;
const char *but_hooks_path;
int zlib_compression_level = Z_BEST_SPEED;
int pack_compression_level = Z_DEFAULT_COMPRESSION;
int fsync_object_files = -1;
int use_fsync = -1;
enum fsync_method fsync_method = FSYNC_METHOD_DEFAULT;
enum fsync_component fsync_components = FSYNC_COMPONENTS_DEFAULT;
size_t packed_but_window_size = DEFAULT_PACKED_BUT_WINDOW_SIZE;
size_t packed_but_limit = DEFAULT_PACKED_BUT_LIMIT;
size_t delta_base_cache_limit = 96 * 1024 * 1024;
unsigned long big_file_threshold = 512 * 1024 * 1024;
int pager_use_color = 1;
const char *editor_program;
const char *askpass_program;
const char *excludes_file;
enum auto_crlf auto_crlf = AUTO_CRLF_FALSE;
int read_replace_refs = 1;
char *but_replace_ref_base;
enum eol core_eol = EOL_UNSET;
int global_conv_flags_eol = CONV_EOL_RNDTRP_WARN;
char *check_roundtrip_encoding = "SHIFT-JIS";
unsigned whitespace_rule_cfg = WS_DEFAULT_RULE;
enum branch_track but_branch_track = BRANCH_TRACK_REMOTE;
enum rebase_setup_type autorebase = AUTOREBASE_NEVER;
enum push_default_type push_default = PUSH_DEFAULT_UNSPECIFIED;
#ifndef OBJECT_CREATION_MODE
#define OBJECT_CREATION_MODE OBJECT_CREATION_USES_HARDLINKS
#endif
enum object_creation_mode object_creation_mode = OBJECT_CREATION_MODE;
char *notes_ref_name;
int grafts_replace_parents = 1;
int core_apply_sparse_checkout;
int core_sparse_checkout_cone;
int sparse_expect_files_outside_of_patterns;
int merge_log_config = -1;
int precomposed_unicode = -1; /* see probe_utf8_pathname_composition() */
unsigned long pack_size_limit_cfg;
enum log_refs_config log_all_ref_updates = LOG_REFS_UNSET;

#ifndef PROTECT_HFS_DEFAULT
#define PROTECT_HFS_DEFAULT 0
#endif
int protect_hfs = PROTECT_HFS_DEFAULT;

#ifndef PROTECT_NTFS_DEFAULT
#define PROTECT_NTFS_DEFAULT 1
#endif
int protect_ntfs = PROTECT_NTFS_DEFAULT;

/*
 * The character that begins a commented line in user-editable file
 * that is subject to stripspace.
 */
char comment_line_char = '#';
int auto_comment_line_char;

/* Parallel index stat data preload? */
int core_preload_index = 1;

/* This is set by setup_but_dir_gently() and/or but_default_config() */
char *but_work_tree_cfg;

static char *but_namespace;

static char *super_prefix;

/*
 * Repository-local BUT_* environment variables; see cache.h for details.
 */
const char * const local_repo_env[] = {
	ALTERNATE_DB_ENVIRONMENT,
	CONFIG_ENVIRONMENT,
	CONFIG_DATA_ENVIRONMENT,
	CONFIG_COUNT_ENVIRONMENT,
	DB_ENVIRONMENT,
	BUT_DIR_ENVIRONMENT,
	BUT_WORK_TREE_ENVIRONMENT,
	BUT_IMPLICIT_WORK_TREE_ENVIRONMENT,
	GRAFT_ENVIRONMENT,
	INDEX_ENVIRONMENT,
	NO_REPLACE_OBJECTS_ENVIRONMENT,
	BUT_REPLACE_REF_BASE_ENVIRONMENT,
	BUT_PREFIX_ENVIRONMENT,
	BUT_SUPER_PREFIX_ENVIRONMENT,
	BUT_SHALLOW_FILE_ENVIRONMENT,
	BUT_COMMON_DIR_ENVIRONMENT,
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
		die(_("bad but namespace path \"%s\""), raw_namespace);
	strbuf_addch(&buf, '/');
	return strbuf_detach(&buf, NULL);
}

const char *getenv_safe(struct strvec *argv, const char *name)
{
	const char *value = getenv(name);

	if (!value)
		return NULL;

	strvec_push(argv, value);
	return argv->v[argv->nr - 1];
}

void setup_but_env(const char *but_dir)
{
	const char *shallow_file;
	const char *replace_ref_base;
	struct set_butdir_args args = { NULL };
	struct strvec to_free = STRVEC_INIT;

	args.commondir = getenv_safe(&to_free, BUT_COMMON_DIR_ENVIRONMENT);
	args.object_dir = getenv_safe(&to_free, DB_ENVIRONMENT);
	args.graft_file = getenv_safe(&to_free, GRAFT_ENVIRONMENT);
	args.index_file = getenv_safe(&to_free, INDEX_ENVIRONMENT);
	args.alternate_db = getenv_safe(&to_free, ALTERNATE_DB_ENVIRONMENT);
	if (getenv(BUT_QUARANTINE_ENVIRONMENT)) {
		args.disable_ref_updates = 1;
	}

	repo_set_butdir(the_repository, but_dir, &args);
	strvec_clear(&to_free);

	if (getenv(NO_REPLACE_OBJECTS_ENVIRONMENT))
		read_replace_refs = 0;
	replace_ref_base = getenv(BUT_REPLACE_REF_BASE_ENVIRONMENT);
	free(but_replace_ref_base);
	but_replace_ref_base = xstrdup(replace_ref_base ? replace_ref_base
							  : "refs/replace/");
	free(but_namespace);
	but_namespace = expand_namespace(getenv(BUT_NAMESPACE_ENVIRONMENT));
	shallow_file = getenv(BUT_SHALLOW_FILE_ENVIRONMENT);
	if (shallow_file)
		set_alternate_shallow_file(the_repository, shallow_file, 0);
}

int is_bare_repository(void)
{
	/* if core.bare is not 'false', let's see if there is a work tree */
	return is_bare_repository_cfg && !get_but_work_tree();
}

int have_but_dir(void)
{
	return startup_info->have_repository
		|| the_repository->butdir;
}

const char *get_but_dir(void)
{
	if (!the_repository->butdir)
		BUG("but environment hasn't been setup");
	return the_repository->butdir;
}

const char *get_but_common_dir(void)
{
	if (!the_repository->commondir)
		BUG("but environment hasn't been setup");
	return the_repository->commondir;
}

const char *get_but_namespace(void)
{
	if (!but_namespace)
		BUG("but environment hasn't been setup");
	return but_namespace;
}

const char *strip_namespace(const char *namespaced_ref)
{
	const char *out;
	if (skip_prefix(namespaced_ref, get_but_namespace(), &out))
		return out;
	return NULL;
}

const char *get_super_prefix(void)
{
	static int initialized;
	if (!initialized) {
		super_prefix = xstrdup_or_null(getenv(BUT_SUPER_PREFIX_ENVIRONMENT));
		initialized = 1;
	}
	return super_prefix;
}

static int but_work_tree_initialized;

/*
 * Note.  This works only before you used a work tree.  This was added
 * primarily to support but-clone to work in a new repository it just
 * created, and is not meant to flip between different work trees.
 */
void set_but_work_tree(const char *new_work_tree)
{
	if (but_work_tree_initialized) {
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
	but_work_tree_initialized = 1;
	repo_set_worktree(the_repository, new_work_tree);
}

const char *get_but_work_tree(void)
{
	return the_repository->worktree;
}

const char *get_object_directory(void)
{
	if (!the_repository->objects->odb)
		BUG("but environment hasn't been setup");
	return the_repository->objects->odb->path;
}

int odb_mkstemp(struct strbuf *temp_filename, const char *pattern)
{
	int fd;
	/*
	 * we let the umask do its job, don't try to be more
	 * restrictive except to remove write permission.
	 */
	int mode = 0444;
	but_path_buf(temp_filename, "objects/%s", pattern);
	fd = but_mkstemp_mode(temp_filename->buf, mode);
	if (0 <= fd)
		return fd;

	/* slow path */
	/* some mkstemp implementations erase temp_filename on failure */
	but_path_buf(temp_filename, "objects/%s", pattern);
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
		BUG("but environment hasn't been setup");
	return the_repository->index_file;
}

char *get_graft_file(struct repository *r)
{
	if (!r->graft_file)
		BUG("but environment hasn't been setup");
	return r->graft_file;
}

static void set_but_dir_1(const char *path)
{
	xsetenv(BUT_DIR_ENVIRONMENT, path, 1);
	setup_but_env(path);
}

static void update_relative_butdir(const char *name,
				   const char *old_cwd,
				   const char *new_cwd,
				   void *data)
{
	char *path = reparent_relative_path(old_cwd, new_cwd, get_but_dir());
	struct tmp_objdir *tmp_objdir = tmp_objdir_unapply_primary_odb();

	trace_printf_key(&trace_setup_key,
			 "setup: move $BUT_DIR to '%s'",
			 path);
	set_but_dir_1(path);
	if (tmp_objdir)
		tmp_objdir_reapply_primary_odb(tmp_objdir, old_cwd, new_cwd);
	free(path);
}

void set_but_dir(const char *path, int make_realpath)
{
	struct strbuf realpath = STRBUF_INIT;

	if (make_realpath) {
		strbuf_realpath(&realpath, path, 1);
		path = realpath.buf;
	}

	set_but_dir_1(path);
	if (!is_absolute_path(path))
		chdir_notify_register(NULL, update_relative_butdir, NULL);

	strbuf_release(&realpath);
}

const char *get_log_output_encoding(void)
{
	return but_log_output_encoding ? but_log_output_encoding
		: get_cummit_output_encoding();
}

const char *get_cummit_output_encoding(void)
{
	return but_cummit_encoding ? but_cummit_encoding : "UTF-8";
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
		if (!but_config_get_value(var, &value))
			the_shared_repository = but_config_perm(var, value);
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
	return but_env_bool(BUT_OPTIONAL_LOCKS_ENVIRONMENT, 1);
}

int print_sha1_ellipsis(void)
{
	/*
	 * Determine if the calling environment contains the variable
	 * BUT_PRINT_SHA1_ELLIPSIS set to "yes".
	 */
	static int cached_result = -1; /* unknown */

	if (cached_result < 0) {
		const char *v = getenv("BUT_PRINT_SHA1_ELLIPSIS");
		cached_result = (v && !strcasecmp(v, "yes"));
	}
	return cached_result;
}
