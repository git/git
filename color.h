#ifndef COLOR_H
#define COLOR_H

struct strbuf;

/*
 * The maximum length of ANSI color sequence we would generate:
 * - leading ESC '['            2
 * - attr + ';'                 2 * num_attr (e.g. "1;")
 * - no-attr + ';'              3 * num_attr (e.g. "22;")
 * - fg color + ';'             17 (e.g. "38;2;255;255;255;")
 * - bg color + ';'             17 (e.g. "48;2;255;255;255;")
 * - terminating 'm' NUL        2
 *
 * The above overcounts by one semicolon but it is close enough.
 *
 * The space for attributes is also slightly overallocated, as
 * the negation for some attributes is the same (e.g., nobold and nodim).
 *
 * We allocate space for 7 attributes.
 */
#define COLOR_MAXLEN 75

#define GIT_COLOR_NORMAL	""
#define GIT_COLOR_RESET		"\033[m"
#define GIT_COLOR_BOLD		"\033[1m"
#define GIT_COLOR_RED		"\033[31m"
#define GIT_COLOR_GREEN		"\033[32m"
#define GIT_COLOR_YELLOW	"\033[33m"
#define GIT_COLOR_BLUE		"\033[34m"
#define GIT_COLOR_MAGENTA	"\033[35m"
#define GIT_COLOR_CYAN		"\033[36m"
#define GIT_COLOR_BOLD_RED	"\033[1;31m"
#define GIT_COLOR_BOLD_GREEN	"\033[1;32m"
#define GIT_COLOR_BOLD_YELLOW	"\033[1;33m"
#define GIT_COLOR_BOLD_BLUE	"\033[1;34m"
#define GIT_COLOR_BOLD_MAGENTA	"\033[1;35m"
#define GIT_COLOR_BOLD_CYAN	"\033[1;36m"
#define GIT_COLOR_BG_RED	"\033[41m"
#define GIT_COLOR_BG_GREEN	"\033[42m"
#define GIT_COLOR_BG_YELLOW	"\033[43m"
#define GIT_COLOR_BG_BLUE	"\033[44m"
#define GIT_COLOR_BG_MAGENTA	"\033[45m"
#define GIT_COLOR_BG_CYAN	"\033[46m"

/* A special value meaning "no color selected" */
#define GIT_COLOR_NIL "NIL"

/*
 * The first three are chosen to match common usage in the code, and what is
 * returned from git_config_colorbool. The "auto" value can be returned from
 * config_colorbool, and will be converted by want_color() into either 0 or 1.
 */
#define GIT_COLOR_UNKNOWN -1
#define GIT_COLOR_NEVER  0
#define GIT_COLOR_ALWAYS 1
#define GIT_COLOR_AUTO   2

/* A default list of colors to use for commit graphs and show-branch output */
extern const char *column_colors_ansi[];
extern const int column_colors_ansi_max;

/*
 * Generally the color code will lazily figure this out itself, but
 * this provides a mechanism for callers to override autodetection.
 */
extern int color_stdout_is_tty;

/*
 * Use the first one if you need only color config; the second is a convenience
 * if you are just going to change to git_default_config, too.
 */
int git_color_config(const char *var, const char *value, void *cb);
int git_color_default_config(const char *var, const char *value, void *cb);

/*
 * Set the color buffer (which must be COLOR_MAXLEN bytes)
 * to the raw color bytes; this is useful for initializing
 * default color variables.
 */
void color_set(char *dst, const char *color_bytes);

int git_config_colorbool(const char *var, const char *value);
int want_color(int var);
int color_parse(const char *value, char *dst);
int color_parse_mem(const char *value, int len, char *dst);
__attribute__((format (printf, 3, 4)))
int color_fprintf(FILE *fp, const char *color, const char *fmt, ...);
__attribute__((format (printf, 3, 4)))
int color_fprintf_ln(FILE *fp, const char *color, const char *fmt, ...);
void color_print_strbuf(FILE *fp, const char *color, const struct strbuf *sb);

int color_is_nil(const char *color);

#endif /* COLOR_H */
