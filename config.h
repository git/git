#ifndef CONFIG_H
#define CONFIG_H

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

struct git_config_source {
	unsigned int use_stdin:1;
	const char *file;
	const char *blob;
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
	const char *commondir;
	const char *git_dir;
	config_parser_event_fn_t event_fn;
	void *event_fn_data;
};

typedef int (*config_fn_t)(const char *, const char *, void *);
extern int git_default_config(const char *, const char *, void *);
extern int git_config_from_file(config_fn_t fn, const char *, void *);
extern int git_config_from_file_with_options(config_fn_t fn, const char *,
					     void *,
					     const struct config_options *);
extern int git_config_from_mem(config_fn_t fn, const enum config_origin_type,
					const char *name, const char *buf, size_t len, void *data);
extern int git_config_from_blob_oid(config_fn_t fn, const char *name,
				    const struct object_id *oid, void *data);
extern void git_config_push_parameter(const char *text);
extern int git_config_from_parameters(config_fn_t fn, void *data);
extern void read_early_config(config_fn_t cb, void *data);
extern void git_config(config_fn_t fn, void *);
extern int config_with_options(config_fn_t fn, void *,
			       struct git_config_source *config_source,
			       const struct config_options *opts);
extern int git_parse_ssize_t(const char *, ssize_t *);
extern int git_parse_ulong(const char *, unsigned long *);
extern int git_parse_maybe_bool(const char *);
extern int git_config_int(const char *, const char *);
extern int64_t git_config_int64(const char *, const char *);
extern unsigned long git_config_ulong(const char *, const char *);
extern ssize_t git_config_ssize_t(const char *, const char *);
extern int git_config_bool_or_int(const char *, const char *, int *);
extern int git_config_bool(const char *, const char *);
extern int git_config_string(const char **, const char *, const char *);
extern int git_config_pathname(const char **, const char *, const char *);
extern int git_config_expiry_date(timestamp_t *, const char *, const char *);
extern int git_config_color(char *, const char *, const char *);
extern int git_config_set_in_file_gently(const char *, const char *, const char *);
extern void git_config_set_in_file(const char *, const char *, const char *);
extern int git_config_set_gently(const char *, const char *);
extern void git_config_set(const char *, const char *);
extern int git_config_parse_key(const char *, char **, int *);
extern int git_config_key_is_valid(const char *key);
extern int git_config_set_multivar_gently(const char *, const char *, const char *, int);
extern void git_config_set_multivar(const char *, const char *, const char *, int);
extern int git_config_set_multivar_in_file_gently(const char *, const char *, const char *, const char *, int);
extern void git_config_set_multivar_in_file(const char *, const char *, const char *, const char *, int);
extern int git_config_rename_section(const char *, const char *);
extern int git_config_rename_section_in_file(const char *, const char *, const char *);
extern int git_config_copy_section(const char *, const char *);
extern int git_config_copy_section_in_file(const char *, const char *, const char *);
extern const char *git_etc_gitconfig(void);
extern int git_env_bool(const char *, int);
extern unsigned long git_env_ulong(const char *, unsigned long);
extern int git_config_system(void);
extern int config_error_nonbool(const char *);
#if defined(__GNUC__)
#define config_error_nonbool(s) (config_error_nonbool(s), const_error())
#endif

extern int git_config_parse_parameter(const char *, config_fn_t fn, void *data);

enum config_scope {
	CONFIG_SCOPE_UNKNOWN = 0,
	CONFIG_SCOPE_SYSTEM,
	CONFIG_SCOPE_GLOBAL,
	CONFIG_SCOPE_REPO,
	CONFIG_SCOPE_CMDLINE,
};

extern enum config_scope current_config_scope(void);
extern const char *current_config_origin_type(void);
extern const char *current_config_name(void);

struct config_include_data {
	int depth;
	config_fn_t fn;
	void *data;
	const struct config_options *opts;
};
#define CONFIG_INCLUDE_INIT { 0 }
extern int git_config_include(const char *name, const char *value, void *data);

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
extern int parse_config_key(const char *var,
			    const char *section,
			    const char **subsection, int *subsection_len,
			    const char **key);

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

extern void git_configset_init(struct config_set *cs);
extern int git_configset_add_file(struct config_set *cs, const char *filename);
extern int git_configset_get_value(struct config_set *cs, const char *key, const char **value);
extern const struct string_list *git_configset_get_value_multi(struct config_set *cs, const char *key);
extern void git_configset_clear(struct config_set *cs);
extern int git_configset_get_string_const(struct config_set *cs, const char *key, const char **dest);
extern int git_configset_get_string(struct config_set *cs, const char *key, char **dest);
extern int git_configset_get_int(struct config_set *cs, const char *key, int *dest);
extern int git_configset_get_ulong(struct config_set *cs, const char *key, unsigned long *dest);
extern int git_configset_get_bool(struct config_set *cs, const char *key, int *dest);
extern int git_configset_get_bool_or_int(struct config_set *cs, const char *key, int *is_bool, int *dest);
extern int git_configset_get_maybe_bool(struct config_set *cs, const char *key, int *dest);
extern int git_configset_get_pathname(struct config_set *cs, const char *key, const char **dest);

/* Functions for reading a repository's config */
struct repository;
extern void repo_config(struct repository *repo, config_fn_t fn, void *data);
extern int repo_config_get_value(struct repository *repo,
				 const char *key, const char **value);
extern const struct string_list *repo_config_get_value_multi(struct repository *repo,
							     const char *key);
extern int repo_config_get_string_const(struct repository *repo,
					const char *key, const char **dest);
extern int repo_config_get_string(struct repository *repo,
				  const char *key, char **dest);
extern int repo_config_get_int(struct repository *repo,
			       const char *key, int *dest);
extern int repo_config_get_ulong(struct repository *repo,
				 const char *key, unsigned long *dest);
extern int repo_config_get_bool(struct repository *repo,
				const char *key, int *dest);
extern int repo_config_get_bool_or_int(struct repository *repo,
				       const char *key, int *is_bool, int *dest);
extern int repo_config_get_maybe_bool(struct repository *repo,
				      const char *key, int *dest);
extern int repo_config_get_pathname(struct repository *repo,
				    const char *key, const char **dest);

/*
 * Note: This function exists solely to maintain backward compatibility with
 * 'fetch' and 'update_clone' storing configuration in '.gitmodules' and should
 * NOT be used anywhere else.
 *
 * Runs the provided config function on the '.gitmodules' file found in the
 * working directory.
 */
extern void config_from_gitmodules(config_fn_t fn, void *data);

extern int git_config_get_value(const char *key, const char **value);
extern const struct string_list *git_config_get_value_multi(const char *key);
extern void git_config_clear(void);
extern int git_config_get_string_const(const char *key, const char **dest);
extern int git_config_get_string(const char *key, char **dest);
extern int git_config_get_int(const char *key, int *dest);
extern int git_config_get_ulong(const char *key, unsigned long *dest);
extern int git_config_get_bool(const char *key, int *dest);
extern int git_config_get_bool_or_int(const char *key, int *is_bool, int *dest);
extern int git_config_get_maybe_bool(const char *key, int *dest);
extern int git_config_get_pathname(const char *key, const char **dest);
extern int git_config_get_untracked_cache(void);
extern int git_config_get_split_index(void);
extern int git_config_get_max_percent_split_change(void);
extern int git_config_get_fsmonitor(void);

/* This dies if the configured or default date is in the future */
extern int git_config_get_expiry(const char *key, const char **output);

/* parse either "this many days" integer, or "5.days.ago" approxidate */
extern int git_config_get_expiry_in_days(const char *key, timestamp_t *, timestamp_t now);

struct key_value_info {
	const char *filename;
	int linenr;
	enum config_origin_type origin_type;
	enum config_scope scope;
};

extern NORETURN void git_die_config(const char *key, const char *err, ...) __attribute__((format(printf, 2, 3)));
extern NORETURN void git_die_config_linenr(const char *key, const char *filename, int linenr);

#endif /* CONFIG_H */
