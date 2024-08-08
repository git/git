#include "test-tool.h"
#include "json-writer.h"
#include "string-list.h"

static const char *expect_obj1 = "{\"a\":\"abc\",\"b\":42,\"c\":true}";
static const char *expect_obj2 = "{\"a\":-1,\"b\":2147483647,\"c\":0}";
static const char *expect_obj3 = "{\"a\":0,\"b\":4294967295,\"c\":9223372036854775807}";
static const char *expect_obj4 = "{\"t\":true,\"f\":false,\"n\":null}";
static const char *expect_obj5 = "{\"abc\\tdef\":\"abc\\\\def\"}";
static const char *expect_obj6 = "{\"a\":3.14}";

static const char *pretty_obj1 = ("{\n"
				  "  \"a\": \"abc\",\n"
				  "  \"b\": 42,\n"
				  "  \"c\": true\n"
				  "}");
static const char *pretty_obj2 = ("{\n"
				  "  \"a\": -1,\n"
				  "  \"b\": 2147483647,\n"
				  "  \"c\": 0\n"
				  "}");
static const char *pretty_obj3 = ("{\n"
				  "  \"a\": 0,\n"
				  "  \"b\": 4294967295,\n"
				  "  \"c\": 9223372036854775807\n"
				  "}");
static const char *pretty_obj4 = ("{\n"
				  "  \"t\": true,\n"
				  "  \"f\": false,\n"
				  "  \"n\": null\n"
				  "}");

static struct json_writer obj1 = JSON_WRITER_INIT;
static struct json_writer obj2 = JSON_WRITER_INIT;
static struct json_writer obj3 = JSON_WRITER_INIT;
static struct json_writer obj4 = JSON_WRITER_INIT;
static struct json_writer obj5 = JSON_WRITER_INIT;
static struct json_writer obj6 = JSON_WRITER_INIT;

static void make_obj1(int pretty)
{
	jw_object_begin(&obj1, pretty);
	{
		jw_object_string(&obj1, "a", "abc");
		jw_object_intmax(&obj1, "b", 42);
		jw_object_true(&obj1, "c");
	}
	jw_end(&obj1);
}

static void make_obj2(int pretty)
{
	jw_object_begin(&obj2, pretty);
	{
		jw_object_intmax(&obj2, "a", -1);
		jw_object_intmax(&obj2, "b", 0x7fffffff);
		jw_object_intmax(&obj2, "c", 0);
	}
	jw_end(&obj2);
}

static void make_obj3(int pretty)
{
	jw_object_begin(&obj3, pretty);
	{
		jw_object_intmax(&obj3, "a", 0);
		jw_object_intmax(&obj3, "b", 0xffffffff);
		jw_object_intmax(&obj3, "c", 0x7fffffffffffffffULL);
	}
	jw_end(&obj3);
}

static void make_obj4(int pretty)
{
	jw_object_begin(&obj4, pretty);
	{
		jw_object_true(&obj4, "t");
		jw_object_false(&obj4, "f");
		jw_object_null(&obj4, "n");
	}
	jw_end(&obj4);
}

static void make_obj5(int pretty)
{
	jw_object_begin(&obj5, pretty);
	{
		jw_object_string(&obj5, "abc" "\x09" "def", "abc" "\\" "def");
	}
	jw_end(&obj5);
}

static void make_obj6(int pretty)
{
	jw_object_begin(&obj6, pretty);
	{
		jw_object_double(&obj6, "a", 2, 3.14159);
	}
	jw_end(&obj6);
}

static const char *expect_arr1 = "[\"abc\",42,true]";
static const char *expect_arr2 = "[-1,2147483647,0]";
static const char *expect_arr3 = "[0,4294967295,9223372036854775807]";
static const char *expect_arr4 = "[true,false,null]";

static const char *pretty_arr1 = ("[\n"
				  "  \"abc\",\n"
				  "  42,\n"
				  "  true\n"
				  "]");
static const char *pretty_arr2 = ("[\n"
				  "  -1,\n"
				  "  2147483647,\n"
				  "  0\n"
				  "]");
static const char *pretty_arr3 = ("[\n"
				  "  0,\n"
				  "  4294967295,\n"
				  "  9223372036854775807\n"
				  "]");
static const char *pretty_arr4 = ("[\n"
				  "  true,\n"
				  "  false,\n"
				  "  null\n"
				  "]");

static struct json_writer arr1 = JSON_WRITER_INIT;
static struct json_writer arr2 = JSON_WRITER_INIT;
static struct json_writer arr3 = JSON_WRITER_INIT;
static struct json_writer arr4 = JSON_WRITER_INIT;

static void make_arr1(int pretty)
{
	jw_array_begin(&arr1, pretty);
	{
		jw_array_string(&arr1, "abc");
		jw_array_intmax(&arr1, 42);
		jw_array_true(&arr1);
	}
	jw_end(&arr1);
}

static void make_arr2(int pretty)
{
	jw_array_begin(&arr2, pretty);
	{
		jw_array_intmax(&arr2, -1);
		jw_array_intmax(&arr2, 0x7fffffff);
		jw_array_intmax(&arr2, 0);
	}
	jw_end(&arr2);
}

static void make_arr3(int pretty)
{
	jw_array_begin(&arr3, pretty);
	{
		jw_array_intmax(&arr3, 0);
		jw_array_intmax(&arr3, 0xffffffff);
		jw_array_intmax(&arr3, 0x7fffffffffffffffULL);
	}
	jw_end(&arr3);
}

static void make_arr4(int pretty)
{
	jw_array_begin(&arr4, pretty);
	{
		jw_array_true(&arr4);
		jw_array_false(&arr4);
		jw_array_null(&arr4);
	}
	jw_end(&arr4);
}

static const char *expect_nest1 =
	"{\"obj1\":{\"a\":\"abc\",\"b\":42,\"c\":true},\"arr1\":[\"abc\",42,true]}";

static struct json_writer nest1 = JSON_WRITER_INIT;

static void make_nest1(int pretty)
{
	make_obj1(0);
	make_arr1(0);

	jw_object_begin(&nest1, pretty);
	{
		jw_object_sub_jw(&nest1, "obj1", &obj1);
		jw_object_sub_jw(&nest1, "arr1", &arr1);
	}
	jw_end(&nest1);

	jw_release(&obj1);
	jw_release(&arr1);
}

static const char *expect_inline1 =
	"{\"obj1\":{\"a\":\"abc\",\"b\":42,\"c\":true},\"arr1\":[\"abc\",42,true]}";

static const char *pretty_inline1 =
	("{\n"
	 "  \"obj1\": {\n"
	 "    \"a\": \"abc\",\n"
	 "    \"b\": 42,\n"
	 "    \"c\": true\n"
	 "  },\n"
	 "  \"arr1\": [\n"
	 "    \"abc\",\n"
	 "    42,\n"
	 "    true\n"
	 "  ]\n"
	 "}");

static struct json_writer inline1 = JSON_WRITER_INIT;

static void make_inline1(int pretty)
{
	jw_object_begin(&inline1, pretty);
	{
		jw_object_inline_begin_object(&inline1, "obj1");
		{
			jw_object_string(&inline1, "a", "abc");
			jw_object_intmax(&inline1, "b", 42);
			jw_object_true(&inline1, "c");
		}
		jw_end(&inline1);
		jw_object_inline_begin_array(&inline1, "arr1");
		{
			jw_array_string(&inline1, "abc");
			jw_array_intmax(&inline1, 42);
			jw_array_true(&inline1);
		}
		jw_end(&inline1);
	}
	jw_end(&inline1);
}

static const char *expect_inline2 =
	"[[1,2],[3,4],{\"a\":\"abc\"}]";

static const char *pretty_inline2 =
	("[\n"
	 "  [\n"
	 "    1,\n"
	 "    2\n"
	 "  ],\n"
	 "  [\n"
	 "    3,\n"
	 "    4\n"
	 "  ],\n"
	 "  {\n"
	 "    \"a\": \"abc\"\n"
	 "  }\n"
	 "]");

static struct json_writer inline2 = JSON_WRITER_INIT;

static void make_inline2(int pretty)
{
	jw_array_begin(&inline2, pretty);
	{
		jw_array_inline_begin_array(&inline2);
		{
			jw_array_intmax(&inline2, 1);
			jw_array_intmax(&inline2, 2);
		}
		jw_end(&inline2);
		jw_array_inline_begin_array(&inline2);
		{
			jw_array_intmax(&inline2, 3);
			jw_array_intmax(&inline2, 4);
		}
		jw_end(&inline2);
		jw_array_inline_begin_object(&inline2);
		{
			jw_object_string(&inline2, "a", "abc");
		}
		jw_end(&inline2);
	}
	jw_end(&inline2);
}

/*
 * When super is compact, we expect subs to be compacted (even if originally
 * pretty).
 */
static const char *expect_mixed1 =
	("{\"obj1\":{\"a\":\"abc\",\"b\":42,\"c\":true},"
	 "\"arr1\":[\"abc\",42,true]}");

/*
 * When super is pretty, a compact sub (obj1) is kept compact and a pretty
 * sub (arr1) is re-indented.
 */
static const char *pretty_mixed1 =
	("{\n"
	 "  \"obj1\": {\"a\":\"abc\",\"b\":42,\"c\":true},\n"
	 "  \"arr1\": [\n"
	 "    \"abc\",\n"
	 "    42,\n"
	 "    true\n"
	 "  ]\n"
	 "}");

static struct json_writer mixed1 = JSON_WRITER_INIT;

static void make_mixed1(int pretty)
{
	jw_init(&obj1);
	jw_init(&arr1);

	make_obj1(0); /* obj1 is compact */
	make_arr1(1); /* arr1 is pretty */

	jw_object_begin(&mixed1, pretty);
	{
		jw_object_sub_jw(&mixed1, "obj1", &obj1);
		jw_object_sub_jw(&mixed1, "arr1", &arr1);
	}
	jw_end(&mixed1);

	jw_release(&obj1);
	jw_release(&arr1);
}

static void cmp(const char *test, const struct json_writer *jw, const char *exp)
{
	if (!strcmp(jw->json.buf, exp))
		return;

	printf("error[%s]: observed '%s' expected '%s'\n",
	       test, jw->json.buf, exp);
	exit(1);
}

#define t(v) do { make_##v(0); cmp(#v, &v, expect_##v); jw_release(&v); } while (0)
#define p(v) do { make_##v(1); cmp(#v, &v, pretty_##v); jw_release(&v); } while (0)

/*
 * Run some basic regression tests with some known patterns.
 * These tests also demonstrate how to use the jw_ API.
 */
static int unit_tests(void)
{
	/* comptact (canonical) forms */
	t(obj1);
	t(obj2);
	t(obj3);
	t(obj4);
	t(obj5);
	t(obj6);

	t(arr1);
	t(arr2);
	t(arr3);
	t(arr4);

	t(nest1);

	t(inline1);
	t(inline2);

	jw_init(&obj1);
	jw_init(&obj2);
	jw_init(&obj3);
	jw_init(&obj4);

	jw_init(&arr1);
	jw_init(&arr2);
	jw_init(&arr3);
	jw_init(&arr4);

	jw_init(&inline1);
	jw_init(&inline2);

	/* pretty forms */
	p(obj1);
	p(obj2);
	p(obj3);
	p(obj4);

	p(arr1);
	p(arr2);
	p(arr3);
	p(arr4);

	p(inline1);
	p(inline2);

	/* mixed forms */
	t(mixed1);
	p(mixed1);

	return 0;
}

struct line {
	struct string_list *parts;
	size_t consumed_nr;
	int nr;
};

static void get_s(struct line *line, char **s_in)
{
	if (line->consumed_nr > line->parts->nr)
		die("line[%d]: expected: <s>", line->nr);
	*s_in = line->parts->items[line->consumed_nr++].string;
}

static void get_i(struct line *line, intmax_t *s_in)
{
	char *s;
	char *endptr;

	get_s(line, &s);

	errno = 0;
	*s_in = strtol(s, &endptr, 10);
	if (*endptr || errno == ERANGE)
		die("line[%d]: invalid integer value", line->nr);
}

static void get_d(struct line *line, double *s_in)
{
	char *s;
	char *endptr;

	get_s(line, &s);

	errno = 0;
	*s_in = strtod(s, &endptr);
	if (*endptr || errno == ERANGE)
		die("line[%d]: invalid float value", line->nr);
}

static int pretty;

#define MAX_LINE_LENGTH (64 * 1024)

static char *get_trimmed_line(char *buf, int buf_size)
{
	int len;

	if (!fgets(buf, buf_size, stdin))
		return NULL;

	len = strlen(buf);
	while (len > 0) {
		char c = buf[len - 1];
		if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
			buf[--len] = 0;
		else
			break;
	}

	while (*buf == ' ' || *buf == '\t')
		buf++;

	return buf;
}

static int scripted(void)
{
	struct string_list parts = STRING_LIST_INIT_NODUP;
	struct json_writer jw = JSON_WRITER_INIT;
	char buf[MAX_LINE_LENGTH];
	char *line;
	int line_nr = 0;

	line = get_trimmed_line(buf, MAX_LINE_LENGTH);
	if (!line)
		return 0;

	if (!strcmp(line, "object"))
		jw_object_begin(&jw, pretty);
	else if (!strcmp(line, "array"))
		jw_array_begin(&jw, pretty);
	else
		die("expected first line to be 'object' or 'array'");

	while ((line = get_trimmed_line(buf, MAX_LINE_LENGTH)) != NULL) {
		struct line state = { 0 };
		char *verb;
		char *key;
		char *s_value;
		intmax_t i_value;
		double d_value;

		state.parts = &parts;
		state.nr = ++line_nr;

		/* break line into command and zero or more tokens */
		string_list_setlen(&parts, 0);
		string_list_split_in_place(&parts, line, " ", -1);
		string_list_remove_empty_items(&parts, 0);

		/* ignore empty lines */
		if (!parts.nr || !*parts.items[0].string)
			continue;

		verb = parts.items[state.consumed_nr++].string;

		if (!strcmp(verb, "end")) {
			jw_end(&jw);
		}
		else if (!strcmp(verb, "object-string")) {
			get_s(&state, &key);
			get_s(&state, &s_value);
			jw_object_string(&jw, key, s_value);
		}
		else if (!strcmp(verb, "object-int")) {
			get_s(&state, &key);
			get_i(&state, &i_value);
			jw_object_intmax(&jw, key, i_value);
		}
		else if (!strcmp(verb, "object-double")) {
			get_s(&state, &key);
			get_i(&state, &i_value);
			get_d(&state, &d_value);
			jw_object_double(&jw, key, i_value, d_value);
		}
		else if (!strcmp(verb, "object-true")) {
			get_s(&state, &key);
			jw_object_true(&jw, key);
		}
		else if (!strcmp(verb, "object-false")) {
			get_s(&state, &key);
			jw_object_false(&jw, key);
		}
		else if (!strcmp(verb, "object-null")) {
			get_s(&state, &key);
			jw_object_null(&jw, key);
		}
		else if (!strcmp(verb, "object-object")) {
			get_s(&state, &key);
			jw_object_inline_begin_object(&jw, key);
		}
		else if (!strcmp(verb, "object-array")) {
			get_s(&state, &key);
			jw_object_inline_begin_array(&jw, key);
		}
		else if (!strcmp(verb, "array-string")) {
			get_s(&state, &s_value);
			jw_array_string(&jw, s_value);
		}
		else if (!strcmp(verb, "array-int")) {
			get_i(&state, &i_value);
			jw_array_intmax(&jw, i_value);
		}
		else if (!strcmp(verb, "array-double")) {
			get_i(&state, &i_value);
			get_d(&state, &d_value);
			jw_array_double(&jw, i_value, d_value);
		}
		else if (!strcmp(verb, "array-true"))
			jw_array_true(&jw);
		else if (!strcmp(verb, "array-false"))
			jw_array_false(&jw);
		else if (!strcmp(verb, "array-null"))
			jw_array_null(&jw);
		else if (!strcmp(verb, "array-object"))
			jw_array_inline_begin_object(&jw);
		else if (!strcmp(verb, "array-array"))
			jw_array_inline_begin_array(&jw);
		else
			die("unrecognized token: '%s'", verb);
	}

	if (!jw_is_terminated(&jw))
		die("json not terminated: '%s'", jw.json.buf);

	printf("%s\n", jw.json.buf);

	jw_release(&jw);
	string_list_clear(&parts, 0);
	return 0;
}

int cmd__json_writer(int argc, const char **argv)
{
	argc--; /* skip over "json-writer" arg */
	argv++;

	if (argc > 0 && argv[0][0] == '-') {
		if (!strcmp(argv[0], "-u") || !strcmp(argv[0], "--unit"))
			return unit_tests();

		if (!strcmp(argv[0], "-p") || !strcmp(argv[0], "--pretty"))
			pretty = 1;
	}

	return scripted();
}
