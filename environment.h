#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "repo-settings.h"

/* Double-check local_repo_env below if you add to this list. */
#define GIT_DIR_ENVIRONMENT "GIT_DIR"
#define GIT_COMMON_DIR_ENVIRONMENT "GIT_COMMON_DIR"
#define GIT_NAMESPACE_ENVIRONMENT "GIT_NAMESPACE"
#define GIT_WORK_TREE_ENVIRONMENT "GIT_WORK_TREE"
#define GIT_PREFIX_ENVIRONMENT "GIT_PREFIX"
#define DEFAULT_GIT_DIR_ENVIRONMENT ".git"
#define DB_ENVIRONMENT "GIT_OBJECT_DIRECTORY"
#define INDEX_ENVIRONMENT "GIT_INDEX_FILE"
#define GRAFT_ENVIRONMENT "GIT_GRAFT_FILE"
#define GIT_SHALLOW_FILE_ENVIRONMENT "GIT_SHALLOW_FILE"
#define TEMPLATE_DIR_ENVIRONMENT "GIT_TEMPLATE_DIR"
#define CONFIG_ENVIRONMENT "GIT_CONFIG"
#define CONFIG_DATA_ENVIRONMENT "GIT_CONFIG_PARAMETERS"
#define CONFIG_COUNT_ENVIRONMENT "GIT_CONFIG_COUNT"
#define EXEC_PATH_ENVIRONMENT "GIT_EXEC_PATH"
#define CEILING_DIRECTORIES_ENVIRONMENT "GIT_CEILING_DIRECTORIES"
#define NO_REPLACE_OBJECTS_ENVIRONMENT "GIT_NO_REPLACE_OBJECTS"
#define GIT_REPLACE_REF_BASE_ENVIRONMENT "GIT_REPLACE_REF_BASE"
#define NO_LAZY_FETCH_ENVIRONMENT "GIT_NO_LAZY_FETCH"
#define GITATTRIBUTES_FILE ".gitattributes"
#define INFOATTRIBUTES_FILE "info/attributes"
#define ATTRIBUTE_MACRO_PREFIX "[attr]"
#define GITMODULES_FILE ".gitmodules"
#define GITMODULES_INDEX ":.gitmodules"
#define GITMODULES_HEAD "HEAD:.gitmodules"
#define GIT_NOTES_REF_ENVIRONMENT "GIT_NOTES_REF"
#define GIT_NOTES_DEFAULT_REF "refs/notes/commits"
#define GIT_NOTES_DISPLAY_REF_ENVIRONMENT "GIT_NOTES_DISPLAY_REF"
#define GIT_NOTES_REWRITE_REF_ENVIRONMENT "GIT_NOTES_REWRITE_REF"
#define GIT_NOTES_REWRITE_MODE_ENVIRONMENT "GIT_NOTES_REWRITE_MODE"
#define GIT_LITERAL_PATHSPECS_ENVIRONMENT "GIT_LITERAL_PATHSPECS"
#define GIT_GLOB_PATHSPECS_ENVIRONMENT "GIT_GLOB_PATHSPECS"
#define GIT_NOGLOB_PATHSPECS_ENVIRONMENT "GIT_NOGLOB_PATHSPECS"
#define GIT_ICASE_PATHSPECS_ENVIRONMENT "GIT_ICASE_PATHSPECS"
#define GIT_QUARANTINE_ENVIRONMENT "GIT_QUARANTINE_PATH"
#define GIT_OPTIONAL_LOCKS_ENVIRONMENT "GIT_OPTIONAL_LOCKS"
#define GIT_TEXT_DOMAIN_DIR_ENVIRONMENT "GIT_TEXTDOMAINDIR"
#define GIT_ATTR_SOURCE_ENVIRONMENT "GIT_ATTR_SOURCE"
#define GIT_REF_URI_ENVIRONMENT "GIT_REF_URI"

/*
 * Environment variable used to propagate the --no-advice global option to the
 * advice_enabled() helper, even when run in a subprocess.
 * This is an internal variable that should not be set by the user.
 */
#define GIT_ADVICE_ENVIRONMENT "GIT_ADVICE"

/*
 * Environment variable used in handshaking the wire protocol.
 * Contains a colon ':' separated list of keys with optional values
 * 'key[=value]'.  Presence of unknown keys and values must be
 * ignored.
 */
#define GIT_PROTOCOL_ENVIRONMENT "GIT_PROTOCOL"
/* HTTP header used to handshake the wire protocol */
#define GIT_PROTOCOL_HEADER "Git-Protocol"

/*
 * This environment variable is expected to contain a boolean indicating
 * whether we should or should not treat:
 *
 *   GIT_DIR=foo.git git ...
 *
 * as if GIT_WORK_TREE=. was given. It's not expected that users will make use
 * of this, but we use it internally to communicate to sub-processes that we
 * are in a bare repo. If not set, defaults to true.
 */
#define GIT_IMPLICIT_WORK_TREE_ENVIRONMENT "GIT_IMPLICIT_WORK_TREE"

#define ALTERNATE_DB_ENVIRONMENT "GIT_ALTERNATE_OBJECT_DIRECTORIES"

/*
 * Repository-local GIT_* environment variables; these will be cleared
 * when git spawns a sub-process that runs inside another repository.
 * The array is NULL-terminated, which makes it easy to pass in the "env"
 * parameter of a run-command invocation, or to do a simple walk.
 */
extern const char * const local_repo_env[];

struct strvec;

/*
 * Wrapper of getenv() that returns a strdup value. This value is kept
 * in argv to be freed later.
 */
const char *getenv_safe(struct strvec *argv, const char *name);

/*
 * Should we print an ellipsis after an abbreviated SHA-1 value
 * when doing diff-raw output or indicating a detached HEAD?
 */
int print_sha1_ellipsis(void);

/*
 * Returns the boolean value of $GIT_OPTIONAL_LOCKS (or the default value).
 */
int use_optional_locks(void);

const char *get_git_namespace(void);
const char *strip_namespace(const char *namespaced_ref);

int git_default_config(const char *, const char *,
		       const struct config_context *, void *);

/*
 * TODO: All the below state either explicitly or implicitly relies on
 * `the_repository`. We should eventually get rid of these and make the
 * dependency on a repository explicit:
 *
 *   - `setup_git_env()` ideally shouldn't exist as it modifies global state,
 *     namely the environment. The current process shouldn't ever access that
 *     state via envvars though, but should instead consult a `struct
 *     repository`. When spawning new processes, we would ideally also pass a
 *     `struct repository` and then set up the environment variables for the
 *     child process, only.
 *
 *   - `have_git_dir()` should not have to exist at all. Instead, we should
 *     decide on whether or not we have a `struct repository`.
 *
 *   - All the global config variables should become tied to a repository. Like
 *     this, we'd correctly honor repository-local configuration and be able to
 *     distinguish configuration values from different repositories.
 *
 * Please do not add new global config variables here.
 */
# ifdef USE_THE_REPOSITORY_VARIABLE
void setup_git_env(const char *git_dir);

/*
 * Returns true iff we have a configured git repository (either via
 * setup_git_directory, or in the environment via $GIT_DIR).
 */
int have_git_dir(void);

extern int is_bare_repository_cfg;
int is_bare_repository(void);
extern char *git_work_tree_cfg;

/* Environment bits from configuration mechanism */
extern int trust_executable_bit;
extern int trust_ctime;
extern int check_stat;
extern int has_symlinks;
extern int minimum_abbrev, default_abbrev;
extern int ignore_case;
extern int assume_unchanged;
extern int warn_on_object_refname_ambiguity;
extern char *apply_default_whitespace;
extern char *apply_default_ignorewhitespace;
extern char *git_attributes_file;
extern int zlib_compression_level;
extern int pack_compression_level;
extern unsigned long pack_size_limit_cfg;
extern int max_allowed_tree_depth;

extern int precomposed_unicode;
extern int protect_hfs;
extern int protect_ntfs;

extern int core_apply_sparse_checkout;
extern int core_sparse_checkout_cone;
extern int sparse_expect_files_outside_of_patterns;

enum rebase_setup_type {
	AUTOREBASE_NEVER = 0,
	AUTOREBASE_LOCAL,
	AUTOREBASE_REMOTE,
	AUTOREBASE_ALWAYS
};
extern enum rebase_setup_type autorebase;

enum push_default_type {
	PUSH_DEFAULT_NOTHING = 0,
	PUSH_DEFAULT_MATCHING,
	PUSH_DEFAULT_SIMPLE,
	PUSH_DEFAULT_UPSTREAM,
	PUSH_DEFAULT_CURRENT,
	PUSH_DEFAULT_UNSPECIFIED
};
extern enum push_default_type push_default;

enum object_creation_mode {
	OBJECT_CREATION_USES_HARDLINKS = 0,
	OBJECT_CREATION_USES_RENAMES = 1
};
extern enum object_creation_mode object_creation_mode;

extern int grafts_keep_true_parents;

const char *get_log_output_encoding(void);
const char *get_commit_output_encoding(void);

extern char *git_commit_encoding;
extern char *git_log_output_encoding;

extern char *editor_program;
extern char *askpass_program;
extern char *excludes_file;

/*
 * The character that begins a commented line in user-editable file
 * that is subject to stripspace.
 */
extern const char *comment_line_str;
extern char *comment_line_str_to_free;
#ifndef WITH_BREAKING_CHANGES
extern int auto_comment_line_char;
extern bool warn_on_auto_comment_char;
#endif /* !WITH_BREAKING_CHANGES */

# endif /* USE_THE_REPOSITORY_VARIABLE */
#endif /* ENVIRONMENT_H */
