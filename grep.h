#ifndef GREP_H
#define GREP_H
#include "color.h"
#ifdef USE_LIBPCRE
#include <pcre.h>
#else
typedef int pcre;
typedef int pcre_extra;
#endif
#include "kwset.h"
#include "thread-utils.h"
#include "userdiff.h"

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

struct grep_pat {
	struct grep_pat *next;
	const char *origin;
	int no;
	enum grep_pat_token token;
	char *pattern;
	size_t patternlen;
	enum grep_header_field field;
	regex_t regexp;
	pcre *pcre_regexp;
	pcre_extra *pcre_extra_info;
	kwset_t kws;
	unsigned fixed:1;
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
	const char *prefix;
	int prefix_length;
	regex_t regexp;
	int linenum;
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
	int pcre;
	int relative;
	int pathname;
	int null_following_name;
	int color;
	int max_depth;
	int funcname;
	int funcbody;
	int extended_regexp_option;
	int pattern_type_option;
	char color_context[COLOR_MAXLEN];
	char color_filename[COLOR_MAXLEN];
	char color_function[COLOR_MAXLEN];
	char color_lineno[COLOR_MAXLEN];
	char color_match[COLOR_MAXLEN];
	char color_selected[COLOR_MAXLEN];
	char color_sep[COLOR_MAXLEN];
	int regflags;
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

extern void init_grep_defaults(void);
extern int grep_config(const char *var, const char *value, void *);
extern void grep_init(struct grep_opt *, const char *prefix);
void grep_set_pattern_type_option(enum grep_pattern_type, struct grep_opt *opt);
void grep_commit_pattern_type(enum grep_pattern_type, struct grep_opt *opt);

extern void append_grep_pat(struct grep_opt *opt, const char *pat, size_t patlen, const char *origin, int no, enum grep_pat_token t);
extern void append_grep_pattern(struct grep_opt *opt, const char *pat, const char *origin, int no, enum grep_pat_token t);
extern void append_header_grep_pattern(struct grep_opt *, enum grep_header_field, const char *);
extern void compile_grep_patterns(struct grep_opt *opt);
extern void free_grep_patterns(struct grep_opt *opt);
extern int grep_buffer(struct grep_opt *opt, char *buf, unsigned long size);

struct grep_source {
	char *name;

	enum grep_source_type {
		GREP_SOURCE_SHA1,
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
void grep_source_load_driver(struct grep_source *gs);


int grep_source(struct grep_opt *opt, struct grep_source *gs);

extern struct grep_opt *grep_opt_dup(const struct grep_opt *opt);
extern int grep_threads_ok(const struct grep_opt *opt);

#ifndef NO_PTHREADS
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

#else
#define grep_read_lock()
#define grep_read_unlock()
#endif

#endif
