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
} whitespace_rule_names[] = {
	{ "trailing-space", WS_TRAILING_SPACE },
	{ "space-before-tab", WS_SPACE_BEFORE_TAB },
	{ "indent-with-non-tab", WS_INDENT_WITH_NON_TAB },
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
		string = ep;
	}
	return rule;
}

static void setup_whitespace_attr_check(struct git_attr_check *check)
{
	static struct git_attr *attr_whitespace;

	if (!attr_whitespace)
		attr_whitespace = git_attr("whitespace", 10);
	check[0].attr = attr_whitespace;
}

unsigned whitespace_rule(const char *pathname)
{
	struct git_attr_check attr_whitespace_rule;

	setup_whitespace_attr_check(&attr_whitespace_rule);
	if (!git_checkattr(pathname, 1, &attr_whitespace_rule)) {
		const char *value;

		value = attr_whitespace_rule.value;
		if (ATTR_TRUE(value)) {
			/* true (whitespace) */
			unsigned all_rule = 0;
			int i;
			for (i = 0; i < ARRAY_SIZE(whitespace_rule_names); i++)
				all_rule |= whitespace_rule_names[i].rule_bits;
			return all_rule;
		} else if (ATTR_FALSE(value)) {
			/* false (-whitespace) */
			return 0;
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
	struct strbuf err;
	strbuf_init(&err, 0);
	if (ws & WS_TRAILING_SPACE)
		strbuf_addstr(&err, "trailing whitespace");
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
	return strbuf_detach(&err, NULL);
}

/* If stream is non-NULL, emits the line after checking. */
unsigned check_and_emit_line(const char *line, int len, unsigned ws_rule,
			     FILE *stream, const char *set,
			     const char *reset, const char *ws)
{
	unsigned result = 0;
	int leading_space = -1;
	int trailing_whitespace = -1;
	int trailing_newline = 0;
	int i;

	/* Logic is simpler if we temporarily ignore the trailing newline. */
	if (len > 0 && line[len - 1] == '\n') {
		trailing_newline = 1;
		len--;
	}

	/* Check for trailing whitespace. */
	if (ws_rule & WS_TRAILING_SPACE) {
		for (i = len - 1; i >= 0; i--) {
			if (isspace(line[i])) {
				trailing_whitespace = i;
				result |= WS_TRAILING_SPACE;
			}
			else
				break;
		}
	}

	/* Check for space before tab in initial indent. */
	for (i = 0; i < len; i++) {
		if (line[i] == '\t') {
			if ((ws_rule & WS_SPACE_BEFORE_TAB) &&
			    (leading_space != -1))
				result |= WS_SPACE_BEFORE_TAB;
			break;
		}
		else if (line[i] == ' ')
			leading_space = i;
		else
			break;
	}

	/* Check for indent using non-tab. */
	if ((ws_rule & WS_INDENT_WITH_NON_TAB) && leading_space >= 8)
		result |= WS_INDENT_WITH_NON_TAB;

	if (stream) {
		/* Highlight errors in leading whitespace. */
		if ((result & WS_SPACE_BEFORE_TAB) ||
		    (result & WS_INDENT_WITH_NON_TAB)) {
			fputs(ws, stream);
			fwrite(line, leading_space + 1, 1, stream);
			fputs(reset, stream);
			leading_space++;
		}
		else
			leading_space = 0;

		/* Now the rest of the line starts at leading_space.
		 * The non-highlighted part ends at trailing_whitespace. */
		if (trailing_whitespace == -1)
			trailing_whitespace = len;

		/* Emit non-highlighted (middle) segment. */
		if (trailing_whitespace - leading_space > 0) {
			fputs(set, stream);
			fwrite(line + leading_space,
			    trailing_whitespace - leading_space, 1, stream);
			fputs(reset, stream);
		}

		/* Highlight errors in trailing whitespace. */
		if (trailing_whitespace != len) {
			fputs(ws, stream);
			fwrite(line + trailing_whitespace,
			    len - trailing_whitespace, 1, stream);
			fputs(reset, stream);
		}
		if (trailing_newline)
			fputc('\n', stream);
	}
	return result;
}
