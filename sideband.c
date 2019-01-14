#include "cache.h"
#include "color.h"
#include "config.h"
#include "pkt-line.h"
#include "sideband.h"
#include "help.h"

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


/*
 * Receive multiplexed output stream over git native protocol.
 * in_stream is the input stream from the remote, which carries data
 * in pkt_line format with band designator.  Demultiplex it into out
 * and err and return error appropriately.  Band #1 carries the
 * primary payload.  Things coming over band #2 is not necessarily
 * error; they are usually informative message on the standard error
 * stream, aka "verbose").  A message over band #3 is a signal that
 * the remote died unexpectedly.  A flush() concludes the stream.
 */

#define DISPLAY_PREFIX "remote: "

#define ANSI_SUFFIX "\033[K"
#define DUMB_SUFFIX "        "

int recv_sideband(const char *me, int in_stream, int out)
{
	const char *suffix;
	char buf[LARGE_PACKET_MAX + 1];
	struct strbuf outbuf = STRBUF_INIT;
	int retval = 0;

	if (isatty(2) && !is_terminal_dumb())
		suffix = ANSI_SUFFIX;
	else
		suffix = DUMB_SUFFIX;

	while (!retval) {
		const char *b, *brk;
		int band, len;
		len = packet_read(in_stream, NULL, NULL, buf, LARGE_PACKET_MAX, 0);
		if (len == 0)
			break;
		if (len < 1) {
			strbuf_addf(&outbuf,
				    "%s%s: protocol error: no band designator",
				    outbuf.len ? "\n" : "", me);
			retval = SIDEBAND_PROTOCOL_ERROR;
			break;
		}
		band = buf[0] & 0xff;
		buf[len] = '\0';
		len--;
		switch (band) {
		case 3:
			strbuf_addf(&outbuf, "%s%s", outbuf.len ? "\n" : "",
				    DISPLAY_PREFIX);
			maybe_colorize_sideband(&outbuf, buf + 1, len);

			retval = SIDEBAND_REMOTE_ERROR;
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

				if (!outbuf.len)
					strbuf_addstr(&outbuf, DISPLAY_PREFIX);
				if (linelen > 0) {
					maybe_colorize_sideband(&outbuf, b, linelen);
					strbuf_addstr(&outbuf, suffix);
				}

				strbuf_addch(&outbuf, *brk);
				xwrite(2, outbuf.buf, outbuf.len);
				strbuf_reset(&outbuf);

				b = brk + 1;
			}

			if (*b) {
				strbuf_addstr(&outbuf, outbuf.len ?
					    "" : DISPLAY_PREFIX);
				maybe_colorize_sideband(&outbuf, b, strlen(b));
			}
			break;
		case 1:
			write_or_die(out, buf + 1, len);
			break;
		default:
			strbuf_addf(&outbuf, "%s%s: protocol error: bad band #%d",
				    outbuf.len ? "\n" : "", me, band);
			retval = SIDEBAND_PROTOCOL_ERROR;
			break;
		}
	}

	if (outbuf.len) {
		strbuf_addch(&outbuf, '\n');
		xwrite(2, outbuf.buf, outbuf.len);
	}
	strbuf_release(&outbuf);
	return retval;
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
