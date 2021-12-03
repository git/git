/*
 * The Scalar command-line interface.
 */

#include "cache.h"
#include "gettext.h"
#include "parse-options.h"

static struct {
	const char *name;
	int (*fn)(int, const char **);
} builtins[] = {
	{ NULL, NULL},
};

int cmd_main(int argc, const char **argv)
{
	struct strbuf scalar_usage = STRBUF_INIT;
	int i;

	if (argc > 1) {
		argv++;
		argc--;

		for (i = 0; builtins[i].name; i++)
			if (!strcmp(builtins[i].name, argv[0]))
				return !!builtins[i].fn(argc, argv);
	}

	strbuf_addstr(&scalar_usage,
		      N_("scalar <command> [<options>]\n\nCommands:\n"));
	for (i = 0; builtins[i].name; i++)
		strbuf_addf(&scalar_usage, "\t%s\n", builtins[i].name);

	usage(scalar_usage.buf);
}
