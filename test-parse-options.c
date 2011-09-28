#include "cache.h"
#include "parse-options.h"
#include "string-list.h"

static int boolean = 0;
static int integer = 0;
static unsigned long timestamp;
static int abbrev = 7;
static int verbose = 0, dry_run = 0, quiet = 0;
static char *string = NULL;
static char *file = NULL;
static int ambiguous;
static struct string_list list;

static int length_callback(const struct option *opt, const char *arg, int unset)
{
	printf("Callback: \"%s\", %d\n",
		(arg ? arg : "not set"), unset);
	if (unset)
		return 1; /* do not support unset */

	*(int *)opt->value = strlen(arg);
	return 0;
}

static int number_callback(const struct option *opt, const char *arg, int unset)
{
	*(int *)opt->value = strtol(arg, NULL, 10);
	return 0;
}

int main(int argc, const char **argv)
{
	const char *prefix = "prefix/";
	const char *usage[] = {
		"test-parse-options <options>",
		NULL
	};
	struct option options[] = {
		OPT_BOOLEAN('b', "boolean", &boolean, "get a boolean"),
		OPT_BIT('4', "or4", &boolean,
			"bitwise-or boolean with ...0100", 4),
		OPT_NEGBIT(0, "neg-or4", &boolean, "same as --no-or4", 4),
		OPT_GROUP(""),
		OPT_INTEGER('i', "integer", &integer, "get a integer"),
		OPT_INTEGER('j', NULL, &integer, "get a integer, too"),
		OPT_SET_INT(0, "set23", &integer, "set integer to 23", 23),
		OPT_DATE('t', NULL, &timestamp, "get timestamp of <time>"),
		OPT_CALLBACK('L', "length", &integer, "str",
			"get length of <str>", length_callback),
		OPT_FILENAME('F', "file", &file, "set file to <file>"),
		OPT_GROUP("String options"),
		OPT_STRING('s', "string", &string, "string", "get a string"),
		OPT_STRING(0, "string2", &string, "str", "get another string"),
		OPT_STRING(0, "st", &string, "st", "get another string (pervert ordering)"),
		OPT_STRING('o', NULL, &string, "str", "get another string"),
		OPT_NOOP_NOARG(0, "obsolete"),
		OPT_SET_PTR(0, "default-string", &string,
			"set string to default", (unsigned long)"default"),
		OPT_STRING_LIST(0, "list", &list, "str", "add str to list"),
		OPT_GROUP("Magic arguments"),
		OPT_ARGUMENT("quux", "means --quux"),
		OPT_NUMBER_CALLBACK(&integer, "set integer to NUM",
			number_callback),
		{ OPTION_BOOLEAN, '+', NULL, &boolean, NULL, "same as -b",
		  PARSE_OPT_NOARG | PARSE_OPT_NONEG | PARSE_OPT_NODASH },
		{ OPTION_BOOLEAN, 0, "ambiguous", &ambiguous, NULL,
		  "positive ambiguity", PARSE_OPT_NOARG | PARSE_OPT_NONEG },
		{ OPTION_BOOLEAN, 0, "no-ambiguous", &ambiguous, NULL,
		  "negative ambiguity", PARSE_OPT_NOARG | PARSE_OPT_NONEG },
		OPT_GROUP("Standard options"),
		OPT__ABBREV(&abbrev),
		OPT__VERBOSE(&verbose, "be verbose"),
		OPT__DRY_RUN(&dry_run, "dry run"),
		OPT__QUIET(&quiet, "be quiet"),
		OPT_END(),
	};
	int i;

	argc = parse_options(argc, argv, prefix, options, usage, 0);

	printf("boolean: %d\n", boolean);
	printf("integer: %u\n", integer);
	printf("timestamp: %lu\n", timestamp);
	printf("string: %s\n", string ? string : "(not set)");
	printf("abbrev: %d\n", abbrev);
	printf("verbose: %d\n", verbose);
	printf("quiet: %s\n", quiet ? "yes" : "no");
	printf("dry run: %s\n", dry_run ? "yes" : "no");
	printf("file: %s\n", file ? file : "(not set)");

	for (i = 0; i < list.nr; i++)
		printf("list: %s\n", list.items[i].string);

	for (i = 0; i < argc; i++)
		printf("arg %02d: %s\n", i, argv[i]);

	return 0;
}
