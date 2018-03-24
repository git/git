#include "git-compat-util.h"
#include "test-tool.h"

struct test_cmd {
	const char *name;
	int (*fn)(int argc, const char **argv);
};

static struct test_cmd cmds[] = {
	{ "chmtime", cmd__chmtime },
	{ "config", cmd__config },
	{ "ctype", cmd__ctype },
	{ "lazy-init-name-hash", cmd__lazy_init_name_hash },
	{ "sha1", cmd__sha1 },
};

int cmd_main(int argc, const char **argv)
{
	int i;

	if (argc < 2)
		die("I need a test name!");

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (!strcmp(cmds[i].name, argv[1])) {
			argv++;
			argc--;
			return cmds[i].fn(argc, argv);
		}
	}
	die("There is no test named '%s'", argv[1]);
}
