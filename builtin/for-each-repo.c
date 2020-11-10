#include "cache.h"
#include "config.h"
#include "builtin.h"
#include "parse-options.h"
#include "run-command.h"
#include "string-list.h"

static const char * const for_each_repo_usage[] = {
	N_("git for-each-repo --config=<config> <command-args>"),
	NULL
};

static int run_command_on_repo(const char *path,
			       void *cbdata)
{
	int i;
	struct child_process child = CHILD_PROCESS_INIT;
	struct strvec *args = (struct strvec *)cbdata;

	child.git_cmd = 1;
	strvec_pushl(&child.args, "-C", path, NULL);

	for (i = 0; i < args->nr; i++)
		strvec_push(&child.args, args->v[i]);

	return run_command(&child);
}

int cmd_for_each_repo(int argc, const char **argv, const char *prefix)
{
	static const char *config_key = NULL;
	int i, result = 0;
	const struct string_list *values;
	struct strvec args = STRVEC_INIT;

	const struct option options[] = {
		OPT_STRING(0, "config", &config_key, N_("config"),
			   N_("config key storing a list of repository paths")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options, for_each_repo_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (!config_key)
		die(_("missing --config=<config>"));

	for (i = 0; i < argc; i++)
		strvec_push(&args, argv[i]);

	values = repo_config_get_value_multi(the_repository,
					     config_key);

	for (i = 0; !result && i < values->nr; i++)
		result = run_command_on_repo(values->items[i].string, &args);

	return result;
}
