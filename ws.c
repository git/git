/*
 * Whitespace rules
 *
 * Copyright (c) 2007 Junio C Hamano
 */

#include "cache.h"
#include "attr.h"

static struct whitespace_rule {
	const char *rule_name;
	unsigned rule_bits;
	unsigned loosens_error:1,
		exclude_default:1;
} whitespace_rule_names[] = {
	{ "trailing-space", WS_TRAILING_SPACE, 0 },
	{ "space-before-tab", WS_SPACE_BEFORE_TAB, 0 },
	{ "indent-with-non-tab", WS_INDENT_WITH_NON_TAB, 0 },
	{ "cr-at-eol", WS_CR_AT_EOL, 1 },
	{ "blank-at-eol", WS_BLANK_AT_EOL, 0 },
	{ "blank-at-eof", WS_BLANK_AT_EOF, 0 },
	{ "tab-in-indent", WS_TAB_IN_INDENT, 0, 1 },
};

unsigned parse_whitespace_rule(const char *string)
{
	unsigned rule = WS_DEFAULT_RULE;

	while (string) {
		int i;
		size_t len;
		const char *ep;
		int negated = 0;

		string = string + strspn(string, ", \t\n\r");
		ep = strchr(string, ',');
		if (!ep)
			len = strlen(string);
		else
			len = ep - string;

		if (*string == '-') {
			negated = 1;
			string++;
			len--;
		}
		if (!len)
			break;
		for (i = 0; i < ARRAY_SIZE(whitespace_rule_names); i++) {
			if (strncmp(whitespace_rule_names[i].rule_name,
				    string, len))
				continue;
			if (negated)
				rule &= ~whitespace_rule_names[i].rule_bits;
			else
				rule |= whitespace_rule_names[i].rule_bits;
			break;
		}
		if (strncmp(string, "tabwidth=", 9) == 0) {
			unsigned tabwidth = atoi(string + 9);
			if (0 < tabwidth && tabwidth < 0100) {
				rule &= ~WS_TAB_WIDTH_MASK;
				rule |= tabwidth;
			}
			else
				warning("tabwidth %.*s out of range",
					(int)(len - 9), string + 9);
		}
		string = ep;
	}

	if (rule & WS_TAB_IN_INDENT && rule & WS_INDENT_WITH_NON_TAB)
		die("cannot enforce both tab-in-indent and indent-with-non-tab");
	return rule;
}

static void setup_whitespace_attr_check(struct git_attr_check *check)
{
	static struct git_attr *attr_whitespace;

	if (!attr_whitespace)
		attr_whitespace = git_attr("whitespace");
	check[0].attr = attr_whitespace;
}

unsigned whitespace_rule(const char *pathname)
{
	struct git_attr_check attr_whitespace_rule;

	setup_whitespace_attr_check(&attr_whitespace_rule);
	if (!git_check_attr(pathname, 1, &attr_whitespace_rule)) {
		const char *value;

		value = attr_whitespace_rule.value;
		if (ATTR_TRUE(value)) {
			/* true (whitespace) */
			unsigned all_rule = ws_tab_width(whitespace_rule_cfg);
			int i;
			for (i = 0; i < ARRAY_SIZE(whitespace_rule_names); i++)
				if (!whitespace_rule_names[i].loosens_error &&
				    !whitespace_rule_names[i].exclude_default)
					all_rule |= whitespace_rule_names[i].rule_bits;
			return all_rule;
		} else if (ATTR_FALSE(value)) {
			/* false (-whitespace) */
			return ws_tab_width(whitespace_rule_cfg);
		} else if (ATTR_UNSET(value)) {
			/* reset to default (!whitespace) */
			return whitespace_rule_cfg;
		} else {
			/* string */
			return parse_whitespace_rule(value);
		}
	} else {
		return whitespace_rule_cfg;
	}
}

/* The returned string should be freed by the caller. */
char *whitespace_error_string(unsigned ws)
{
	struct strbuf err = STRBUF_INIT;
	if ((ws & WS_TRAILING_SPACE) == WS_TRAILING_SPACE)
		strbuf_addstr(&err, "trailing whitespace");
	else {
		if (ws & WS_BLANK_AT_EOL)
			strbuf_addstr(&err, "trailing whitespace");
		if (ws & WS_BLANK_AT_EOF) {
			if (err.len)
				strbuf_addstr(&err, ", ");
			strbuf_addstr(&err, "new blank line at EOF");
		}
	}
	if (ws & WS_SPACE_BEFORE_TAB) {
		if (err.len)
			strbuf_addstr(&err, ", ");
		strbuf_addstr(&err, "space before tab in indent");
	}
	if (ws & WS_INDENT_WITH_NON_TAB) {
		if (err.len)
			strbuf_addstr(&err, ", ");
		strbuf_addstr(&err, "indent with spaces");
	}
	if (ws & WS_TAB_IN_INDENT) {
		if (err.len)
			strbuf_addstr(&err, ", ");
		strbuf_addstr(&err, "tab in indent");
	}
	return strbuf_detach(&err, NULL);
}

/* If stream is non-NULL, emits the line after checking. */
static unsigned ws_check_emit_1(const char *line, int len, unsigned ws_rule,
				FILE *stream, const char *set,
				const char *reset, const char *ws)
{
	unsigned result = 0;
	int written = 0;
	int trailing_whitespace = -1;
	int trailing_newline = 0;
	int trailing_carriage_return = 0;
	int i;

	/* Logic is simpler if we temporarily ignore the trailing newline. */
	if (len > 0 && line[len - 1] == '\n') {
		trailing_newline = 1;
		len--;
	}
	if ((ws_rule & WS_CR_AT_EOL) &&
	    len > 0 && line[len - 1] == '\r') {
		trailing_carriage_return = 1;
		len--;
	}

	/* Check for trailing whitespace. */
	if (ws_rule & WS_BLANK_AT_EOL) {
		for (i = len - 1; i >= 0; i--) {
			if (isspace(line[i])) {
				trailing_whitespace = i;
				result |= WS_BLANK_AT_EOL;
			}
			else
				break;
		}
	}

	if (trailing_whitespace == -1)
		trailing_whitespace = len;

	/* Check indentation */
	for (i = 0; i < trailing_whitespace; i++) {
		if (line[i] == ' ')
			continue;
		if (line[i] != '\t')
			break;
		if ((ws_rule & WS_SPACE_BEFORE_TAB) && written < i) {
			result |= WS_SPACE_BEFORE_TAB;
			if (stream) {
				fputs(ws, stream);
				fwrite(line + written, i - written, 1, stream);
				fputs(reset, stream);
				fwrite(line + i, 1, 1, stream);
			}
		} else if (ws_rule & WS_TAB_IN_INDENT) {
			result |= WS_TAB_IN_INDENT;
			if (stream) {
				fwrite(line + written, i - written, 1, stream);
				fputs(ws, stream);
				fwrite(line + i, 1, 1, stream);
				fputs(reset, stream);
			}
		} else if (stream) {
			fwrite(line + written, i - written + 1, 1, stream);
		}
		written = i + 1;
	}

	/* Check for indent using non-tab. */
	if ((ws_rule & WS_INDENT_WITH_NON_TAB) && i - written >= ws_tab_width(ws_rule)) {
		result |= WS_INDENT_WITH_NON_TAB;
		if (stream) {
			fputs(ws, stream);
			fwrite(line + written, i - written, 1, stream);
			fputs(reset, stream);
		}
		written = i;
	}

	if (stream) {
		/*
		 * Now the rest of the line starts at "written".
		 * The non-highlighted part ends at "trailing_whitespace".
		 */

		/* Emit non-highlighted (middle) segment. */
		if (trailing_whitespace - written > 0) {
			fputs(set, stream);
			fwrite(line + written,
			    trailing_whitespace - written, 1, stream);
			fputs(reset, stream);
		}

		/* Highlight errors in trailing whitespace. */
		if (trailing_whitespace != len) {
			fputs(ws, stream);
			fwrite(line + trailing_whitespace,
			    len - trailing_whitespace, 1, stream);
			fputs(reset, stream);
		}
		if (trailing_carriage_return)
			fputc('\r', stream);
		if (trailing_newline)
			fputc('\n', stream);
	}
	return result;
}

void ws_check_emit(const char *line, int len, unsigned ws_rule,
		   FILE *stream, const char *set,
		   const char *reset, const char *ws)
{
	(void)ws_check_emit_1(line, len, ws_rule, stream, set, reset, ws);
}

unsigned ws_check(const char *line, int len, unsigned ws_rule)
{
	return ws_check_emit_1(line, len, ws_rule, NULL, NULL, NULL, NULL);
}

int ws_blank_line(const char *line, int len, unsigned ws_rule)
{
	/*
	 * We _might_ want to treat CR differently from other
	 * whitespace characters when ws_rule has WS_CR_AT_EOL, but
	 * for now we just use this stupid definition.
	 */
	while (len-- > 0) {
		if (!isspace(*line))
			return 0;
		line++;
	}
	return 1;
}

/* Copy the line onto the end of the strbuf while fixing whitespaces */
void ws_fix_copy(struct strbuf *dst, const char *src, int len, unsigned ws_rule, int *error_count)
{
	/*
	 * len is number of bytes to be copied from src, starting
	 * at src.  Typically src[len-1] is '\n', unless this is
	 * the incomplete last line.
	 */
	int i;
	int add_nl_to_tail = 0;
	int add_cr_to_tail = 0;
	int fixed = 0;
	int last_tab_in_indent = -1;
	int last_space_in_indent = -1;
	int need_fix_leading_space = 0;

	/*
	 * Strip trailing whitespace
	 */
	if (ws_rule & WS_BLANK_AT_EOL) {
		if (0 < len && src[len - 1] == '\n') {
			add_nl_to_tail = 1;
			len--;
			if (0 < len && src[len - 1] == '\r') {
				add_cr_to_tail = !!(ws_rule & WS_CR_AT_EOL);
				len--;
			}
		}
		if (0 < len && isspace(src[len - 1])) {
			while (0 < len && isspace(src[len-1]))
				len--;
			fixed = 1;
		}
	}

	/*
	 * Check leading whitespaces (indent)
	 */
	for (i = 0; i < len; i++) {
		char ch = src[i];
		if (ch == '\t') {
			last_tab_in_indent = i;
			if ((ws_rule & WS_SPACE_BEFORE_TAB) &&
			    0 <= last_space_in_indent)
			    need_fix_leading_space = 1;
		} else if (ch == ' ') {
			last_space_in_indent = i;
			if ((ws_rule & WS_INDENT_WITH_NON_TAB) &&
			    ws_tab_width(ws_rule) <= i - last_tab_in_indent)
				need_fix_leading_space = 1;
		} else
			break;
	}

	if (need_fix_leading_space) {
		/* Process indent ourselves */
		int consecutive_spaces = 0;
		int last = last_tab_in_indent + 1;

		if (ws_rule & WS_INDENT_WITH_NON_TAB) {
			/* have "last" point at one past the indent */
			if (last_tab_in_indent < last_space_in_indent)
				last = last_space_in_indent + 1;
			else
				last = last_tab_in_indent + 1;
		}

		/*
		 * between src[0..last-1], strip the funny spaces,
		 * updating them to tab as needed.
		 */
		for (i = 0; i < last; i++) {
			char ch = src[i];
			if (ch != ' ') {
				consecutive_spaces = 0;
				strbuf_addch(dst, ch);
			} else {
				consecutive_spaces++;
				if (consecutive_spaces == ws_tab_width(ws_rule)) {
					strbuf_addch(dst, '\t');
					consecutive_spaces = 0;
				}
			}
		}
		while (0 < consecutive_spaces--)
			strbuf_addch(dst, ' ');
		len -= last;
		src += last;
		fixed = 1;
	} else if ((ws_rule & WS_TAB_IN_INDENT) && last_tab_in_indent >= 0) {
		/* Expand tabs into spaces */
		int start = dst->len;
		int last = last_tab_in_indent + 1;
		for (i = 0; i < last; i++) {
			if (src[i] == '\t')
				do {
					strbuf_addch(dst, ' ');
				} while ((dst->len - start) % ws_tab_width(ws_rule));
			else
				strbuf_addch(dst, src[i]);
		}
		len -= last;
		src += last;
		fixed = 1;
	}

	strbuf_add(dst, src, len);
	if (add_cr_to_tail)
		strbuf_addch(dst, '\r');
	if (add_nl_to_tail)
		strbuf_addch(dst, '\n');
	if (fixed && error_count)
		(*error_count)++;
}
