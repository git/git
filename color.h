#ifndef COLOR_H
#define COLOR_H

/* "\033[1;38;5;2xx;48;5;2xxm\0" is 23 bytes */
#define COLOR_MAXLEN 24

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
#define GIT_COLOR_BG_RED	"\033[41m"

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
int color_fwrite_lines(FILE *fp, const char *color, size_t count, const char *buf);

#endif /* COLOR_H */
