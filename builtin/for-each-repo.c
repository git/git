#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "config.h"
#include "gettext.h"
#include "parse-options.h"
#include "path.h"
#include "run-command.h"
#include "string-list.h"

static const char * const for_each_repo_usage[] = {
	N_("git for-each-repo --config=<config> [--] <arguments>"),
	NULL
};

static int run_command_on_repo(const char *path, int argc, const char ** argv)
{
	int i;
	struct child_process child = CHILD_PROCESS_INIT;
	char *abspath = interpolate_path(path, 0);

	child.git_cmd = 1;
	strvec_pushl(&child.args, "-C", abspath, NULL);

	for (i = 0; i < argc; i++)
		strvec_push(&child.args, argv[i]);

	free(abspath);

	return run_command(&child);
}

int cmd_for_each_repo(int argc,
		      const char **argv,
		      const char *prefix,
		      struct repository *repo UNUSED)
{
	static const char *config_key = NULL;
	int keep_going = 0;
	int result = 0;
	const struct string_list *values;
	int err;

	const struct option options[] = {
		OPT_STRING(0, "config", &config_key, N_("config"),
			   N_("config key storing a list of repository paths")),
		OPT_BOOL(0, "keep-going", &keep_going,
			 N_("keep going even if command fails in a repository")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, for_each_repo_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (!config_key)
		die(_("missing --config=<config>"));

	err = repo_config_get_string_multi(the_repository, config_key, &values);
	if (err < 0)
		usage_msg_optf(_("got bad config --config=%s"),
			       for_each_repo_usage, options, config_key);
	else if (err)
		return 0;

	for (size_t i = 0; i < values->nr; i++) {
		int ret = run_command_on_repo(values->items[i].string, argc, argv);
		if (ret) {
			if (!keep_going)
					return ret;
			result = 1;
		}
	}

	return result;
}
