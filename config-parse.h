/*
 * Low level config parsing.
 */
#ifndef CONFIG_PARSE_H
#define CONFIG_PARSE_H

#include "strbuf.h"

/* git_config_parse_key() returns these: */
#define CONFIG_INVALID_KEY 1
#define CONFIG_NO_SECTION_OR_NAME 2

int git_config_parse_key(const char *, char **, size_t *);

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

enum config_origin_type {
	CONFIG_ORIGIN_UNKNOWN = 0,
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

struct config_source;
/*
 * The parser event function (if not NULL) is called with the event type and
 * the begin/end offsets of the parsed elements.
 *
 * Note: for CONFIG_EVENT_ENTRY (i.e. config variables), the trailing newline
 * character is considered part of the element.
 */
typedef int (*config_parser_event_fn_t)(enum config_event_t type,
					size_t begin_offset, size_t end_offset,
					struct config_source *cs,
					void *event_fn_data);

struct config_parse_options {
	/*
	 * event_fn and event_fn_data are for internal use only. Handles events
	 * emitted by the config parser.
	 */
	config_parser_event_fn_t event_fn;
	void *event_fn_data;
};

struct config_source {
	struct config_source *prev;
	union {
		FILE *file;
		struct config_buf {
			const char *buf;
			size_t len;
			size_t pos;
		} buf;
	} u;
	enum config_origin_type origin_type;
	const char *name;
	const char *path;
	int linenr;
	int eof;
	size_t total_len;
	struct strbuf value;
	struct strbuf var;
	unsigned subsection_case_sensitive : 1;

	int (*do_fgetc)(struct config_source *c);
	int (*do_ungetc)(int c, struct config_source *conf);
	long (*do_ftell)(struct config_source *c);
};
#define CONFIG_SOURCE_INIT { 0 }

/* Config source metadata for a given config key-value pair */
struct key_value_info {
	const char *filename;
	int linenr;
	enum config_origin_type origin_type;
	enum config_scope scope;
	const char *path;
};
#define KVI_INIT { \
	.filename = NULL, \
	.linenr = -1, \
	.origin_type = CONFIG_ORIGIN_UNKNOWN, \
	.scope = CONFIG_SCOPE_UNKNOWN, \
	.path = NULL, \
}

/* Captures additional information that a config callback can use. */
struct config_context {
	/* Config source metadata for key and value. */
	const struct key_value_info *kvi;
};
#define CONFIG_CONTEXT_INIT { 0 }

/**
 * A config callback function takes four parameters:
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
 * - the 'config context', that is, additional information about the config
 *   iteration operation provided by the config machinery. For example, this
 *   includes information about the config source being parsed (e.g. the
 *   filename).
 *
 * - a void pointer passed in by the caller of the config API; this can
 *   contain callback-specific data
 *
 * A config callback should return 0 for success, or -1 if the variable
 * could not be parsed properly.
 */
typedef int (*config_fn_t)(const char *, const char *,
			   const struct config_context *, void *);

int git_config_from_file_with_options(config_fn_t fn, const char *,
				      void *, enum config_scope,
				      const struct config_parse_options *);

int git_config_from_mem(config_fn_t fn,
			const enum config_origin_type,
			const char *name,
			const char *buf, size_t len,
			void *data, enum config_scope scope,
			const struct config_parse_options *opts);

int git_config_from_stdin(config_fn_t fn, void *data, enum config_scope scope,
			  const struct config_parse_options *config_opts);

#endif /* CONFIG_PARSE_H */
