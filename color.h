#ifndef COLOR_H
#define COLOR_H

/*  2 + (2 * num_attrs) + 8 + 1 + 8 + 'm' + NUL */
/* "\033[1;2;4;5;7;38;5;2xx;48;5;2xxm\0" */
/*
 * The maximum length of ANSI color sequence we would generate:
 * - leading ESC '['            2
 * - attr + ';'                 2 * 8 (e.g. "1;")
 * - fg color + ';'             9 (e.g. "38;5;2xx;")
 * - fg color + ';'             9 (e.g. "48;5;2xx;")
 * - terminating 'm' NUL        2
 *
 * The above overcounts attr (we only use 5 not 8) and one semicolon
 * but it is close enough.
 */
#define COLOR_MAXLEN 40

/*
 * IMPORTANT: Due to the way these color codes are emulated on Windows,
 * write them only using printf(), fprintf(), and fputs(). In particular,
 * do not use puts() or write().
 */
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

/*
 * This variable stores the value of color.ui
 */
extern int git_use_color_default;


/*
 * Use this instead of git_default_config if you need the value of color.ui.
 */
int git_color_default_config(const char *var, const char *value, void *cb);

int git_config_colorbool(const char *var, const char *value, int stdout_is_tty);
void color_parse(const char *value, const char *var, char *dst);
void color_parse_mem(const char *value, int len, const char *var, char *dst);
__attribute__((format (printf, 3, 4)))
int color_fprintf(FILE *fp, const char *color, const char *fmt, ...);
__attribute__((format (printf, 3, 4)))
int color_fprintf_ln(FILE *fp, const char *color, const char *fmt, ...);

#endif /* COLOR_H */
