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
	GREP_HEADER_AUTHOR = 0,
	GREP_HEADER_COMMITTER
};
#define GREP_HEADER_FIELD_MAX (GREP_HEADER_COMMITTER + 1)

struct grep_pat {
	struct grep_pat *next;
	const char *origin;
	int no;
	enum grep_pat_token token;
	const char *pattern;
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
#define GREP_BINARY_DEFAULT	0
#define GREP_BINARY_NOMATCH	1
#define GREP_BINARY_TEXT	2
	int binary;
	int extended;
	int pcre;
	int relative;
	int pathname;
	int null_following_name;
	int color;
	int max_depth;
	int funcname;
	int funcbody;
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

extern void append_grep_pat(struct grep_opt *opt, const char *pat, size_t patlen, const char *origin, int no, enum grep_pat_token t);
extern void append_grep_pattern(struct grep_opt *opt, const char *pat, const char *origin, int no, enum grep_pat_token t);
extern void append_header_grep_pattern(struct grep_opt *, enum grep_header_field, const char *);
extern void compile_grep_patterns(struct grep_opt *opt);
extern void free_grep_patterns(struct grep_opt *opt);
extern int grep_buffer(struct grep_opt *opt, const char *name, char *buf, unsigned long size);

extern struct grep_opt *grep_opt_dup(const struct grep_opt *opt);
extern int grep_threads_ok(const struct grep_opt *opt);

#endif
