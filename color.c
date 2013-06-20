#include "cache.h"
#include "color.h"

static int git_use_color_default = GIT_COLOR_AUTO;
int color_stdout_is_tty = -1;

/*
 * The list of available column colors.
 */
const char *column_colors_ansi[] = {
	GIT_COLOR_RED,
	GIT_COLOR_GREEN,
	GIT_COLOR_YELLOW,
	GIT_COLOR_BLUE,
	GIT_COLOR_MAGENTA,
	GIT_COLOR_CYAN,
	GIT_COLOR_BOLD_RED,
	GIT_COLOR_BOLD_GREEN,
	GIT_COLOR_BOLD_YELLOW,
	GIT_COLOR_BOLD_BLUE,
	GIT_COLOR_BOLD_MAGENTA,
	GIT_COLOR_BOLD_CYAN,
	GIT_COLOR_RESET,
};

/* Ignore the RESET at the end when giving the size */
const int column_colors_ansi_max = ARRAY_SIZE(column_colors_ansi) - 1;

static int parse_color(const char *name, int len)
{
	static const char * const color_names[] = {
		"normal", "black", "red", "green", "yellow",
		"blue", "magenta", "cyan", "white"
	};
	char *end;
	int i;
	for (i = 0; i < ARRAY_SIZE(color_names); i++) {
		const char *str = color_names[i];
		if (!strncasecmp(name, str, len) && !str[len])
			return i - 1;
	}
	i = strtol(name, &end, 10);
	if (end - name == len && i >= -1 && i <= 255)
		return i;
	return -2;
}

static int parse_attr(const char *name, int len)
{
	static const int attr_values[] = { 1, 2, 4, 5, 7 };
	static const char * const attr_names[] = {
		"bold", "dim", "ul", "blink", "reverse"
	};
	int i;
	for (i = 0; i < ARRAY_SIZE(attr_names); i++) {
		const char *str = attr_names[i];
		if (!strncasecmp(name, str, len) && !str[len])
			return attr_values[i];
	}
	return -1;
}

void color_parse(const char *value, const char *var, char *dst)
{
	color_parse_mem(value, strlen(value), var, dst);
}

void color_parse_mem(const char *value, int value_len, const char *var,
		char *dst)
{
	const char *ptr = value;
	int len = value_len;
	unsigned int attr = 0;
	int fg = -2;
	int bg = -2;

	if (!strncasecmp(value, "reset", len)) {
		strcpy(dst, GIT_COLOR_RESET);
		return;
	}

	/* [fg [bg]] [attr]... */
	while (len > 0) {
		const char *word = ptr;
		int val, wordlen = 0;

		while (len > 0 && !isspace(word[wordlen])) {
			wordlen++;
			len--;
		}

		ptr = word + wordlen;
		while (len > 0 && isspace(*ptr)) {
			ptr++;
			len--;
		}

		val = parse_color(word, wordlen);
		if (val >= -1) {
			if (fg == -2) {
				fg = val;
				continue;
			}
			if (bg == -2) {
				bg = val;
				continue;
			}
			goto bad;
		}
		val = parse_attr(word, wordlen);
		if (0 <= val)
			attr |= (1 << val);
		else
			goto bad;
	}

	if (attr || fg >= 0 || bg >= 0) {
		int sep = 0;
		int i;

		*dst++ = '\033';
		*dst++ = '[';

		for (i = 0; attr; i++) {
			unsigned bit = (1 << i);
			if (!(attr & bit))
				continue;
			attr &= ~bit;
			if (sep++)
				*dst++ = ';';
			*dst++ = '0' + i;
		}
		if (fg >= 0) {
			if (sep++)
				*dst++ = ';';
			if (fg < 8) {
				*dst++ = '3';
				*dst++ = '0' + fg;
			} else {
				dst += sprintf(dst, "38;5;%d", fg);
			}
		}
		if (bg >= 0) {
			if (sep++)
				*dst++ = ';';
			if (bg < 8) {
				*dst++ = '4';
				*dst++ = '0' + bg;
			} else {
				dst += sprintf(dst, "48;5;%d", bg);
			}
		}
		*dst++ = 'm';
	}
	*dst = 0;
	return;
bad:
	die("bad color value '%.*s' for variable '%s'", value_len, value, var);
}

int git_config_colorbool(const char *var, const char *value)
{
	if (value) {
		if (!strcasecmp(value, "never"))
			return 0;
		if (!strcasecmp(value, "always"))
			return 1;
		if (!strcasecmp(value, "auto"))
			return GIT_COLOR_AUTO;
	}

	if (!var)
		return -1;

	/* Missing or explicit false to turn off colorization */
	if (!git_config_bool(var, value))
		return 0;

	/* any normal truth value defaults to 'auto' */
	return GIT_COLOR_AUTO;
}

static int check_auto_color(void)
{
	if (color_stdout_is_tty < 0)
		color_stdout_is_tty = isatty(1);
	if (color_stdout_is_tty || (pager_in_use() && pager_use_color)) {
		char *term = getenv("TERM");
		if (term && strcmp(term, "dumb"))
			return 1;
	}
	return 0;
}

int want_color(int var)
{
	static int want_auto = -1;

	if (var < 0)
		var = git_use_color_default;

	if (var == GIT_COLOR_AUTO) {
		if (want_auto < 0)
			want_auto = check_auto_color();
		return want_auto;
	}
	return var;
}

int git_color_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "color.ui")) {
		git_use_color_default = git_config_colorbool(var, value);
		return 0;
	}

	return 0;
}

int git_color_default_config(const char *var, const char *value, void *cb)
{
	if (git_color_config(var, value, cb) < 0)
		return -1;

	return git_default_config(var, value, cb);
}

void color_print_strbuf(FILE *fp, const char *color, const struct strbuf *sb)
{
	if (*color)
		fprintf(fp, "%s", color);
	fprintf(fp, "%s", sb->buf);
	if (*color)
		fprintf(fp, "%s", GIT_COLOR_RESET);
}

static int color_vfprintf(FILE *fp, const char *color, const char *fmt,
		va_list args, const char *trail)
{
	int r = 0;

	if (*color)
		r += fprintf(fp, "%s", color);
	r += vfprintf(fp, fmt, args);
	if (*color)
		r += fprintf(fp, "%s", GIT_COLOR_RESET);
	if (trail)
		r += fprintf(fp, "%s", trail);
	return r;
}



int color_fprintf(FILE *fp, const char *color, const char *fmt, ...)
{
	va_list args;
	int r;
	va_start(args, fmt);
	r = color_vfprintf(fp, color, fmt, args, NULL);
	va_end(args);
	return r;
}

int color_fprintf_ln(FILE *fp, const char *color, const char *fmt, ...)
{
	va_list args;
	int r;
	va_start(args, fmt);
	r = color_vfprintf(fp, color, fmt, args, "\n");
	va_end(args);
	return r;
}

int color_is_nil(const char *c)
{
	return !strcmp(c, "NIL");
}
