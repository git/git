#ifndef COLOR_H
#define COLOR_H

struct strbuf;

/*
 * The maximum length of ANSI color sequence we would generate:
 * - leading ESC '['            2
 * - reset ';' .................1
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
#define GIT_COLOR_BLACK		"\033[30m"
#define GIT_COLOR_RED		"\033[31m"
#define GIT_COLOR_GREEN		"\033[32m"
#define GIT_COLOR_YELLOW	"\033[33m"
#define GIT_COLOR_BLUE		"\033[34m"
#define GIT_COLOR_MAGENTA	"\033[35m"
#define GIT_COLOR_CYAN		"\033[36m"
#define GIT_COLOR_WHITE		"\033[37m"
#define GIT_COLOR_DEFAULT	"\033[39m"
#define GIT_COLOR_BOLD_BLACK	"\033[1;30m"
#define GIT_COLOR_BOLD_RED	"\033[1;31m"
#define GIT_COLOR_BOLD_GREEN	"\033[1;32m"
#define GIT_COLOR_BOLD_YELLOW	"\033[1;33m"
#define GIT_COLOR_BOLD_BLUE	"\033[1;34m"
#define GIT_COLOR_BOLD_MAGENTA	"\033[1;35m"
#define GIT_COLOR_BOLD_CYAN	"\033[1;36m"
#define GIT_COLOR_BOLD_WHITE	"\033[1;37m"
#define GIT_COLOR_BOLD_DEFAULT	"\033[1;39m"
#define GIT_COLOR_FAINT_BLACK	"\033[2;30m"
#define GIT_COLOR_FAINT_RED	"\033[2;31m"
#define GIT_COLOR_FAINT_GREEN	"\033[2;32m"
#define GIT_COLOR_FAINT_YELLOW	"\033[2;33m"
#define GIT_COLOR_FAINT_BLUE	"\033[2;34m"
#define GIT_COLOR_FAINT_MAGENTA	"\033[2;35m"
#define GIT_COLOR_FAINT_CYAN	"\033[2;36m"
#define GIT_COLOR_FAINT_WHITE	"\033[2;37m"
#define GIT_COLOR_FAINT_DEFAULT	"\033[2;39m"
#define GIT_COLOR_BG_BLACK	"\033[40m"
#define GIT_COLOR_BG_RED	"\033[41m"
#define GIT_COLOR_BG_GREEN	"\033[42m"
#define GIT_COLOR_BG_YELLOW	"\033[43m"
#define GIT_COLOR_BG_BLUE	"\033[44m"
#define GIT_COLOR_BG_MAGENTA	"\033[45m"
#define GIT_COLOR_BG_CYAN	"\033[46m"
#define GIT_COLOR_BG_WHITE	"\033[47m"
#define GIT_COLOR_BG_DEFAULT	"\033[49m"
#define GIT_COLOR_FAINT		"\033[2m"
#define GIT_COLOR_FAINT_ITALIC	"\033[2;3m"
#define GIT_COLOR_REVERSE	"\033[7m"

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

/* Parse color config. */
int git_color_config(const char *var, const char *value, void *cb);

/*
 * Parse a config option, which can be a boolean or one of
 * "never", "auto", "always". Return a constant of
 * GIT_COLOR_NEVER for "never" or negative boolean,
 * GIT_COLOR_ALWAYS for "always" or a positive boolean,
 * and GIT_COLOR_AUTO for "auto".
 */
int git_config_colorbool(const char *var, const char *value);

/*
 * Return a boolean whether to use color, where the argument 'var' is
 * one of GIT_COLOR_UNKNOWN, GIT_COLOR_NEVER, GIT_COLOR_ALWAYS, GIT_COLOR_AUTO.
 */
int want_color_fd(int fd, int var);
#define want_color(colorbool) want_color_fd(1, (colorbool))
#define want_color_stderr(colorbool) want_color_fd(2, (colorbool))

/*
 * Translate a Git color from 'value' into a string that the terminal can
 * interpret and store it into 'dst'. The Git color values are of the form
 * "foreground [background] [attr]" where fore- and background can be a color
 * name ("red"), a RGB code (#0xFF0000) or a 256-color-mode from the terminal.
 */
int color_parse(const char *value, char *dst);
int color_parse_mem(const char *value, int len, char *dst);

/*
 * Output the formatted string in the specified color (and then reset to normal
 * color so subsequent output is uncolored). Omits the color encapsulation if
 * `color` is NULL. The `color_fprintf_ln` prints a new line after resetting
 * the color.  The `color_print_strbuf` prints the contents of the given
 * strbuf (BUG: but only up to its first NUL character).
 */
__attribute__((format (printf, 3, 4)))
int color_fprintf(FILE *fp, const char *color, const char *fmt, ...);
__attribute__((format (printf, 3, 4)))
int color_fprintf_ln(FILE *fp, const char *color, const char *fmt, ...);
void color_print_strbuf(FILE *fp, const char *color, const struct strbuf *sb);

/*
 * Check if the given color is GIT_COLOR_NIL that means "no color selected".
 * The caller needs to replace the color with the actual desired color.
 */
int color_is_nil(const char *color);

#endif /* COLOR_H */
