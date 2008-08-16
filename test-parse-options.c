#include "cache.h"
#include "parse-options.h"

static int boolean = 0;
static int integer = 0;
static unsigned long timestamp;
static int abbrev = 7;
static int verbose = 0, dry_run = 0, quiet = 0;
static char *string = NULL;

int length_callback(const struct option *opt, const char *arg, int unset)
{
	printf("Callback: \"%s\", %d\n",
		(arg ? arg : "not set"), unset);
	if (unset)
		return 1; /* do not support unset */

	*(int *)opt->value = strlen(arg);
	return 0;
}

int main(int argc, const char **argv)
{
	const char *usage[] = {
		"test-parse-options <options>",
		NULL
	};
	struct option options[] = {
		OPT_BOOLEAN('b', "boolean", &boolean, "get a boolean"),
		OPT_BIT('4', "or4", &boolean,
			"bitwise-or boolean with ...0100", 4),
		OPT_GROUP(""),
		OPT_INTEGER('i', "integer", &integer, "get a integer"),
		OPT_INTEGER('j', NULL, &integer, "get a integer, too"),
		OPT_SET_INT(0, "set23", &integer, "set integer to 23", 23),
		OPT_DATE('t', NULL, &timestamp, "get timestamp of <time>"),
		OPT_CALLBACK('L', "length", &integer, "str",
			"get length of <str>", length_callback),
		OPT_GROUP("String options"),
		OPT_STRING('s', "string", &string, "string", "get a string"),
		OPT_STRING(0, "string2", &string, "str", "get another string"),
		OPT_STRING(0, "st", &string, "st", "get another string (pervert ordering)"),
		OPT_STRING('o', NULL, &string, "str", "get another string"),
		OPT_SET_PTR(0, "default-string", &string,
			"set string to default", (unsigned long)"default"),
		OPT_GROUP("Magic arguments"),
		OPT_ARGUMENT("quux", "means --quux"),
		OPT_GROUP("Standard options"),
		OPT__ABBREV(&abbrev),
		OPT__VERBOSE(&verbose),
		OPT__DRY_RUN(&dry_run),
		OPT__QUIET(&quiet),
		OPT_END(),
	};
	int i;

	argc = parse_options(argc, argv, options, usage, 0);

	printf("boolean: %d\n", boolean);
	printf("integer: %u\n", integer);
	printf("timestamp: %lu\n", timestamp);
	printf("string: %s\n", string ? string : "(not set)");
	printf("abbrev: %d\n", abbrev);
	printf("verbose: %d\n", verbose);
	printf("quiet: %s\n", quiet ? "yes" : "no");
	printf("dry run: %s\n", dry_run ? "yes" : "no");

	for (i = 0; i < argc; i++)
		printf("arg %02d: %s\n", i, argv[i]);

	return 0;
}
