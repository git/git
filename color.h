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
int color_fprintf(FILE *fp, const char *color, const char *fmt, ...);
int color_fprintf_ln(FILE *fp, const char *color, const char *fmt, ...);
int color_fwrite_lines(FILE *fp, const char *color, size_t count, const char *buf);

#endif /* COLOR_H */
