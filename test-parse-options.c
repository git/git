#include "cache.h"
#include "parse-options.h"

static int boolean = 0;
static int integer = 0;
static char *string = NULL;

int main(int argc, const char **argv)
{
	const char *usage[] = {
		"test-parse-options <options>",
		NULL
	};
	struct option options[] = {
		OPT_BOOLEAN('b', "boolean", &boolean, "get a boolean"),
		OPT_INTEGER('i', "integer", &integer, "get a integer"),
		OPT_INTEGER('j', NULL, &integer, "get a integer, too"),
		OPT_GROUP("string options"),
		OPT_STRING('s', "string", &string, "string", "get a string"),
		OPT_STRING(0, "string2", &string, "str", "get another string"),
		OPT_STRING(0, "st", &string, "st", "get another string (pervert ordering)"),
		OPT_STRING('o', NULL, &string, "str", "get another string"),
		OPT_GROUP("magic arguments"),
		OPT_ARGUMENT("quux", "means --quux"),
		OPT_END(),
	};
	int i;

	argc = parse_options(argc, argv, options, usage, 0);

	printf("boolean: %d\n", boolean);
	printf("integer: %d\n", integer);
	printf("string: %s\n", string ? string : "(not set)");

	for (i = 0; i < argc; i++)
		printf("arg %02d: %s\n", i, argv[i]);

	return 0;
}
