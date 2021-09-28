#include "test-tool.h"
#include "cache.h"
#include "parse-options.h"
#include "string-list.h"
#include "trace2.h"

static int boolean = 0;
static int integer = 0;
static unsigned long magnitude = 0;
static timestamp_t timestamp;
static int abbrev = 7;
static int verbose = -1; /* unspecified */
static int dry_run = 0, quiet = 0;
static char *string = NULL;
static char *file = NULL;
static int ambiguous;
static struct string_list list = STRING_LIST_INIT_NODUP;

static struct {
	int called;
	const char *arg;
	int unset;
} length_cb;

static int length_callback(const struct option *opt, const char *arg, int unset)
{
	length_cb.called = 1;
	length_cb.arg = arg;
	length_cb.unset = unset;

	if (unset)
		return 1; /* do not support unset */

	*(int *)opt->value = strlen(arg);
	return 0;
}

static int number_callback(const struct option *opt, const char *arg, int unset)
{
	BUG_ON_OPT_NEG(unset);
	*(int *)opt->value = strtol(arg, NULL, 10);
	return 0;
}

static int collect_expect(const struct option *opt, const char *arg, int unset)
{
	struct string_list *expect;
	struct string_list_item *item;
	struct strbuf label = STRBUF_INIT;
	const char *colon;

	if (!arg || unset)
		die("malformed --expect option");

	expect = (struct string_list *)opt->value;
	colon = strchr(arg, ':');
	if (!colon)
		die("malformed --expect option, lacking a colon");
	strbuf_add(&label, arg, colon - arg);
	item = string_list_insert(expect, strbuf_detach(&label, NULL));
	if (item->util)
		die("malformed --expect option, duplicate %s", label.buf);
	item->util = (void *)arg;
	return 0;
}

__attribute__((format (printf,3,4)))
static void show(struct string_list *expect, int *status, const char *fmt, ...)
{
	struct string_list_item *item;
	struct strbuf buf = STRBUF_INIT;
	va_list args;

	va_start(args, fmt);
	strbuf_vaddf(&buf, fmt, args);
	va_end(args);

	if (!expect->nr)
		printf("%s\n", buf.buf);
	else {
		char *colon = strchr(buf.buf, ':');
		if (!colon)
			die("malformed output format, output lacking colon: %s", fmt);
		*colon = '\0';
		item = string_list_lookup(expect, buf.buf);
		*colon = ':';
		if (!item)
			; /* not among entries being checked */
		else {
			if (strcmp((const char *)item->util, buf.buf)) {
				printf("-%s\n", (char *)item->util);
				printf("+%s\n", buf.buf);
				*status = 1;
			}
		}
	}
	strbuf_release(&buf);
}

int cmd__parse_options(int argc, const char **argv)
{
	const char *prefix = "prefix/";
	const char *usage[] = {
		"test-tool parse-options <options>",
		"",
		"A helper function for the parse-options API.",
		NULL
	};
	struct string_list expect = STRING_LIST_INIT_NODUP;
	struct option options[] = {
		OPT_BOOL(0, "yes", &boolean, "get a boolean"),
		OPT_BOOL('D', "no-doubt", &boolean, "begins with 'no-'"),
		{ OPTION_SET_INT, 'B', "no-fear", &boolean, NULL,
		  "be brave", PARSE_OPT_NOARG | PARSE_OPT_NONEG, NULL, 1 },
		OPT_COUNTUP('b', "boolean", &boolean, "increment by one"),
		OPT_BIT('4', "or4", &boolean,
			"bitwise-or boolean with ...0100", 4),
		OPT_NEGBIT(0, "neg-or4", &boolean, "same as --no-or4", 4),
		OPT_GROUP(""),
		OPT_INTEGER('i', "integer", &integer, "get a integer"),
		OPT_INTEGER('j', NULL, &integer, "get a integer, too"),
		OPT_MAGNITUDE('m', "magnitude", &magnitude, "get a magnitude"),
		OPT_SET_INT(0, "set23", &integer, "set integer to 23", 23),
		OPT_CMDMODE(0, "mode1", &integer, "set integer to 1 (cmdmode option)", 1),
		OPT_CMDMODE(0, "mode2", &integer, "set integer to 2 (cmdmode option)", 2),
		OPT_CALLBACK('L', "length", &integer, "str",
			"get length of <str>", length_callback),
		OPT_FILENAME('F', "file", &file, "set file to <file>"),
		OPT_GROUP("String options"),
		OPT_STRING('s', "string", &string, "string", "get a string"),
		OPT_STRING(0, "string2", &string, "str", "get another string"),
		OPT_STRING(0, "st", &string, "st", "get another string (pervert ordering)"),
		OPT_STRING('o', NULL, &string, "str", "get another string"),
		OPT_NOOP_NOARG(0, "obsolete"),
		OPT_STRING_LIST(0, "list", &list, "str", "add str to list"),
		OPT_GROUP("Magic arguments"),
		OPT_NUMBER_CALLBACK(&integer, "set integer to NUM",
			number_callback),
		{ OPTION_COUNTUP, '+', NULL, &boolean, NULL, "same as -b",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG | PARSE_OPT_NODASH },
		{ OPTION_COUNTUP, 0, "ambiguous", &ambiguous, NULL,
		  "positive ambiguity", PARSE_OPT_NOARG | PARSE_OPT_NONEG },
		{ OPTION_COUNTUP, 0, "no-ambiguous", &ambiguous, NULL,
		  "negative ambiguity", PARSE_OPT_NOARG | PARSE_OPT_NONEG },
		OPT_GROUP("Standard options"),
		OPT__ABBREV(&abbrev),
		OPT__VERBOSE(&verbose, "be verbose"),
		OPT__DRY_RUN(&dry_run, "dry run"),
		OPT__QUIET(&quiet, "be quiet"),
		OPT_CALLBACK(0, "expect", &expect, "string",
			     "expected output in the variable dump",
			     collect_expect),
		OPT_GROUP("Alias"),
		OPT_STRING('A', "alias-source", &string, "string", "get a string"),
		OPT_ALIAS('Z', "alias-target", "alias-source"),
		OPT_END(),
	};
	int i;
	int ret = 0;

	trace2_cmd_name("_parse_");

	argc = parse_options(argc, (const char **)argv, prefix, options, usage, 0);

	if (length_cb.called) {
		const char *arg = length_cb.arg;
		int unset = length_cb.unset;
		show(&expect, &ret, "Callback: \"%s\", %d",
		     (arg ? arg : "not set"), unset);
	}
	show(&expect, &ret, "boolean: %d", boolean);
	show(&expect, &ret, "integer: %d", integer);
	show(&expect, &ret, "magnitude: %lu", magnitude);
	show(&expect, &ret, "timestamp: %"PRItime, timestamp);
	show(&expect, &ret, "string: %s", string ? string : "(not set)");
	show(&expect, &ret, "abbrev: %d", abbrev);
	show(&expect, &ret, "verbose: %d", verbose);
	show(&expect, &ret, "quiet: %d", quiet);
	show(&expect, &ret, "dry run: %s", dry_run ? "yes" : "no");
	show(&expect, &ret, "file: %s", file ? file : "(not set)");

	for (i = 0; i < list.nr; i++)
		show(&expect, &ret, "list: %s", list.items[i].string);

	for (i = 0; i < argc; i++)
		show(&expect, &ret, "arg %02d: %s", i, argv[i]);

	return ret;
}
