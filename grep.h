#ifndef GREP_H
#define GREP_H
#include "color.h"
#ifdef USE_LIBPCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#if (PCRE2_MAJOR >= 10 && PCRE2_MINOR >= 36) || PCRE2_MAJOR >= 11
#define GIT_PCRE2_VERSION_10_36_OR_HIGHER
#endif
#if (PCRE2_MAJOR >= 10 && PCRE2_MINOR >= 34) || PCRE2_MAJOR >= 11
#define GIT_PCRE2_VERSION_10_34_OR_HIGHER
#endif
#else
typedef int pcre2_code;
typedef int pcre2_match_data;
typedef int pcre2_compile_context;
typedef int pcre2_general_context;
#endif
#ifndef PCRE2_MATCH_INVALID_UTF
/* PCRE2_MATCH_* dummy also with !USE_LIBPCRE2, for test-pcre2-config.c */
#define PCRE2_MATCH_INVALID_UTF 0
#endif
#include "thread-utils.h"
#include "userdiff.h"

struct repository;

enum grep_pat_token {
	GREP_PATTERN,
	GREP_PATTERN_HEAD,
	GREP_PATTERN_BODY,
	GREP_AND,
	GREP_OPEN_PAREN,
	GREP_CLOSE_PAREN,
	GREP_NOT,
	GREP_OR
};

enum grep_context {
	GREP_CONTEXT_HEAD,
	GREP_CONTEXT_BODY
};

enum grep_header_field {
	GREP_HEADER_FIELD_MIN = 0,
	GREP_HEADER_AUTHOR = GREP_HEADER_FIELD_MIN,
	GREP_HEADER_COMMITTER,
	GREP_HEADER_REFLOG,

	/* Must be at the end of the enum */
	GREP_HEADER_FIELD_MAX
};

enum grep_color {
	GREP_COLOR_CONTEXT,
	GREP_COLOR_FILENAME,
	GREP_COLOR_FUNCTION,
	GREP_COLOR_LINENO,
	GREP_COLOR_COLUMNNO,
	GREP_COLOR_MATCH_CONTEXT,
	GREP_COLOR_MATCH_SELECTED,
	GREP_COLOR_SELECTED,
	GREP_COLOR_SEP,
	NR_GREP_COLORS
};

struct grep_pat {
	struct grep_pat *next;
	const char *origin;
	int no;
	enum grep_pat_token token;
	char *pattern;
	size_t patternlen;
	enum grep_header_field field;
	regex_t regexp;
	pcre2_code *pcre2_pattern;
	pcre2_match_data *pcre2_match_data;
	pcre2_compile_context *pcre2_compile_context;
	pcre2_general_context *pcre2_general_context;
	const uint8_t *pcre2_tables;
	uint32_t pcre2_jit_on;
	unsigned fixed:1;
	unsigned is_fixed:1;
	unsigned ignore_case:1;
	unsigned word_regexp:1;
};

enum grep_expr_node {
	GREP_NODE_ATOM,
	GREP_NODE_NOT,
	GREP_NODE_AND,
	GREP_NODE_TRUE,
	GREP_NODE_OR
};

enum grep_pattern_type {
	GREP_PATTERN_TYPE_UNSPECIFIED = 0,
	GREP_PATTERN_TYPE_BRE,
	GREP_PATTERN_TYPE_ERE,
	GREP_PATTERN_TYPE_FIXED,
	GREP_PATTERN_TYPE_PCRE
};

struct grep_expr {
	enum grep_expr_node node;
	unsigned hit;
	union {
		struct grep_pat *atom;
		struct grep_expr *unary;
		struct {
			struct grep_expr *left;
			struct grep_expr *right;
		} binary;
	} u;
};

struct grep_opt {
	struct grep_pat *pattern_list;
	struct grep_pat **pattern_tail;
	struct grep_pat *header_list;
	struct grep_pat **header_tail;
	struct grep_expr *pattern_expression;

	/*
	 * NEEDSWORK: See if we can remove this field, because the repository
	 * should probably be per-source. That is, grep.c functions using this
	 * field should probably start using "repo" in "struct grep_source"
	 * instead.
	 *
	 * This is potentially the cause of at least one bug - "git grep"
	 * using the textconv attributes from the superproject on the
	 * submodules. See the failing "git grep --textconv" tests in
	 * t7814-grep-recurse-submodules.sh for more information.
	 */
	struct repository *repo;

	const char *prefix;
	int prefix_length;
	regex_t regexp;
	int linenum;
	int columnnum;
	int invert;
	int ignore_case;
	int status_only;
	int name_only;
	int unmatch_name_only;
	int count;
	int word_regexp;
	int fixed;
	int all_match;
#define GREP_BINARY_DEFAULT	0
#define GREP_BINARY_NOMATCH	1
#define GREP_BINARY_TEXT	2
	int binary;
	int allow_textconv;
	int extended;
	int use_reflog_filter;
	int pcre2;
	int relative;
	int pathname;
	int null_following_name;
	int only_matching;
	int color;
	int max_depth;
	int funcname;
	int funcbody;
	int extended_regexp_option;
	int pattern_type_option;
	int ignore_locale;
	char colors[NR_GREP_COLORS][COLOR_MAXLEN];
	unsigned pre_context;
	unsigned post_context;
	unsigned last_shown;
	int show_hunk_mark;
	int file_break;
	int heading;
	void *priv;

	void (*output)(struct grep_opt *opt, const void *data, size_t size);
	void *output_priv;
};

int grep_config(const char *var, const char *value, void *);
void grep_init(struct grep_opt *, struct repository *repo, const char *prefix);
void grep_commit_pattern_type(enum grep_pattern_type, struct grep_opt *opt);

void append_grep_pat(struct grep_opt *opt, const char *pat, size_t patlen, const char *origin, int no, enum grep_pat_token t);
void append_grep_pattern(struct grep_opt *opt, const char *pat, const char *origin, int no, enum grep_pat_token t);
void append_header_grep_pattern(struct grep_opt *, enum grep_header_field, const char *);
void compile_grep_patterns(struct grep_opt *opt);
void free_grep_patterns(struct grep_opt *opt);
int grep_buffer(struct grep_opt *opt, const char *buf, unsigned long size);

/* The field parameter is only used to filter header patterns
 * (where appropriate). If filtering isn't desirable
 * GREP_HEADER_FIELD_MAX should be supplied.
 */
int grep_next_match(struct grep_opt *opt,
		    const char *bol, const char *eol,
		    enum grep_context ctx, regmatch_t *pmatch,
		    enum grep_header_field field, int eflags);

struct grep_source {
	char *name;

	enum grep_source_type {
		GREP_SOURCE_OID,
		GREP_SOURCE_FILE,
		GREP_SOURCE_BUF,
	} type;
	void *identifier;
	struct repository *repo; /* if GREP_SOURCE_OID */

	const char *buf;
	unsigned long size;

	char *path; /* for attribute lookups */
	struct userdiff_driver *driver;
};

void grep_source_init_file(struct grep_source *gs, const char *name,
			   const char *path);
void grep_source_init_oid(struct grep_source *gs, const char *name,
			  const char *path, const struct object_id *oid,
			  struct repository *repo);
void grep_source_clear_data(struct grep_source *gs);
void grep_source_clear(struct grep_source *gs);
void grep_source_load_driver(struct grep_source *gs,
			     struct index_state *istate);


int grep_source(struct grep_opt *opt, struct grep_source *gs);

struct grep_opt *grep_opt_dup(const struct grep_opt *opt);

/*
 * Mutex used around access to the attributes machinery if
 * opt->use_threads.  Must be initialized/destroyed by callers!
 */
extern int grep_use_locks;
extern pthread_mutex_t grep_attr_mutex;

#endif
