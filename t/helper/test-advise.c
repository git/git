#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "advice.h"
#include "config.h"
#include "setup.h"

int cmd__advise_if_enabled(int argc, const char **argv)
{
	if (argc != 2)
		die("usage: %s <advice>", argv[0]);

	setup_git_directory();
	git_config(git_default_config, NULL);

	/*
	 * Any advice type can be used for testing, but NESTED_TAG was
	 * selected here and in t0018 where this command is being
	 * executed.
	 */
	advise_if_enabled(ADVICE_NESTED_TAG, "%s", argv[1]);

	return 0;
}
