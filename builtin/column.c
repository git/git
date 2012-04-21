#include "builtin.h"
#include "cache.h"
#include "strbuf.h"
#include "parse-options.h"
#include "string-list.h"
#include "column.h"

static const char * const builtin_column_usage[] = {
	"git column [options]",
	NULL
};
static unsigned int colopts;

static int column_config(const char *var, const char *value, void *cb)
{
	return git_column_config(var, value, cb, &colopts);
}

int cmd_column(int argc, const char **argv, const char *prefix)
{
	struct string_list list = STRING_LIST_INIT_DUP;
	struct strbuf sb = STRBUF_INIT;
	struct column_options copts;
	const char *command = NULL, *real_command = NULL;
	struct option options[] = {
		OPT_STRING(0, "command", &real_command, "name", "lookup config vars"),
		OPT_COLUMN(0, "mode", &colopts, "layout to use"),
		OPT_INTEGER(0, "raw-mode", &colopts, "layout to use"),
		OPT_INTEGER(0, "width", &copts.width, "Maximum width"),
		OPT_STRING(0, "indent", &copts.indent, "string", "Padding space on left border"),
		OPT_INTEGER(0, "nl", &copts.nl, "Padding space on right border"),
		OPT_INTEGER(0, "padding", &copts.padding, "Padding space between columns"),
		OPT_END()
	};

	/* This one is special and must be the first one */
	if (argc > 1 && !prefixcmp(argv[1], "--command=")) {
		command = argv[1] + 10;
		git_config(column_config, (void *)command);
	} else
		git_config(column_config, NULL);

	memset(&copts, 0, sizeof(copts));
	copts.width = term_columns();
	copts.padding = 1;
	argc = parse_options(argc, argv, "", options, builtin_column_usage, 0);
	if (argc)
		usage_with_options(builtin_column_usage, options);
	if (real_command || command) {
		if (!real_command || !command || strcmp(real_command, command))
			die(_("--command must be the first argument"));
	}
	finalize_colopts(&colopts, -1);
	while (!strbuf_getline(&sb, stdin, '\n'))
		string_list_append(&list, sb.buf);

	print_columns(&list, colopts, &copts);
	return 0;
}
