#ifndef GREP_H
#define GREP_H
#include "color.h"

enum grep_pat_token {
	GREP_PATTERN,
	GREP_PATTERN_HEAD,
	GREP_PATTERN_BODY,
	GREP_AND,
	GREP_OPEN_PAREN,
	GREP_CLOSE_PAREN,
	GREP_NOT,
	GREP_OR,
};

enum grep_context {
	GREP_CONTEXT_HEAD,
	GREP_CONTEXT_BODY,
};

enum grep_header_field {
	GREP_HEADER_AUTHOR = 0,
	GREP_HEADER_COMMITTER,
};

struct grep_pat {
	struct grep_pat *next;
	const char *origin;
	int no;
	enum grep_pat_token token;
	const char *pattern;
	enum grep_header_field field;
	regex_t regexp;
	unsigned fixed:1;
	unsigned word_regexp:1;
};

enum grep_expr_node {
	GREP_NODE_ATOM,
	GREP_NODE_NOT,
	GREP_NODE_AND,
	GREP_NODE_OR,
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
	struct grep_expr *pattern_expression;
	int prefix_length;
	regex_t regexp;
	unsigned linenum:1;
	unsigned invert:1;
	unsigned status_only:1;
	unsigned name_only:1;
	unsigned unmatch_name_only:1;
	unsigned count:1;
	unsigned word_regexp:1;
	unsigned fixed:1;
	unsigned all_match:1;
#define GREP_BINARY_DEFAULT	0
#define GREP_BINARY_NOMATCH	1
#define GREP_BINARY_TEXT	2
	unsigned binary:2;
	unsigned extended:1;
	unsigned relative:1;
	unsigned pathname:1;
	unsigned null_following_name:1;
	int color;
	char color_match[COLOR_MAXLEN];
	const char *color_external;
	int regflags;
	unsigned pre_context;
	unsigned post_context;
};

extern void append_grep_pattern(struct grep_opt *opt, const char *pat, const char *origin, int no, enum grep_pat_token t);
extern void append_header_grep_pattern(struct grep_opt *, enum grep_header_field, const char *);
extern void compile_grep_patterns(struct grep_opt *opt);
extern void free_grep_patterns(struct grep_opt *opt);
extern int grep_buffer(struct grep_opt *opt, const char *name, char *buf, unsigned long size);

#endif
