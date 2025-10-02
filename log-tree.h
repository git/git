#ifndef LOG_TREE_H
#define LOG_TREE_H

#include "color.h"

struct rev_info;

struct log_info {
	struct commit *commit, *parent;
};

struct decoration_filter {
	struct string_list *include_ref_pattern;
	struct string_list *exclude_ref_pattern;
	struct string_list *exclude_ref_config_pattern;
};

struct decoration_options {
	char *prefix;
	char *suffix;
	char *separator;
	char *pointer;
	char *tag;
};

int parse_decorate_color_config(const char *var, const char *slot_name, const char *value);
int log_tree_diff_flush(struct rev_info *);
int log_tree_commit(struct rev_info *, struct commit *);
void show_log(struct rev_info *opt);
void format_decorations(struct strbuf *sb, const struct commit *commit,
			enum git_colorbool use_color, const struct decoration_options *opts);
void show_decorations(struct rev_info *opt, struct commit *commit);
void log_write_email_headers(struct rev_info *opt, struct commit *commit,
			     char **extra_headers_p,
			     int *need_8bit_cte_p,
			     int maybe_multipart);
void load_ref_decorations(struct decoration_filter *filter, int flags);
void load_branch_decorations(void);

void fmt_output_commit(struct strbuf *, struct commit *, struct rev_info *);
void fmt_output_subject(struct strbuf *, const char *subject, struct rev_info *);
void fmt_output_email_subject(struct strbuf *, struct rev_info *);

#endif
