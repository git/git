#include "test-tool.h"
#include "test-tool-utils.h"
#include "cache.h"
#include "parse-options.h"
#include "submodule-config.h"
#include "submodule.h"

#define TEST_TOOL_CHECK_NAME_USAGE \
	"test-tool submodule check-name <name>"
static const char *submodule_check_name_usage[] = {
	TEST_TOOL_CHECK_NAME_USAGE,
	NULL
};

#define TEST_TOOL_IS_ACTIVE_USAGE \
	"test-tool submodule is-active <name>"
static const char *submodule_is_active_usage[] = {
	TEST_TOOL_IS_ACTIVE_USAGE,
	NULL
};

static const char *submodule_usage[] = {
	TEST_TOOL_CHECK_NAME_USAGE,
	TEST_TOOL_IS_ACTIVE_USAGE,
	NULL
};

/*
 * Exit non-zero if any of the submodule names given on the command line is
 * invalid. If no names are given, filter stdin to print only valid names
 * (which is primarily intended for testing).
 */
static int check_name(int argc, const char **argv)
{
	if (argc > 1) {
		while (*++argv) {
			if (check_submodule_name(*argv) < 0)
				return 1;
		}
	} else {
		struct strbuf buf = STRBUF_INIT;
		while (strbuf_getline(&buf, stdin) != EOF) {
			if (!check_submodule_name(buf.buf))
				printf("%s\n", buf.buf);
		}
		strbuf_release(&buf);
	}
	return 0;
}

static int cmd__submodule_check_name(int argc, const char **argv)
{
	struct option options[] = {
		OPT_END()
	};
	argc = parse_options(argc, argv, "test-tools", options,
			     submodule_check_name_usage, 0);
	if (argc)
		usage_with_options(submodule_check_name_usage, options);

	return check_name(argc, argv);
}

static int cmd__submodule_is_active(int argc, const char **argv)
{
	struct option options[] = {
		OPT_END()
	};
	argc = parse_options(argc, argv, "test-tools", options,
			     submodule_is_active_usage, 0);
	if (argc != 1)
		usage_with_options(submodule_is_active_usage, options);

	setup_git_directory();

	return !is_submodule_active(the_repository, argv[0]);
}

static struct test_cmd cmds[] = {
	{ "check-name", cmd__submodule_check_name },
	{ "is-active", cmd__submodule_is_active },
};

int cmd__submodule(int argc, const char **argv)
{
	struct option options[] = {
		OPT_END()
	};
	size_t i;

	argc = parse_options(argc, argv, "test-tools", options, submodule_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc < 1)
		usage_with_options(submodule_usage, options);

	for (i = 0; i < ARRAY_SIZE(cmds); i++)
		if (!strcmp(cmds[i].name, argv[0]))
			return cmds[i].fn(argc, argv);

	usage_msg_optf("unknown subcommand '%s'", submodule_usage, options,
		       argv[0]);

	return 0;
}
