#ifndef COLOR_H
#define COLOR_H

/* "\033[1;38;5;2xx;48;5;2xxm\0" is 23 bytes */
#define COLOR_MAXLEN 24

int git_config_colorbool(const char *var, const char *value, int stdout_is_tty);
void color_parse(const char *var, const char *value, char *dst);
int color_fprintf(FILE *fp, const char *color, const char *fmt, ...);
int color_fprintf_ln(FILE *fp, const char *color, const char *fmt, ...);

#endif /* COLOR_H */
