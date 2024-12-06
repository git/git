#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "config.h"
#include "color.h"
#include "editor.h"
#include "gettext.h"
#include "hex-ll.h"
#include "pager.h"
#include "strbuf.h"

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

enum {
	COLOR_BACKGROUND_OFFSET = 10,
	COLOR_FOREGROUND_ANSI = 30,
	COLOR_FOREGROUND_RGB = 38,
	COLOR_FOREGROUND_256 = 38,
	COLOR_FOREGROUND_BRIGHT_ANSI = 90,
};

/* Ignore the RESET at the end when giving the size */
const int column_colors_ansi_max = ARRAY_SIZE(column_colors_ansi) - 1;

/* An individual foreground or background color. */
struct color {
	enum {
		COLOR_UNSPECIFIED = 0,
		COLOR_NORMAL,
		COLOR_ANSI, /* basic 0-7 ANSI colors + "default" (value = 9) */
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

static int get_hex_color(const char **inp, int width, unsigned char *out)
{
	const char *in = *inp;
	unsigned int val;

	assert(width == 1 || width == 2);
	val = (hexval(in[0]) << 4) | hexval(in[width - 1]);
	if (val & ~0xff)
		return -1;
	*inp += width;
	*out = val;
	return 0;
}

/*
 * If an ANSI color is recognized in "name", fill "out" and return 0.
 * Otherwise, leave out unchanged and return -1.
 */
static int parse_ansi_color(struct color *out, const char *name, int len)
{
	/* Positions in array must match ANSI color codes */
	static const char * const color_names[] = {
		"black", "red", "green", "yellow",
		"blue", "magenta", "cyan", "white"
	};
	int i;
	int color_offset = COLOR_FOREGROUND_ANSI;

	if (match_word(name, len, "default")) {
		/*
		 * Restores to the terminal's default color, which may not be
		 * the same as explicitly setting "white" or "black".
		 *
		 * ECMA-48 - Control Functions \
		 *  for Coded Character Sets, 5th edition (June 1991):
		 * > 39 default display colour (implementation-defined)
		 * > 49 default background colour (implementation-defined)
		 *
		 * Although not supported /everywhere/--according to terminfo,
		 * some terminals define "op" (original pair) as a blunt
		 * "set to white on black", or even "send full SGR reset"--
		 * it's standard and well-supported enough that if a user
		 * asks for it in their config this will do the right thing.
		 */
		out->type = COLOR_ANSI;
		out->value = 9 + color_offset;
		return 0;
	}

	if (strncasecmp(name, "bright", 6) == 0) {
		color_offset = COLOR_FOREGROUND_BRIGHT_ANSI;
		name += 6;
		len -= 6;
	}
	for (i = 0; i < ARRAY_SIZE(color_names); i++) {
		if (match_word(name, len, color_names[i])) {
			out->type = COLOR_ANSI;
			out->value = i + color_offset;
			return 0;
		}
	}
	return -1;
}

static int parse_color(struct color *out, const char *name, int len)
{
	char *end;
	long val;

	/* First try the special word "normal"... */
	if (match_word(name, len, "normal")) {
		out->type = COLOR_NORMAL;
		return 0;
	}

	/* Try a 24- or 12-bit RGB value prefixed with '#' */
	if ((len == 7 || len == 4) && name[0] == '#') {
		int width_per_color = (len == 7) ? 2 : 1;
		const char *color = name + 1;

		if (!get_hex_color(&color, width_per_color, &out->red) &&
		    !get_hex_color(&color, width_per_color, &out->green) &&
		    !get_hex_color(&color, width_per_color, &out->blue)) {
			out->type = COLOR_RGB;
			return 0;
		}
	}

	/* Then pick from our human-readable color names... */
	if (parse_ansi_color(out, name, len) == 0) {
		return 0;
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
		/* Rewrite 0-7 as more-portable standard colors. */
		} else if (val < 8) {
			out->type = COLOR_ANSI;
			out->value = val + COLOR_FOREGROUND_ANSI;
			return 0;
		/* Rewrite 8-15 as more-portable aixterm colors. */
		} else if (val < 16) {
			out->type = COLOR_ANSI;
			out->value = val - 8 + COLOR_FOREGROUND_BRIGHT_ANSI;
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

/*
 * Write the ANSI color codes for "c" to "out"; the string should
 * already have the ANSI escape code in it. "out" should have enough
 * space in it to fit any color.
 */
static char *color_output(char *out, int len, const struct color *c, int background)
{
	int offset = 0;

	if (background)
		offset = COLOR_BACKGROUND_OFFSET;
	switch (c->type) {
	case COLOR_UNSPECIFIED:
	case COLOR_NORMAL:
		break;
	case COLOR_ANSI:
		out += xsnprintf(out, len, "%d", c->value + offset);
		break;
	case COLOR_256:
		out += xsnprintf(out, len, "%d;5;%d", COLOR_FOREGROUND_256 + offset,
				 c->value);
		break;
	case COLOR_RGB:
		out += xsnprintf(out, len, "%d;2;%d;%d;%d",
				 COLOR_FOREGROUND_RGB + offset,
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
	unsigned int has_reset = 0;
	unsigned int attr = 0;
	struct color fg = { COLOR_UNSPECIFIED };
	struct color bg = { COLOR_UNSPECIFIED };

	while (len > 0 && isspace(*ptr)) {
		ptr++;
		len--;
	}

	if (!len) {
		dst[0] = '\0';
		return 0;
	}

	/* [reset] [fg [bg]] [attr]... */
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

		if (match_word(word, wordlen, "reset")) {
			has_reset = 1;
			continue;
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
		BUG("color parsing ran out of space"); \
	*dst++ = (x); \
} while(0)

	if (has_reset || attr || !color_empty(&fg) || !color_empty(&bg)) {
		int sep = 0;
		int i;

		OUT('\033');
		OUT('[');

		if (has_reset)
			sep++;

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
			dst = color_output(dst, end - dst, &fg, 0);
		}
		if (!color_empty(&bg)) {
			if (sep++)
				OUT(';');
			dst = color_output(dst, end - dst, &bg, 1);
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

static int check_auto_color(int fd)
{
	static int color_stderr_is_tty = -1;
	int *is_tty_p = fd == 1 ? &color_stdout_is_tty : &color_stderr_is_tty;
	if (*is_tty_p < 0)
		*is_tty_p = isatty(fd);
	if (*is_tty_p || (fd == 1 && pager_in_use() && pager_use_color)) {
		if (!is_terminal_dumb())
			return 1;
	}
	return 0;
}

int want_color_fd(int fd, int var)
{
	/*
	 * NEEDSWORK: This function is sometimes used from multiple threads, and
	 * we end up using want_auto racily. That "should not matter" since
	 * we always write the same value, but it's still wrong. This function
	 * is listed in .tsan-suppressions for the time being.
	 */

	static int want_auto[3] = { -1, -1, -1 };

	if (fd < 1 || fd >= ARRAY_SIZE(want_auto))
		BUG("file descriptor out of range: %d", fd);

	if (var < 0)
		var = git_use_color_default;

	if (var == GIT_COLOR_AUTO) {
		if (want_auto[fd] < 0)
			want_auto[fd] = check_auto_color(fd);
		return want_auto[fd];
	}
	return var;
}

int git_color_config(const char *var, const char *value, void *cb UNUSED)
{
	if (!strcmp(var, "color.ui")) {
		git_use_color_default = git_config_colorbool(var, value);
		return 0;
	}

	return 0;
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
