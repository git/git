#ifndef GREP_H
#define GREP_H

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

struct grep_pat {
	struct grep_pat *next;
	const char *origin;
	int no;
	enum grep_pat_token token;
	const char *pattern;
	regex_t regexp;
};

enum grep_expr_node {
	GREP_NODE_ATOM,
	GREP_NODE_NOT,
	GREP_NODE_AND,
	GREP_NODE_OR,
};

struct grep_expr {
	enum grep_expr_node node;
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
#define GREP_BINARY_DEFAULT	0
#define GREP_BINARY_NOMATCH	1
#define GREP_BINARY_TEXT	2
	unsigned binary:2;
	unsigned extended:1;
	unsigned relative:1;
	unsigned pathname:1;
	int regflags;
	unsigned pre_context;
	unsigned post_context;
};

extern void append_grep_pattern(struct grep_opt *opt, const char *pat, const char *origin, int no, enum grep_pat_token t);
extern void compile_grep_patterns(struct grep_opt *opt);
extern int grep_buffer(struct grep_opt *opt, const char *name, char *buf, unsigned long size);

#endif
