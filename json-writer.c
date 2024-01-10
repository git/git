#include "git-compat-util.h"
#include "json-writer.h"

void jw_init(struct json_writer *jw)
{
	struct json_writer blank = JSON_WRITER_INIT;
	memcpy(jw, &blank, sizeof(*jw));;
}

void jw_release(struct json_writer *jw)
{
	strbuf_release(&jw->json);
	strbuf_release(&jw->open_stack);
}

/*
 * Append JSON-quoted version of the given string to 'out'.
 */
static void append_quoted_string(struct strbuf *out, const char *in)
{
	unsigned char c;

	strbuf_addch(out, '"');
	while ((c = *in++) != '\0') {
		if (c == '"')
			strbuf_addstr(out, "\\\"");
		else if (c == '\\')
			strbuf_addstr(out, "\\\\");
		else if (c == '\n')
			strbuf_addstr(out, "\\n");
		else if (c == '\r')
			strbuf_addstr(out, "\\r");
		else if (c == '\t')
			strbuf_addstr(out, "\\t");
		else if (c == '\f')
			strbuf_addstr(out, "\\f");
		else if (c == '\b')
			strbuf_addstr(out, "\\b");
		else if (c < 0x20)
			strbuf_addf(out, "\\u%04x", c);
		else
			strbuf_addch(out, c);
	}
	strbuf_addch(out, '"');
}

static void indent_pretty(struct json_writer *jw)
{
	int k;

	for (k = 0; k < jw->open_stack.len; k++)
		strbuf_addstr(&jw->json, "  ");
}

/*
 * Begin an object or array (either top-level or nested within the currently
 * open object or array).
 */
static void begin(struct json_writer *jw, char ch_open, int pretty)
{
	jw->pretty = pretty;

	strbuf_addch(&jw->json, ch_open);

	strbuf_addch(&jw->open_stack, ch_open);
	jw->need_comma = 0;
}

/*
 * Assert that the top of the open-stack is an object.
 */
static void assert_in_object(const struct json_writer *jw, const char *key)
{
	if (!jw->open_stack.len)
		BUG("json-writer: object: missing jw_object_begin(): '%s'", key);
	if (jw->open_stack.buf[jw->open_stack.len - 1] != '{')
		BUG("json-writer: object: not in object: '%s'", key);
}

/*
 * Assert that the top of the open-stack is an array.
 */
static void assert_in_array(const struct json_writer *jw)
{
	if (!jw->open_stack.len)
		BUG("json-writer: array: missing jw_array_begin()");
	if (jw->open_stack.buf[jw->open_stack.len - 1] != '[')
		BUG("json-writer: array: not in array");
}

/*
 * Add comma if we have already seen a member at this level.
 */
static void maybe_add_comma(struct json_writer *jw)
{
	if (jw->need_comma)
		strbuf_addch(&jw->json, ',');
	else
		jw->need_comma = 1;
}

static void fmt_double(struct json_writer *jw, int precision,
			      double value)
{
	if (precision < 0) {
		strbuf_addf(&jw->json, "%f", value);
	} else {
		struct strbuf fmt = STRBUF_INIT;
		strbuf_addf(&fmt, "%%.%df", precision);
		strbuf_addf(&jw->json, fmt.buf, value);
		strbuf_release(&fmt);
	}
}

static void object_common(struct json_writer *jw, const char *key)
{
	assert_in_object(jw, key);
	maybe_add_comma(jw);

	if (jw->pretty) {
		strbuf_addch(&jw->json, '\n');
		indent_pretty(jw);
	}

	append_quoted_string(&jw->json, key);
	strbuf_addch(&jw->json, ':');
	if (jw->pretty)
		strbuf_addch(&jw->json, ' ');
}

static void array_common(struct json_writer *jw)
{
	assert_in_array(jw);
	maybe_add_comma(jw);

	if (jw->pretty) {
		strbuf_addch(&jw->json, '\n');
		indent_pretty(jw);
	}
}

/*
 * Assert that the given JSON object or JSON array has been properly
 * terminated.  (Has closing bracket.)
 */
static void assert_is_terminated(const struct json_writer *jw)
{
	if (jw->open_stack.len)
		BUG("json-writer: object: missing jw_end(): '%s'",
		    jw->json.buf);
}

void jw_object_begin(struct json_writer *jw, int pretty)
{
	begin(jw, '{', pretty);
}

void jw_object_string(struct json_writer *jw, const char *key, const char *value)
{
	object_common(jw, key);
	append_quoted_string(&jw->json, value);
}

void jw_object_intmax(struct json_writer *jw, const char *key, intmax_t value)
{
	object_common(jw, key);
	strbuf_addf(&jw->json, "%"PRIdMAX, value);
}

void jw_object_double(struct json_writer *jw, const char *key, int precision,
		      double value)
{
	object_common(jw, key);
	fmt_double(jw, precision, value);
}

void jw_object_true(struct json_writer *jw, const char *key)
{
	object_common(jw, key);
	strbuf_addstr(&jw->json, "true");
}

void jw_object_false(struct json_writer *jw, const char *key)
{
	object_common(jw, key);
	strbuf_addstr(&jw->json, "false");
}

void jw_object_bool(struct json_writer *jw, const char *key, int value)
{
	if (value)
		jw_object_true(jw, key);
	else
		jw_object_false(jw, key);
}

void jw_object_null(struct json_writer *jw, const char *key)
{
	object_common(jw, key);
	strbuf_addstr(&jw->json, "null");
}

static void increase_indent(struct strbuf *sb,
			    const struct json_writer *jw,
			    int indent)
{
	int k;

	strbuf_reset(sb);
	for (k = 0; k < jw->json.len; k++) {
		char ch = jw->json.buf[k];
		strbuf_addch(sb, ch);
		if (ch == '\n')
			strbuf_addchars(sb, ' ', indent);
	}
}

static void kill_indent(struct strbuf *sb,
			const struct json_writer *jw)
{
	int k;
	int eat_it = 0;

	strbuf_reset(sb);
	for (k = 0; k < jw->json.len; k++) {
		char ch = jw->json.buf[k];
		if (eat_it && ch == ' ')
			continue;
		if (ch == '\n') {
			eat_it = 1;
			continue;
		}
		eat_it = 0;
		strbuf_addch(sb, ch);
	}
}

static void append_sub_jw(struct json_writer *jw,
			  const struct json_writer *value)
{
	/*
	 * If both are pretty, increase the indentation of the sub_jw
	 * to better fit under the super.
	 *
	 * If the super is pretty, but the sub_jw is compact, leave the
	 * sub_jw compact.  (We don't want to parse and rebuild the sub_jw
	 * for this debug-ish feature.)
	 *
	 * If the super is compact, and the sub_jw is pretty, convert
	 * the sub_jw to compact.
	 *
	 * If both are compact, keep the sub_jw compact.
	 */
	if (jw->pretty && jw->open_stack.len && value->pretty) {
		struct strbuf sb = STRBUF_INIT;
		increase_indent(&sb, value, jw->open_stack.len * 2);
		strbuf_addbuf(&jw->json, &sb);
		strbuf_release(&sb);
		return;
	}
	if (!jw->pretty && value->pretty) {
		struct strbuf sb = STRBUF_INIT;
		kill_indent(&sb, value);
		strbuf_addbuf(&jw->json, &sb);
		strbuf_release(&sb);
		return;
	}

	strbuf_addbuf(&jw->json, &value->json);
}

/*
 * Append existing (properly terminated) JSON sub-data (object or array)
 * as-is onto the given JSON data.
 */
void jw_object_sub_jw(struct json_writer *jw, const char *key,
		      const struct json_writer *value)
{
	assert_is_terminated(value);

	object_common(jw, key);
	append_sub_jw(jw, value);
}

void jw_object_inline_begin_object(struct json_writer *jw, const char *key)
{
	object_common(jw, key);

	jw_object_begin(jw, jw->pretty);
}

void jw_object_inline_begin_array(struct json_writer *jw, const char *key)
{
	object_common(jw, key);

	jw_array_begin(jw, jw->pretty);
}

void jw_array_begin(struct json_writer *jw, int pretty)
{
	begin(jw, '[', pretty);
}

void jw_array_string(struct json_writer *jw, const char *value)
{
	array_common(jw);
	append_quoted_string(&jw->json, value);
}

void jw_array_intmax(struct json_writer *jw, intmax_t value)
{
	array_common(jw);
	strbuf_addf(&jw->json, "%"PRIdMAX, value);
}

void jw_array_double(struct json_writer *jw, int precision, double value)
{
	array_common(jw);
	fmt_double(jw, precision, value);
}

void jw_array_true(struct json_writer *jw)
{
	array_common(jw);
	strbuf_addstr(&jw->json, "true");
}

void jw_array_false(struct json_writer *jw)
{
	array_common(jw);
	strbuf_addstr(&jw->json, "false");
}

void jw_array_bool(struct json_writer *jw, int value)
{
	if (value)
		jw_array_true(jw);
	else
		jw_array_false(jw);
}

void jw_array_null(struct json_writer *jw)
{
	array_common(jw);
	strbuf_addstr(&jw->json, "null");
}

void jw_array_sub_jw(struct json_writer *jw, const struct json_writer *value)
{
	assert_is_terminated(value);

	array_common(jw);
	append_sub_jw(jw, value);
}

void jw_array_argc_argv(struct json_writer *jw, int argc, const char **argv)
{
	int k;

	for (k = 0; k < argc; k++)
		jw_array_string(jw, argv[k]);
}

void jw_array_argv(struct json_writer *jw, const char **argv)
{
	while (*argv)
		jw_array_string(jw, *argv++);
}

void jw_array_inline_begin_object(struct json_writer *jw)
{
	array_common(jw);

	jw_object_begin(jw, jw->pretty);
}

void jw_array_inline_begin_array(struct json_writer *jw)
{
	array_common(jw);

	jw_array_begin(jw, jw->pretty);
}

int jw_is_terminated(const struct json_writer *jw)
{
	return !jw->open_stack.len;
}

void jw_end(struct json_writer *jw)
{
	char ch_open;
	int len;

	if (!jw->open_stack.len)
		BUG("json-writer: too many jw_end(): '%s'", jw->json.buf);

	len = jw->open_stack.len - 1;
	ch_open = jw->open_stack.buf[len];

	strbuf_setlen(&jw->open_stack, len);
	jw->need_comma = 1;

	if (jw->pretty) {
		strbuf_addch(&jw->json, '\n');
		indent_pretty(jw);
	}

	if (ch_open == '{')
		strbuf_addch(&jw->json, '}');
	else
		strbuf_addch(&jw->json, ']');
}
