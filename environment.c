/*
 * We put all the git config variables in this same object
 * file, so that programs can link against the config parser
 * without having to link against all the rest of git.
 *
 * In particular, no need to bring in libz etc unless needed,
 * even if you might want to know where the git directory etc
 * are.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "abspath.h"
#include "branch.h"
#include "convert.h"
#include "environment.h"
#include "gettext.h"
#include "git-zlib.h"
#include "repository.h"
#include "config.h"
#include "refs.h"
#include "fmt-merge-msg.h"
#include "commit.h"
#include "strvec.h"
#include "path.h"
#include "chdir-notify.h"
#include "setup.h"
#include "write-or-die.h"

int trust_executable_bit = 1;
int trust_ctime = 1;
int check_stat = 1;
int has_symlinks = 1;
int minimum_abbrev = 4, default_abbrev = -1;
int ignore_case;
int assume_unchanged;
int is_bare_repository_cfg = -1; /* unspecified */
int warn_on_object_refname_ambiguity = 1;
int repository_format_precious_objects;
char *git_commit_encoding;
char *git_log_output_encoding;
char *apply_default_whitespace;
char *apply_default_ignorewhitespace;
char *git_attributes_file;
int zlib_compression_level = Z_BEST_SPEED;
int pack_compression_level = Z_DEFAULT_COMPRESSION;
int fsync_object_files = -1;
int use_fsync = -1;
enum fsync_method fsync_method = FSYNC_METHOD_DEFAULT;
enum fsync_component fsync_components = FSYNC_COMPONENTS_DEFAULT;
char *editor_program;
char *askpass_program;
char *excludes_file;
enum auto_crlf auto_crlf = AUTO_CRLF_FALSE;
enum eol core_eol = EOL_UNSET;
int global_conv_flags_eol = CONV_EOL_RNDTRP_WARN;
char *check_roundtrip_encoding;
enum branch_track git_branch_track = BRANCH_TRACK_REMOTE;
enum rebase_setup_type autorebase = AUTOREBASE_NEVER;
enum push_default_type push_default = PUSH_DEFAULT_UNSPECIFIED;
#ifndef OBJECT_CREATION_MODE
#define OBJECT_CREATION_MODE OBJECT_CREATION_USES_HARDLINKS
#endif
enum object_creation_mode object_creation_mode = OBJECT_CREATION_MODE;
int grafts_keep_true_parents;
int core_apply_sparse_checkout;
int core_sparse_checkout_cone;
int sparse_expect_files_outside_of_patterns;
int merge_log_config = -1;
int precomposed_unicode = -1; /* see probe_utf8_pathname_composition() */
unsigned long pack_size_limit_cfg;
int max_allowed_tree_depth =
#ifdef _MSC_VER
	/*
	 * When traversing into too-deep trees, Visual C-compiled Git seems to
	 * run into some internal stack overflow detection in the
	 * `RtlpAllocateHeap()` function that is called from within
	 * `git_inflate_init()`'s call tree. The following value seems to be
	 * low enough to avoid that by letting Git exit with an error before
	 * the stack overflow can occur.
	 */
	512;
#elif defined(GIT_WINDOWS_NATIVE) && defined(__clang__) && defined(__aarch64__)
	/*
	 * Similar to Visual C, it seems that on Windows/ARM64 the clang-based
	 * builds have a smaller stack space available. When running out of
	 * that stack space, a `STATUS_STACK_OVERFLOW` is produced. When the
	 * Git command was run from an MSYS2 Bash, this unfortunately results
	 * in an exit code 127. Let's prevent that by lowering the maximal
	 * tree depth; This value seems to be low enough.
	 */
	1280;
#else
	2048;
#endif

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
const char *comment_line_str = "#";
char *comment_line_str_to_free;
int auto_comment_line_char;

/* This is set by setup_git_directory_gently() and/or git_default_config() */
char *git_work_tree_cfg;

/*
 * Repository-local GIT_* environment variables; see environment.h for details.
 */
const char * const local_repo_env[] = {
	ALTERNATE_DB_ENVIRONMENT,
	CONFIG_ENVIRONMENT,
	CONFIG_DATA_ENVIRONMENT,
	CONFIG_COUNT_ENVIRONMENT,
	DB_ENVIRONMENT,
	GIT_DIR_ENVIRONMENT,
	GIT_WORK_TREE_ENVIRONMENT,
	GIT_IMPLICIT_WORK_TREE_ENVIRONMENT,
	GRAFT_ENVIRONMENT,
	INDEX_ENVIRONMENT,
	NO_REPLACE_OBJECTS_ENVIRONMENT,
	GIT_REPLACE_REF_BASE_ENVIRONMENT,
	GIT_PREFIX_ENVIRONMENT,
	GIT_SHALLOW_FILE_ENVIRONMENT,
	GIT_COMMON_DIR_ENVIRONMENT,
	NULL
};

const char *getenv_safe(struct strvec *argv, const char *name)
{
	const char *value = getenv(name);

	if (!value)
		return NULL;

	strvec_push(argv, value);
	return argv->v[argv->nr - 1];
}

int is_bare_repository(void)
{
	/* if core.bare is not 'false', let's see if there is a work tree */
	return is_bare_repository_cfg && !repo_get_work_tree(the_repository);
}

int have_git_dir(void)
{
	return startup_info->have_repository
		|| the_repository->gitdir;
}

const char *get_git_namespace(void)
{
	static const char *namespace;

	struct strbuf buf = STRBUF_INIT;
	struct strbuf **components, **c;
	const char *raw_namespace;

	if (namespace)
		return namespace;

	raw_namespace = getenv(GIT_NAMESPACE_ENVIRONMENT);
	if (!raw_namespace || !*raw_namespace) {
		namespace = "";
		return namespace;
	}

	strbuf_addstr(&buf, raw_namespace);
	components = strbuf_split(&buf, '/');
	strbuf_reset(&buf);
	for (c = components; *c; c++)
		if (strcmp((*c)->buf, "/") != 0)
			strbuf_addf(&buf, "refs/namespaces/%s", (*c)->buf);
	strbuf_list_free(components);
	if (check_refname_format(buf.buf, 0))
		die(_("bad git namespace path \"%s\""), raw_namespace);
	strbuf_addch(&buf, '/');

	namespace = strbuf_detach(&buf, NULL);

	return namespace;
}

const char *strip_namespace(const char *namespaced_ref)
{
	const char *out;
	if (skip_prefix(namespaced_ref, get_git_namespace(), &out))
		return out;
	return NULL;
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
