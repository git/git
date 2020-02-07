#ifndef CONFIG_H
#define CONFIG_H

#include "hashmap.h"
#include "string-list.h"


/**
 * The config API gives callers a way to access Git configuration files
 * (and files which have the same syntax).
 *
 * General Usage
 * -------------
 *
 * Config files are parsed linearly, and each variable found is passed to a
 * caller-provided callback function. The callback function is responsible
 * for any actions to be taken on the config option, and is free to ignore
 * some options. It is not uncommon for the configuration to be parsed
 * several times during the run of a Git program, with different callbacks
 * picking out different variables useful to themselves.
 */

struct object_id;

/* git_config_parse_key() returns these negated: */
#define CONFIG_INVALID_KEY 1
#define CONFIG_NO_SECTION_OR_NAME 2
/* git_config_set_gently(), git_config_set_multivar_gently() return the above or these: */
#define CONFIG_NO_LOCK -1
#define CONFIG_INVALID_FILE 3
#define CONFIG_NO_WRITE 4
#define CONFIG_NOTHING_SET 5
#define CONFIG_INVALID_PATTERN 6
#define CONFIG_GENERIC_ERROR 7

#define CONFIG_REGEX_NONE ((void *)1)

enum config_scope {
	CONFIG_SCOPE_UNKNOWN = 0,
	CONFIG_SCOPE_SYSTEM,
	CONFIG_SCOPE_GLOBAL,
	CONFIG_SCOPE_LOCAL,
	CONFIG_SCOPE_WORKTREE,
	CONFIG_SCOPE_COMMAND,
	CONFIG_SCOPE_SUBMODULE,
};
const char *config_scope_name(enum config_scope scope);

struct git_config_source {
	unsigned int use_stdin:1;
	const char *file;
	const char *blob;
	enum config_scope scope;
};

enum config_origin_type {
	CONFIG_ORIGIN_BLOB,
	CONFIG_ORIGIN_FILE,
	CONFIG_ORIGIN_STDIN,
	CONFIG_ORIGIN_SUBMODULE_BLOB,
	CONFIG_ORIGIN_CMDLINE
};

enum config_event_t {
	CONFIG_EVENT_SECTION,
	CONFIG_EVENT_ENTRY,
	CONFIG_EVENT_WHITESPACE,
	CONFIG_EVENT_COMMENT,
	CONFIG_EVENT_EOF,
	CONFIG_EVENT_ERROR
};

/*
 * The parser event function (if not NULL) is called with the event type and
 * the begin/end offsets of the parsed elements.
 *
 * Note: for CONFIG_EVENT_ENTRY (i.e. config variables), the trailing newline
 * character is considered part of the element.
 */
typedef int (*config_parser_event_fn_t)(enum config_event_t type,
					size_t begin_offset, size_t end_offset,
					void *event_fn_data);

struct config_options {
	unsigned int respect_includes : 1;
	unsigned int ignore_repo : 1;
	unsigned int ignore_worktree : 1;
	unsigned int ignore_cmdline : 1;
	unsigned int system_gently : 1;
	const char *commondir;
	const char *git_dir;
	config_parser_event_fn_t event_fn;
	void *event_fn_data;
	enum config_error_action {
		CONFIG_ERROR_UNSET = 0, /* use source-specific default */
		CONFIG_ERROR_DIE, /* die() on error */
		CONFIG_ERROR_ERROR, /* error() on error, return -1 */
		CONFIG_ERROR_SILENT, /* return -1 */
	} error_action;
};

/**
 * A config callback function takes three parameters:
 *
 * - the name of the parsed variable. This is in canonical "flat" form: the
 *   section, subsection, and variable segments will be separated by dots,
 *   and the section and variable segments will be all lowercase. E.g.,
 *   `core.ignorecase`, `diff.SomeType.textconv`.
 *
 * - the value of the found variable, as a string. If the variable had no
 *   value specified, the value will be NULL (typically this means it
 *   should be interpreted as boolean true).
 *
 * - a void pointer passed in by the caller of the config API; this can
 *   contain callback-specific data
 *
 * A config callback should return 0 for success, or -1 if the variable
 * could not be parsed properly.
 */
typedef int (*config_fn_t)(const char *, const char *, void *);

int git_default_config(const char *, const char *, void *);

/**
 * Read a specific file in git-config format.
 * This function takes the same callback and data parameters as `git_config`.
 */
int git_config_from_file(config_fn_t fn, const char *, void *);

int git_config_from_file_with_options(config_fn_t fn, const char *,
				      void *,
				      const struct config_options *);
int git_config_from_mem(config_fn_t fn,
			const enum config_origin_type,
			const char *name,
			const char *buf, size_t len,
			void *data, const struct config_options *opts);
int git_config_from_blob_oid(config_fn_t fn, const char *name,
			     const struct object_id *oid, void *data);
void git_config_push_parameter(const char *text);
int git_config_from_parameters(config_fn_t fn, void *data);
void read_early_config(config_fn_t cb, void *data);
void read_very_early_config(config_fn_t cb, void *data);

/**
 * Most programs will simply want to look up variables in all config files
 * that Git knows about, using the normal precedence rules. To do this,
 * call `git_config` with a callback function and void data pointer.
 *
 * `git_config` will read all config sources in order of increasing
 * priority. Thus a callback should typically overwrite previously-seen
 * entries with new ones (e.g., if both the user-wide `~/.gitconfig` and
 * repo-specific `.git/config` contain `color.ui`, the config machinery
 * will first feed the user-wide one to the callback, and then the
 * repo-specific one; by overwriting, the higher-priority repo-specific
 * value is left at the end).
 */
void git_config(config_fn_t fn, void *);

/**
 * Lets the caller examine config while adjusting some of the default
 * behavior of `git_config`. It should almost never be used by "regular"
 * Git code that is looking up configuration variables.
 * It is intended for advanced callers like `git-config`, which are
 * intentionally tweaking the normal config-lookup process.
 * It takes two extra parameters:
 *
 * - `config_source`
 * If this parameter is non-NULL, it specifies the source to parse for
 * configuration, rather than looking in the usual files. See `struct
 * git_config_source` in `config.h` for details. Regular `git_config` defaults
 * to `NULL`.
 *
 * - `opts`
 * Specify options to adjust the behavior of parsing config files. See `struct
 * config_options` in `config.h` for details. As an example: regular `git_config`
 * sets `opts.respect_includes` to `1` by default.
 */
int config_with_options(config_fn_t fn, void *,
			struct git_config_source *config_source,
			const struct config_options *opts);

/**
 * Value Parsing Helpers
 * ---------------------
 *
 * The following helper functions aid in parsing string values
 */

int git_parse_ssize_t(const char *, ssize_t *);
int git_parse_ulong(const char *, unsigned long *);

/**
 * Same as `git_config_bool`, except that it returns -1 on error rather
 * than dying.
 */
int git_parse_maybe_bool(const char *);

/**
 * Same as `git_parse_maybe_bool`, except that it does not handle
 * integer values, i.e., those cause this function to return -1.
 */
int git_parse_maybe_bool_text(const char *);

/**
 * Parse the string to an integer, including unit factors. Dies on error;
 * otherwise, returns the parsed result.
 */
int git_config_int(const char *, const char *);

int64_t git_config_int64(const char *, const char *);

/**
 * Identical to `git_config_int`, but for unsigned longs.
 */
unsigned long git_config_ulong(const char *, const char *);

ssize_t git_config_ssize_t(const char *, const char *);

/**
 * Same as `git_config_bool`, except that integers are returned as-is, and
 * an `is_bool` flag is unset.
 */
int git_config_bool_or_int(const char *, const char *, int *);

/**
 * Parse a string into a boolean value, respecting keywords like "true" and
 * "false". Integer values are converted into true/false values (when they
 * are non-zero or zero, respectively). Other values cause a die(). If
 * parsing is successful, the return value is the result.
 */
int git_config_bool(const char *, const char *);

/**
 * Allocates and copies the value string into the `dest` parameter; if no
 * string is given, prints an error message and returns -1.
 */
int git_config_string(const char **, const char *, const char *);

/**
 * Similar to `git_config_string`, but expands `~` or `~user` into the
 * user's home directory when found at the beginning of the path.
 */
int git_config_pathname(const char **, const char *, const char *);

int git_config_expiry_date(timestamp_t *, const char *, const char *);
int git_config_color(char *, const char *, const char *);
int git_config_set_in_file_gently(const char *, const char *, const char *);

/**
 * write config values to a specific config file, takes a key/value pair as
 * parameter.
 */
void git_config_set_in_file(const char *, const char *, const char *);

int git_config_set_gently(const char *, const char *);

/**
 * write config values to `.git/config`, takes a key/value pair as parameter.
 */
void git_config_set(const char *, const char *);

int git_config_parse_key(const char *, char **, int *);
int git_config_key_is_valid(const char *key);
int git_config_set_multivar_gently(const char *, const char *, const char *, int);
void git_config_set_multivar(const char *, const char *, const char *, int);
int git_config_set_multivar_in_file_gently(const char *, const char *, const char *, const char *, int);

/**
 * takes four parameters:
 *
 * - the name of the file, as a string, to which key/value pairs will be written.
 *
 * - the name of key, as a string. This is in canonical "flat" form: the section,
 *   subsection, and variable segments will be separated by dots, and the section
 *   and variable segments will be all lowercase.
 *   E.g., `core.ignorecase`, `diff.SomeType.textconv`.
 *
 * - the value of the variable, as a string. If value is equal to NULL, it will
 *   remove the matching key from the config file.
 *
 * - the value regex, as a string. It will disregard key/value pairs where value
 *   does not match.
 *
 * - a multi_replace value, as an int. If value is equal to zero, nothing or only
 *   one matching key/value is replaced, else all matching key/values (regardless
 *   how many) are removed, before the new pair is written.
 *
 * It returns 0 on success.
 */
void git_config_set_multivar_in_file(const char *, const char *, const char *, const char *, int);

/**
 * rename or remove sections in the config file
 * parameters `old_name` and `new_name`
 * If NULL is passed through `new_name` parameter,
 * the section will be removed from the config file.
 */
int git_config_rename_section(const char *, const char *);

int git_config_rename_section_in_file(const char *, const char *, const char *);
int git_config_copy_section(const char *, const char *);
int git_config_copy_section_in_file(const char *, const char *, const char *);
const char *git_etc_gitconfig(void);
int git_env_bool(const char *, int);
unsigned long git_env_ulong(const char *, unsigned long);
int git_config_system(void);
int config_error_nonbool(const char *);
#if defined(__GNUC__)
#define config_error_nonbool(s) (config_error_nonbool(s), const_error())
#endif

int git_config_parse_parameter(const char *, config_fn_t fn, void *data);

enum config_scope current_config_scope(void);
const char *current_config_origin_type(void);
const char *current_config_name(void);
int current_config_line(void);

/**
 * Include Directives
 * ------------------
 *
 * By default, the config parser does not respect include directives.
 * However, a caller can use the special `git_config_include` wrapper
 * callback to support them. To do so, you simply wrap your "real" callback
 * function and data pointer in a `struct config_include_data`, and pass
 * the wrapper to the regular config-reading functions. For example:
 *
 * -------------------------------------------
 * int read_file_with_include(const char *file, config_fn_t fn, void *data)
 * {
 * struct config_include_data inc = CONFIG_INCLUDE_INIT;
 * inc.fn = fn;
 * inc.data = data;
 * return git_config_from_file(git_config_include, file, &inc);
 * }
 * -------------------------------------------
 *
 * `git_config` respects includes automatically. The lower-level
 * `git_config_from_file` does not.
 *
 */
struct config_include_data {
	int depth;
	config_fn_t fn;
	void *data;
	const struct config_options *opts;
};
#define CONFIG_INCLUDE_INIT { 0 }
int git_config_include(const char *name, const char *value, void *data);

/*
 * Match and parse a config key of the form:
 *
 *   section.(subsection.)?key
 *
 * (i.e., what gets handed to a config_fn_t). The caller provides the section;
 * we return -1 if it does not match, 0 otherwise. The subsection and key
 * out-parameters are filled by the function (and *subsection is NULL if it is
 * missing).
 *
 * If the subsection pointer-to-pointer passed in is NULL, returns 0 only if
 * there is no subsection at all.
 */
int parse_config_key(const char *var,
		     const char *section,
		     const char **subsection, int *subsection_len,
		     const char **key);

/**
 * Custom Configsets
 * -----------------
 *
 * A `config_set` can be used to construct an in-memory cache for
 * config-like files that the caller specifies (i.e., files like `.gitmodules`,
 * `~/.gitconfig` etc.). For example,
 *
 * ----------------------------------------
 * struct config_set gm_config;
 * git_configset_init(&gm_config);
 * int b;
 * //we add config files to the config_set
 * git_configset_add_file(&gm_config, ".gitmodules");
 * git_configset_add_file(&gm_config, ".gitmodules_alt");
 *
 * if (!git_configset_get_bool(gm_config, "submodule.frotz.ignore", &b)) {
 * //hack hack hack
 * }
 *
 * when we are done with the configset:
 * git_configset_clear(&gm_config);
 * ----------------------------------------
 *
 * Configset API provides functions for the above mentioned work flow
 */

struct config_set_element {
	struct hashmap_entry ent;
	char *key;
	struct string_list value_list;
};

struct configset_list_item {
	struct config_set_element *e;
	int value_index;
};

/*
 * the contents of the list are ordered according to their
 * position in the config files and order of parsing the files.
 * (i.e. key-value pair at the last position of .git/config will
 * be at the last item of the list)
 */
struct configset_list {
	struct configset_list_item *items;
	unsigned int nr, alloc;
};

struct config_set {
	struct hashmap config_hash;
	int hash_initialized;
	struct configset_list list;
};

/**
 * Initializes the config_set `cs`.
 */
void git_configset_init(struct config_set *cs);

/**
 * Parses the file and adds the variable-value pairs to the `config_set`,
 * dies if there is an error in parsing the file. Returns 0 on success, or
 * -1 if the file does not exist or is inaccessible. The user has to decide
 * if he wants to free the incomplete configset or continue using it when
 * the function returns -1.
 */
int git_configset_add_file(struct config_set *cs, const char *filename);

/**
 * Finds and returns the value list, sorted in order of increasing priority
 * for the configuration variable `key` and config set `cs`. When the
 * configuration variable `key` is not found, returns NULL. The caller
 * should not free or modify the returned pointer, as it is owned by the cache.
 */
const struct string_list *git_configset_get_value_multi(struct config_set *cs, const char *key);

/**
 * Clears `config_set` structure, removes all saved variable-value pairs.
 */
void git_configset_clear(struct config_set *cs);

/*
 * These functions return 1 if not found, and 0 if found, leaving the found
 * value in the 'dest' pointer.
 */

/*
 * Finds the highest-priority value for the configuration variable `key`
 * and config set `cs`, stores the pointer to it in `value` and returns 0.
 * When the configuration variable `key` is not found, returns 1 without
 * touching `value`. The caller should not free or modify `value`, as it
 * is owned by the cache.
 */
int git_configset_get_value(struct config_set *cs, const char *key, const char **dest);

int git_configset_get_string_const(struct config_set *cs, const char *key, const char **dest);
int git_configset_get_string(struct config_set *cs, const char *key, char **dest);
int git_configset_get_int(struct config_set *cs, const char *key, int *dest);
int git_configset_get_ulong(struct config_set *cs, const char *key, unsigned long *dest);
int git_configset_get_bool(struct config_set *cs, const char *key, int *dest);
int git_configset_get_bool_or_int(struct config_set *cs, const char *key, int *is_bool, int *dest);
int git_configset_get_maybe_bool(struct config_set *cs, const char *key, int *dest);
int git_configset_get_pathname(struct config_set *cs, const char *key, const char **dest);

/* Functions for reading a repository's config */
struct repository;
void repo_config(struct repository *repo, config_fn_t fn, void *data);
int repo_config_get_value(struct repository *repo,
			  const char *key, const char **value);
const struct string_list *repo_config_get_value_multi(struct repository *repo,
						      const char *key);
int repo_config_get_string_const(struct repository *repo,
				 const char *key, const char **dest);
int repo_config_get_string(struct repository *repo,
			   const char *key, char **dest);
int repo_config_get_int(struct repository *repo,
			const char *key, int *dest);
int repo_config_get_ulong(struct repository *repo,
			  const char *key, unsigned long *dest);
int repo_config_get_bool(struct repository *repo,
			 const char *key, int *dest);
int repo_config_get_bool_or_int(struct repository *repo,
				const char *key, int *is_bool, int *dest);
int repo_config_get_maybe_bool(struct repository *repo,
			       const char *key, int *dest);
int repo_config_get_pathname(struct repository *repo,
			     const char *key, const char **dest);

/**
 * Querying For Specific Variables
 * -------------------------------
 *
 * For programs wanting to query for specific variables in a non-callback
 * manner, the config API provides two functions `git_config_get_value`
 * and `git_config_get_value_multi`. They both read values from an internal
 * cache generated previously from reading the config files.
 */

/**
 * Finds the highest-priority value for the configuration variable `key`,
 * stores the pointer to it in `value` and returns 0. When the
 * configuration variable `key` is not found, returns 1 without touching
 * `value`. The caller should not free or modify `value`, as it is owned
 * by the cache.
 */
int git_config_get_value(const char *key, const char **value);

/**
 * Finds and returns the value list, sorted in order of increasing priority
 * for the configuration variable `key`. When the configuration variable
 * `key` is not found, returns NULL. The caller should not free or modify
 * the returned pointer, as it is owned by the cache.
 */
const struct string_list *git_config_get_value_multi(const char *key);

/**
 * Resets and invalidates the config cache.
 */
void git_config_clear(void);

/**
 * Allocates and copies the retrieved string into the `dest` parameter for
 * the configuration variable `key`; if NULL string is given, prints an
 * error message and returns -1. When the configuration variable `key` is
 * not found, returns 1 without touching `dest`.
 */
int git_config_get_string_const(const char *key, const char **dest);

/**
 * Similar to `git_config_get_string_const`, except that retrieved value
 * copied into the `dest` parameter is a mutable string.
 */
int git_config_get_string(const char *key, char **dest);

/**
 * Finds and parses the value to an integer for the configuration variable
 * `key`. Dies on error; otherwise, stores the value of the parsed integer in
 * `dest` and returns 0. When the configuration variable `key` is not found,
 * returns 1 without touching `dest`.
 */
int git_config_get_int(const char *key, int *dest);

/**
 * Similar to `git_config_get_int` but for unsigned longs.
 */
int git_config_get_ulong(const char *key, unsigned long *dest);

/**
 * Finds and parses the value into a boolean value, for the configuration
 * variable `key` respecting keywords like "true" and "false". Integer
 * values are converted into true/false values (when they are non-zero or
 * zero, respectively). Other values cause a die(). If parsing is successful,
 * stores the value of the parsed result in `dest` and returns 0. When the
 * configuration variable `key` is not found, returns 1 without touching
 * `dest`.
 */
int git_config_get_bool(const char *key, int *dest);

/**
 * Similar to `git_config_get_bool`, except that integers are copied as-is,
 * and `is_bool` flag is unset.
 */
int git_config_get_bool_or_int(const char *key, int *is_bool, int *dest);

/**
 * Similar to `git_config_get_bool`, except that it returns -1 on error
 * rather than dying.
 */
int git_config_get_maybe_bool(const char *key, int *dest);

/**
 * Similar to `git_config_get_string`, but expands `~` or `~user` into
 * the user's home directory when found at the beginning of the path.
 */
int git_config_get_pathname(const char *key, const char **dest);

int git_config_get_index_threads(int *dest);
int git_config_get_untracked_cache(void);
int git_config_get_split_index(void);
int git_config_get_max_percent_split_change(void);
int git_config_get_fsmonitor(void);

/* This dies if the configured or default date is in the future */
int git_config_get_expiry(const char *key, const char **output);

/* parse either "this many days" integer, or "5.days.ago" approxidate */
int git_config_get_expiry_in_days(const char *key, timestamp_t *, timestamp_t now);

struct key_value_info {
	const char *filename;
	int linenr;
	enum config_origin_type origin_type;
	enum config_scope scope;
};

/**
 * First prints the error message specified by the caller in `err` and then
 * dies printing the line number and the file name of the highest priority
 * value for the configuration variable `key`.
 */
NORETURN void git_die_config(const char *key, const char *err, ...) __attribute__((format(printf, 2, 3)));

/**
 * Helper function which formats the die error message according to the
 * parameters entered. Used by `git_die_config()`. It can be used by callers
 * handling `git_config_get_value_multi()` to print the correct error message
 * for the desired value.
 */
NORETURN void git_die_config_linenr(const char *key, const char *filename, int linenr);

#define LOOKUP_CONFIG(mapping, var) \
	lookup_config(mapping, ARRAY_SIZE(mapping), var)
int lookup_config(const char **mapping, int nr_mapping, const char *var);

#endif /* CONFIG_H */
