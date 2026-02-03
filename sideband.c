#define USE_THE_REPOSITORY_VARIABLE
#define DISABLE_SIGN_COMPARE_WARNINGS

#include "git-compat-util.h"
#include "color.h"
#include "config.h"
#include "editor.h"
#include "gettext.h"
#include "sideband.h"
#include "help.h"
#include "pkt-line.h"
#include "write-or-die.h"
#include "urlmatch.h"

struct keyword_entry {
	/*
	 * We use keyword as config key so it should be a single alphanumeric word.
	 */
	const char *keyword;
	char color[COLOR_MAXLEN];
};

static struct keyword_entry keywords[] = {
	{ "hint",	GIT_COLOR_YELLOW },
	{ "warning",	GIT_COLOR_BOLD_YELLOW },
	{ "success",	GIT_COLOR_BOLD_GREEN },
	{ "error",	GIT_COLOR_BOLD_RED },
};

static enum {
	ALLOW_CONTROL_SEQUENCES_UNSET = -1,
	ALLOW_NO_CONTROL_CHARACTERS   = 0,
	ALLOW_ANSI_COLOR_SEQUENCES    = 1<<0,
	ALLOW_ANSI_CURSOR_MOVEMENTS   = 1<<1,
	ALLOW_ANSI_ERASE              = 1<<2,
	ALLOW_ALL_CONTROL_CHARACTERS  = 1<<3,
#ifdef WITH_BREAKING_CHANGES
	ALLOW_DEFAULT_ANSI_SEQUENCES  = ALLOW_ANSI_COLOR_SEQUENCES,
#else
	ALLOW_DEFAULT_ANSI_SEQUENCES  = ALLOW_ALL_CONTROL_CHARACTERS,
#endif
} allow_control_characters = ALLOW_CONTROL_SEQUENCES_UNSET;

static inline int skip_prefix_in_csv(const char *value, const char *prefix,
				     const char **out)
{
	if (!skip_prefix(value, prefix, &value) ||
	    (*value && *value != ','))
		return 0;
	*out = value + !!*value;
	return 1;
}

int sideband_allow_control_characters_config(const char *var, const char *value)
{
	switch (git_parse_maybe_bool(value)) {
	case 0:
		allow_control_characters = ALLOW_NO_CONTROL_CHARACTERS;
		return 0;
	case 1:
		allow_control_characters = ALLOW_ALL_CONTROL_CHARACTERS;
		return 0;
	default:
		break;
	}

	allow_control_characters = ALLOW_NO_CONTROL_CHARACTERS;
	while (*value) {
		if (skip_prefix_in_csv(value, "default", &value))
			allow_control_characters |= ALLOW_DEFAULT_ANSI_SEQUENCES;
		else if (skip_prefix_in_csv(value, "color", &value))
			allow_control_characters |= ALLOW_ANSI_COLOR_SEQUENCES;
		else if (skip_prefix_in_csv(value, "cursor", &value))
			allow_control_characters |= ALLOW_ANSI_CURSOR_MOVEMENTS;
		else if (skip_prefix_in_csv(value, "erase", &value))
			allow_control_characters |= ALLOW_ANSI_ERASE;
		else if (skip_prefix_in_csv(value, "true", &value))
			allow_control_characters = ALLOW_ALL_CONTROL_CHARACTERS;
		else if (skip_prefix_in_csv(value, "false", &value))
			allow_control_characters = ALLOW_NO_CONTROL_CHARACTERS;
		else
			warning(_("unrecognized value for '%s': '%s'"), var, value);
	}
	return 0;
}

static int sideband_config_callback(const char *var, const char *value,
				    const struct config_context *ctx UNUSED,
				    void *data UNUSED)
{
	if (!strcmp(var, "sideband.allowcontrolcharacters"))
		return sideband_allow_control_characters_config(var, value);

	return 0;
}

void sideband_apply_url_config(const char *url)
{
	struct urlmatch_config config = URLMATCH_CONFIG_INIT;
	char *normalized_url;

	if (!url)
		BUG("must not call sideband_apply_url_config(NULL)");

	config.section = "sideband";
	config.collect_fn = sideband_config_callback;

	normalized_url = url_normalize(url, &config.url);
	repo_config(the_repository, urlmatch_config_entry, &config);
	free(normalized_url);
	string_list_clear(&config.vars, 1);
	urlmatch_config_release(&config);
}

/* Returns a color setting (GIT_COLOR_NEVER, etc). */
static enum git_colorbool use_sideband_colors(void)
{
	static enum git_colorbool use_sideband_colors_cached = GIT_COLOR_UNKNOWN;

	const char *key = "color.remote";
	struct strbuf sb = STRBUF_INIT;
	const char *value;
	int i;

	if (use_sideband_colors_cached != GIT_COLOR_UNKNOWN)
		return use_sideband_colors_cached;

	if (allow_control_characters == ALLOW_CONTROL_SEQUENCES_UNSET) {
		if (!repo_config_get_value(the_repository, "sideband.allowcontrolcharacters", &value))
			sideband_allow_control_characters_config("sideband.allowcontrolcharacters", value);

		if (allow_control_characters == ALLOW_CONTROL_SEQUENCES_UNSET)
			allow_control_characters = ALLOW_DEFAULT_ANSI_SEQUENCES;
	}

	if (!repo_config_get_string_tmp(the_repository, key, &value))
		use_sideband_colors_cached = git_config_colorbool(key, value);
	else if (!repo_config_get_string_tmp(the_repository, "color.ui", &value))
		use_sideband_colors_cached = git_config_colorbool("color.ui", value);
	else
		use_sideband_colors_cached = GIT_COLOR_AUTO;

	for (i = 0; i < ARRAY_SIZE(keywords); i++) {
		strbuf_reset(&sb);
		strbuf_addf(&sb, "%s.%s", key, keywords[i].keyword);
		if (repo_config_get_string_tmp(the_repository, sb.buf, &value))
			continue;
		color_parse(value, keywords[i].color);
	}

	strbuf_release(&sb);
	return use_sideband_colors_cached;
}

void list_config_color_sideband_slots(struct string_list *list, const char *prefix)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(keywords); i++)
		list_config_item(list, prefix, keywords[i].keyword);
}

static int handle_ansi_sequence(struct strbuf *dest, const char *src, int n)
{
	int i;

	/*
	 * Valid ANSI color sequences are of the form
	 *
	 * ESC [ [<n> [; <n>]*] m
	 *
	 * These are part of the Select Graphic Rendition sequences which
	 * contain more than just color sequences, for more details see
	 * https://en.wikipedia.org/wiki/ANSI_escape_code#SGR.
	 *
	 * The cursor movement sequences are:
	 *
	 * ESC [ n A - Cursor up n lines (CUU)
	 * ESC [ n B - Cursor down n lines (CUD)
	 * ESC [ n C - Cursor forward n columns (CUF)
	 * ESC [ n D - Cursor back n columns (CUB)
	 * ESC [ n E - Cursor next line, beginning (CNL)
	 * ESC [ n F - Cursor previous line, beginning (CPL)
	 * ESC [ n G - Cursor to column n (CHA)
	 * ESC [ n ; m H - Cursor position (row n, col m) (CUP)
	 * ESC [ n ; m f - Same as H (HVP)
	 *
	 * The sequences to erase characters are:
	 *
	 *
	 * ESC [ 0 J - Clear from cursor to end of screen (ED)
	 * ESC [ 1 J - Clear from cursor to beginning of screen (ED)
	 * ESC [ 2 J - Clear entire screen (ED)
	 * ESC [ 3 J - Clear entire screen + scrollback (ED) - xterm extension
	 * ESC [ 0 K - Clear from cursor to end of line (EL)
	 * ESC [ 1 K - Clear from cursor to beginning of line (EL)
	 * ESC [ 2 K - Clear entire line (EL)
	 * ESC [ n M - Delete n lines (DL)
	 * ESC [ n P - Delete n characters (DCH)
	 * ESC [ n X - Erase n characters (ECH)
	 *
	 * For a comprehensive list of common ANSI Escape sequences, see
	 * https://www.xfree86.org/current/ctlseqs.html
	 */

	if (n < 3 || src[0] != '\x1b' || src[1] != '[')
		return 0;

	for (i = 2; i < n; i++) {
		if (((allow_control_characters & ALLOW_ANSI_COLOR_SEQUENCES) &&
		     src[i] == 'm') ||
		    ((allow_control_characters & ALLOW_ANSI_CURSOR_MOVEMENTS) &&
		     strchr("ABCDEFGHf", src[i])) ||
		    ((allow_control_characters & ALLOW_ANSI_ERASE) &&
		     strchr("JKMPX", src[i]))) {
			strbuf_add(dest, src, i + 1);
			return i;
		}
		if (!isdigit(src[i]) && src[i] != ';')
			break;
	}

	return 0;
}

static void strbuf_add_sanitized(struct strbuf *dest, const char *src, int n)
{
	int i;

	if ((allow_control_characters & ALLOW_ALL_CONTROL_CHARACTERS)) {
		strbuf_add(dest, src, n);
		return;
	}

	strbuf_grow(dest, n);
	for (; n && *src; src++, n--) {
		if (!iscntrl(*src) || *src == '\t' || *src == '\n') {
			strbuf_addch(dest, *src);
		} else if (allow_control_characters != ALLOW_NO_CONTROL_CHARACTERS &&
			   (i = handle_ansi_sequence(dest, src, n))) {
			src += i;
			n -= i;
		} else {
			strbuf_addch(dest, '^');
			strbuf_addch(dest, *src == 0x7f ? '?' : 0x40 + *src);
		}
	}
}

/*
 * Optionally highlight one keyword in remote output if it appears at the start
 * of the line. This should be called for a single line only, which is
 * passed as the first N characters of the SRC array.
 *
 * It is fine to use "int n" here instead of "size_t n" as all calls to this
 * function pass an 'int' parameter. Additionally, the buffer involved in
 * storing these 'int' values takes input from a packet via the pkt-line
 * interface, which is capable of transferring only 64kB at a time.
 */
static void maybe_colorize_sideband(struct strbuf *dest, const char *src, int n)
{
	int i;

	if (!want_color_stderr(use_sideband_colors())) {
		strbuf_add_sanitized(dest, src, n);
		return;
	}

	while (0 < n && isspace(*src)) {
		strbuf_addch(dest, *src);
		src++;
		n--;
	}

	for (i = 0; i < ARRAY_SIZE(keywords); i++) {
		struct keyword_entry *p = keywords + i;
		int len = strlen(p->keyword);

		if (n < len)
			continue;
		/*
		 * Match case insensitively, so we colorize output from existing
		 * servers regardless of the case that they use for their
		 * messages. We only highlight the word precisely, so
		 * "successful" stays uncolored.
		 */
		if (!strncasecmp(p->keyword, src, len) &&
		    (len == n || !isalnum(src[len]))) {
			strbuf_addstr(dest, p->color);
			strbuf_add(dest, src, len);
			strbuf_addstr(dest, GIT_COLOR_RESET);
			n -= len;
			src += len;
			break;
		}
	}

	strbuf_add_sanitized(dest, src, n);
}


#define DISPLAY_PREFIX "remote: "

#define ANSI_SUFFIX "\033[K"
#define DUMB_SUFFIX "        "

int demultiplex_sideband(const char *me, int status,
			 char *buf, int len,
			 int die_on_error,
			 struct strbuf *scratch,
			 enum sideband_type *sideband_type)
{
	static const char *suffix;
	const char *b, *brk;
	int band;

	if (!suffix) {
		if (isatty(2) && !is_terminal_dumb())
			suffix = ANSI_SUFFIX;
		else
			suffix = DUMB_SUFFIX;
	}

	if (status == PACKET_READ_EOF) {
		strbuf_addf(scratch,
			    "%s%s: unexpected disconnect while reading sideband packet",
			    scratch->len ? "\n" : "", me);
		*sideband_type = SIDEBAND_PROTOCOL_ERROR;
		goto cleanup;
	}

	if (len < 0)
		BUG("negative length on non-eof packet read");

	if (len == 0) {
		if (status == PACKET_READ_NORMAL) {
			strbuf_addf(scratch,
				    "%s%s: protocol error: missing sideband designator",
				    scratch->len ? "\n" : "", me);
			*sideband_type = SIDEBAND_PROTOCOL_ERROR;
		} else {
			/* covers flush, delim, etc */
			*sideband_type = SIDEBAND_FLUSH;
		}
		goto cleanup;
	}

	band = buf[0] & 0xff;
	buf[len] = '\0';
	len--;
	switch (band) {
	case 3:
		if (die_on_error)
			die(_("remote error: %s"), buf + 1);
		strbuf_addf(scratch, "%s%s", scratch->len ? "\n" : "",
			    DISPLAY_PREFIX);
		maybe_colorize_sideband(scratch, buf + 1, len);

		*sideband_type = SIDEBAND_REMOTE_ERROR;
		break;
	case 2:
		b = buf + 1;

		/*
		 * Append a suffix to each nonempty line to clear the
		 * end of the screen line.
		 *
		 * The output is accumulated in a buffer and
		 * each line is printed to stderr using
		 * write(2) to ensure inter-process atomicity.
		 */
		while ((brk = strpbrk(b, "\n\r"))) {
			int linelen = brk - b;

			/*
			 * For message across packet boundary, there would have
			 * a nonempty "scratch" buffer from last call of this
			 * function, and there may have a leading CR/LF in "buf".
			 * For this case we should add a clear-to-eol suffix to
			 * clean leftover letters we previously have written on
			 * the same line.
			 */
			if (scratch->len && !linelen)
				strbuf_addstr(scratch, suffix);

			if (!scratch->len)
				strbuf_addstr(scratch, DISPLAY_PREFIX);

			/*
			 * A use case that we should not add clear-to-eol suffix
			 * to empty lines:
			 *
			 * For progress reporting we may receive a bunch of
			 * percentage updates followed by '\r' to remain on the
			 * same line, and at the end receive a single '\n' to
			 * move to the next line. We should preserve the final
			 * status report line by not appending clear-to-eol
			 * suffix to this single line break.
			 */
			if (linelen > 0) {
				maybe_colorize_sideband(scratch, b, linelen);
				strbuf_addstr(scratch, suffix);
			}

			strbuf_addch(scratch, *brk);
			write_in_full(2, scratch->buf, scratch->len);
			strbuf_reset(scratch);

			b = brk + 1;
		}

		if (*b) {
			strbuf_addstr(scratch, scratch->len ?
				    "" : DISPLAY_PREFIX);
			maybe_colorize_sideband(scratch, b, strlen(b));
		}
		return 0;
	case 1:
		*sideband_type = SIDEBAND_PRIMARY;
		return 1;
	default:
		strbuf_addf(scratch, "%s%s: protocol error: bad band #%d",
			    scratch->len ? "\n" : "", me, band);
		*sideband_type = SIDEBAND_PROTOCOL_ERROR;
		break;
	}

cleanup:
	if (die_on_error && *sideband_type == SIDEBAND_PROTOCOL_ERROR)
		die("%s", scratch->buf);
	if (scratch->len) {
		strbuf_addch(scratch, '\n');
		write_in_full(2, scratch->buf, scratch->len);
	}
	strbuf_release(scratch);
	return 1;
}

/*
 * fd is connected to the remote side; send the sideband data
 * over multiplexed packet stream.
 */
void send_sideband(int fd, int band, const char *data, ssize_t sz, int packet_max)
{
	const char *p = data;

	while (sz) {
		unsigned n;
		char hdr[5];

		n = sz;
		if (packet_max - 5 < n)
			n = packet_max - 5;
		if (0 <= band) {
			xsnprintf(hdr, sizeof(hdr), "%04x", n + 5);
			hdr[4] = band;
			write_or_die(fd, hdr, 5);
		} else {
			xsnprintf(hdr, sizeof(hdr), "%04x", n + 4);
			write_or_die(fd, hdr, 4);
		}
		write_or_die(fd, p, n);
		p += n;
		sz -= n;
	}
}
