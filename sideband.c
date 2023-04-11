#include "cache.h"
#include "color.h"
#include "config.h"
#include "editor.h"
#include "gettext.h"
#include "sideband.h"
#include "help.h"
#include "pkt-line.h"
#include "write-or-die.h"

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

/* Returns a color setting (GIT_COLOR_NEVER, etc). */
static int use_sideband_colors(void)
{
	static int use_sideband_colors_cached = -1;

	const char *key = "color.remote";
	struct strbuf sb = STRBUF_INIT;
	char *value;
	int i;

	if (use_sideband_colors_cached >= 0)
		return use_sideband_colors_cached;

	if (!git_config_get_string(key, &value)) {
		use_sideband_colors_cached = git_config_colorbool(key, value);
	} else if (!git_config_get_string("color.ui", &value)) {
		use_sideband_colors_cached = git_config_colorbool("color.ui", value);
	} else {
		use_sideband_colors_cached = GIT_COLOR_AUTO;
	}

	for (i = 0; i < ARRAY_SIZE(keywords); i++) {
		strbuf_reset(&sb);
		strbuf_addf(&sb, "%s.%s", key, keywords[i].keyword);
		if (git_config_get_string(sb.buf, &value))
			continue;
		if (color_parse(value, keywords[i].color))
			continue;
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

/*
 * Optionally highlight one keyword in remote output if it appears at the start
 * of the line. This should be called for a single line only, which is
 * passed as the first N characters of the SRC array.
 *
 * NEEDSWORK: use "size_t n" instead for clarity.
 */
static void maybe_colorize_sideband(struct strbuf *dest, const char *src, int n)
{
	int i;

	if (!want_color_stderr(use_sideband_colors())) {
		strbuf_add(dest, src, n);
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

	strbuf_add(dest, src, n);
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
			 * For message accross packet boundary, there would have
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
			xwrite(2, scratch->buf, scratch->len);
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
		xwrite(2, scratch->buf, scratch->len);
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
