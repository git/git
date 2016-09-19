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

/* An individual foreground or background color. */
struct color {
	enum {
		COLOR_UNSPECIFIED = 0,
		COLOR_NORMAL,
		COLOR_ANSI, /* basic 0-7 ANSI colors */
		COLOR_256,
		COLOR_RGB
	} type;
	/* The numeric value for ANSI and 256-color modes */
	unsigned char value;
	/* 24-bit RGB color values */
	unsigned char red, green, blue;
};

/*
 * "word" is a buffer of length "len"; does it match the NUL-terminated
 * "match" exactly?
 */
static int match_word(const char *word, int len, const char *match)
{
	return !strncasecmp(word, match, len) && !match[len];
}

static int get_hex_color(const char *in, unsigned char *out)
{
	unsigned int val;
	val = (hexval(in[0]) << 4) | hexval(in[1]);
	if (val & ~0xff)
		return -1;
	*out = val;
	return 0;
}

static int parse_color(struct color *out, const char *name, int len)
{
	/* Positions in array must match ANSI color codes */
	static const char * const color_names[] = {
		"black", "red", "green", "yellow",
		"blue", "magenta", "cyan", "white"
	};
	char *end;
	int i;
	long val;

	/* First try the special word "normal"... */
	if (match_word(name, len, "normal")) {
		out->type = COLOR_NORMAL;
		return 0;
	}

	/* Try a 24-bit RGB value */
	if (len == 7 && name[0] == '#') {
		if (!get_hex_color(name + 1, &out->red) &&
		    !get_hex_color(name + 3, &out->green) &&
		    !get_hex_color(name + 5, &out->blue)) {
			out->type = COLOR_RGB;
			return 0;
		}
	}

	/* Then pick from our human-readable color names... */
	for (i = 0; i < ARRAY_SIZE(color_names); i++) {
		if (match_word(name, len, color_names[i])) {
			out->type = COLOR_ANSI;
			out->value = i;
			return 0;
		}
	}

	/* And finally try a literal 256-color-mode number */
	val = strtol(name, &end, 10);
	if (end - name == len) {
		/*
		 * Allow "-1" as an alias for "normal", but other negative
		 * numbers are bogus.
		 */
		if (val < -1)
			; /* fall through to error */
		else if (val < 0) {
			out->type = COLOR_NORMAL;
			return 0;
		/* Rewrite low numbers as more-portable standard colors. */
		} else if (val < 8) {
			out->type = COLOR_ANSI;
			out->value = val;
			return 0;
		} else if (val < 256) {
			out->type = COLOR_256;
			out->value = val;
			return 0;
		}
	}

	return -1;
}

static int parse_attr(const char *name, size_t len)
{
	static const struct {
		const char *name;
		size_t len;
		int val, neg;
	} attrs[] = {
#define ATTR(x, val, neg) { (x), sizeof(x)-1, (val), (neg) }
		ATTR("bold",      1, 22),
		ATTR("dim",       2, 22),
		ATTR("italic",    3, 23),
		ATTR("ul",        4, 24),
		ATTR("blink",     5, 25),
		ATTR("reverse",   7, 27),
		ATTR("strike",    9, 29)
#undef ATTR
	};
	int negate = 0;
	int i;

	if (skip_prefix_mem(name, len, "no", &name, &len)) {
		skip_prefix_mem(name, len, "-", &name, &len);
		negate = 1;
	}

	for (i = 0; i < ARRAY_SIZE(attrs); i++) {
		if (attrs[i].len == len && !memcmp(attrs[i].name, name, len))
			return negate ? attrs[i].neg : attrs[i].val;
	}
	return -1;
}

int color_parse(const char *value, char *dst)
{
	return color_parse_mem(value, strlen(value), dst);
}

void color_set(char *dst, const char *color_bytes)
{
	xsnprintf(dst, COLOR_MAXLEN, "%s", color_bytes);
}

/*
 * Write the ANSI color codes for "c" to "out"; the string should
 * already have the ANSI escape code in it. "out" should have enough
 * space in it to fit any color.
 */
static char *color_output(char *out, int len, const struct color *c, char type)
{
	switch (c->type) {
	case COLOR_UNSPECIFIED:
	case COLOR_NORMAL:
		break;
	case COLOR_ANSI:
		if (len < 2)
			die("BUG: color parsing ran out of space");
		*out++ = type;
		*out++ = '0' + c->value;
		break;
	case COLOR_256:
		out += xsnprintf(out, len, "%c8;5;%d", type, c->value);
		break;
	case COLOR_RGB:
		out += xsnprintf(out, len, "%c8;2;%d;%d;%d", type,
				 c->red, c->green, c->blue);
		break;
	}
	return out;
}

static int color_empty(const struct color *c)
{
	return c->type <= COLOR_NORMAL;
}

int color_parse_mem(const char *value, int value_len, char *dst)
{
	const char *ptr = value;
	int len = value_len;
	char *end = dst + COLOR_MAXLEN;
	unsigned int attr = 0;
	struct color fg = { COLOR_UNSPECIFIED };
	struct color bg = { COLOR_UNSPECIFIED };

	if (!strncasecmp(value, "reset", len)) {
		xsnprintf(dst, end - dst, GIT_COLOR_RESET);
		return 0;
	}

	/* [fg [bg]] [attr]... */
	while (len > 0) {
		const char *word = ptr;
		struct color c = { COLOR_UNSPECIFIED };
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

		if (!parse_color(&c, word, wordlen)) {
			if (fg.type == COLOR_UNSPECIFIED) {
				fg = c;
				continue;
			}
			if (bg.type == COLOR_UNSPECIFIED) {
				bg = c;
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

#undef OUT
#define OUT(x) do { \
	if (dst == end) \
		die("BUG: color parsing ran out of space"); \
	*dst++ = (x); \
} while(0)

	if (attr || !color_empty(&fg) || !color_empty(&bg)) {
		int sep = 0;
		int i;

		OUT('\033');
		OUT('[');

		for (i = 0; attr; i++) {
			unsigned bit = (1 << i);
			if (!(attr & bit))
				continue;
			attr &= ~bit;
			if (sep++)
				OUT(';');
			dst += xsnprintf(dst, end - dst, "%d", i);
		}
		if (!color_empty(&fg)) {
			if (sep++)
				OUT(';');
			/* foreground colors are all in the 3x range */
			dst = color_output(dst, end - dst, &fg, '3');
		}
		if (!color_empty(&bg)) {
			if (sep++)
				OUT(';');
			/* background colors are all in the 4x range */
			dst = color_output(dst, end - dst, &bg, '4');
		}
		OUT('m');
	}
	OUT(0);
	return 0;
bad:
	return error(_("invalid color value: %.*s"), value_len, value);
#undef OUT
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
