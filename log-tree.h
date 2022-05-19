#ifndef LOG_TREE_H
#define LOG_TREE_H

#include "revision.h"

struct log_info {
	struct cummit *cummit, *parent;
};

struct decoration_filter {
	struct string_list *include_ref_pattern;
	struct string_list *exclude_ref_pattern;
	struct string_list *exclude_ref_config_pattern;
};

int parse_decorate_color_config(const char *var, const char *slot_name, const char *value);
int log_tree_diff_flush(struct rev_info *);
int log_tree_cummit(struct rev_info *, struct cummit *);
void show_log(struct rev_info *opt);
void format_decorations_extended(struct strbuf *sb, const struct cummit *cummit,
			     int use_color,
			     const char *prefix,
			     const char *separator,
			     const char *suffix);
#define format_decorations(strbuf, cummit, color) \
			     format_decorations_extended((strbuf), (cummit), (color), " (", ", ", ")")
void show_decorations(struct rev_info *opt, struct cummit *cummit);
void log_write_email_headers(struct rev_info *opt, struct cummit *cummit,
			     const char **extra_headers_p,
			     int *need_8bit_cte_p,
			     int maybe_multipart);
void load_ref_decorations(struct decoration_filter *filter, int flags);

void fmt_output_cummit(struct strbuf *, struct cummit *, struct rev_info *);
void fmt_output_subject(struct strbuf *, const char *subject, struct rev_info *);
void fmt_output_email_subject(struct strbuf *, struct rev_info *);

#endif
