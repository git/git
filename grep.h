#ifndef GREP_H
#define GREP_H
#include "color.h"
#ifdef USE_LIBPCRE1
#include <pcre.h>
#ifndef PCRE_NO_UTF8_CHECK
#define PCRE_NO_UTF8_CHECK 0
#endif
#else
typedef int pcre;
typedef int pcre_extra;
#endif
#ifdef USE_LIBPCRE2
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#else
typedef int pcre2_code;
typedef int pcre2_match_data;
typedef int pcre2_compile_context;
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
	pcre *pcre1_regexp;
	pcre_extra *pcre1_extra_info;
	const unsigned char *pcre1_tables;
	int pcre1_jit_on;
	pcre2_code *pcre2_pattern;
	pcre2_match_data *pcre2_match_data;
	pcre2_compile_context *pcre2_compile_context;
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
	int debug;
#define GREP_BINARY_DEFAULT	0
#define GREP_BINARY_NOMATCH	1
#define GREP_BINARY_TEXT	2
	int binary;
	int allow_textconv;
	int extended;
	int use_reflog_filter;
	int pcre1;
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

void init_grep_defaults(struct repository *);
int grep_config(const char *var, const char *value, void *);
void grep_init(struct grep_opt *, struct repository *repo, const char *prefix);
void grep_commit_pattern_type(enum grep_pattern_type, struct grep_opt *opt);

void append_grep_pat(struct grep_opt *opt, const char *pat, size_t patlen, const char *origin, int no, enum grep_pat_token t);
void append_grep_pattern(struct grep_opt *opt, const char *pat, const char *origin, int no, enum grep_pat_token t);
void append_header_grep_pattern(struct grep_opt *, enum grep_header_field, const char *);
void compile_grep_patterns(struct grep_opt *opt);
void free_grep_patterns(struct grep_opt *opt);
int grep_buffer(struct grep_opt *opt, char *buf, unsigned long size);

struct grep_source {
	char *name;

	enum grep_source_type {
		GREP_SOURCE_OID,
		GREP_SOURCE_FILE,
		GREP_SOURCE_BUF,
	} type;
	void *identifier;

	char *buf;
	unsigned long size;

	char *path; /* for attribute lookups */
	struct userdiff_driver *driver;
};

void grep_source_init(struct grep_source *gs, enum grep_source_type type,
		      const char *name, const char *path,
		      const void *identifier);
void grep_source_clear_data(struct grep_source *gs);
void grep_source_clear(struct grep_source *gs);
void grep_source_load_driver(struct grep_source *gs,
			     struct index_state *istate);


int grep_source(struct grep_opt *opt, struct grep_source *gs);

struct grep_opt *grep_opt_dup(const struct grep_opt *opt);
int grep_threads_ok(const struct grep_opt *opt);

/*
 * Mutex used around access to the attributes machinery if
 * opt->use_threads.  Must be initialized/destroyed by callers!
 */
extern int grep_use_locks;
extern pthread_mutex_t grep_attr_mutex;
extern pthread_mutex_t grep_read_mutex;

static inline void grep_read_lock(void)
{
	if (grep_use_locks)
		pthread_mutex_lock(&grep_read_mutex);
}

static inline void grep_read_unlock(void)
{
	if (grep_use_locks)
		pthread_mutex_unlock(&grep_read_mutex);
}

#endif
