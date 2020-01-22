#ifndef LOG_TREE_H
#define LOG_TREE_H

#include "revision.h"

struct log_info {
	struct commit *commit, *parent;
};

struct decoration_filter {
	struct string_list *include_ref_pattern, *exclude_ref_pattern;
};

int parse_decorate_color_config(const char *var, const char *slot_name, const char *value);
void init_log_tree_opt(struct rev_info *);
int log_tree_diff_flush(struct rev_info *);
int log_tree_commit(struct rev_info *, struct commit *);
int log_tree_opt_parse(struct rev_info *, const char **, int);
void show_log(struct rev_info *opt);
void format_decorations_extended(struct strbuf *sb, const struct commit *commit,
			     int use_color,
			     const char *prefix,
			     const char *separator,
			     const char *suffix);
#define format_decorations(strbuf, commit, color) \
			     format_decorations_extended((strbuf), (commit), (color), " (", ", ", ")")
void show_decorations(struct rev_info *opt, struct commit *commit);
void log_write_email_headers(struct rev_info *opt, struct commit *commit,
			     const char **extra_headers_p,
			     int *need_8bit_cte_p,
			     int maybe_multipart);
void load_ref_decorations(struct decoration_filter *filter, int flags);

#define FORMAT_PATCH_NAME_MAX 64
void fmt_output_commit(struct strbuf *, struct commit *, struct rev_info *);
void fmt_output_subject(struct strbuf *, const char *subject, struct rev_info *);
void fmt_output_email_subject(struct strbuf *, struct rev_info *);

#endif
