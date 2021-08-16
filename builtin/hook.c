#include "cache.h"
#include "builtin.h"
#include "config.h"
#include "hook.h"
#include "parse-options.h"
#include "strbuf.h"
#include "strvec.h"

#define BUILTIN_HOOK_RUN_USAGE \
	N_("git hook run [--ignore-missing] [--to-stdin=<path>] <hook-name> [-- <hook-args>]")
#define BUILTIN_HOOK_LIST_USAGE \
	N_("git hook list <hook-name>")

static const char * const builtin_hook_usage[] = {
	BUILTIN_HOOK_LIST_USAGE,
	BUILTIN_HOOK_RUN_USAGE,
	NULL
};

static const char * const builtin_hook_run_usage[] = {
	BUILTIN_HOOK_RUN_USAGE,
	NULL
};

static const char *const builtin_hook_list_usage[] = {
	BUILTIN_HOOK_LIST_USAGE,
	NULL
};

static int list(int argc, const char **argv, const char *prefix)
{
	struct list_head *head, *pos;
	const char *hookname = NULL;
	struct strbuf hookdir_annotation = STRBUF_INIT;

	struct option list_options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, list_options,
			     builtin_hook_list_usage, 0);

	if (argc < 1)
		usage_msg_opt(_("You must specify a hook event name to list."),
			      builtin_hook_list_usage, list_options);

	hookname = argv[0];

	head = hook_list(hookname, 1);

	if (list_empty(head)) {
		printf(_("no hooks configured for event '%s'\n"),
		       hookname);
		return 0;
	}

	list_for_each(pos, head) {
		struct hook *item = list_entry(pos, struct hook, list);
		item = list_entry(pos, struct hook, list);
		if (item)
			printf("%s\n", item->name ? item->name
						  : _("hook from hookdir"));
	}

	clear_hook_list(head);
	strbuf_release(&hookdir_annotation);

	return 0;
}
static int run(int argc, const char **argv, const char *prefix)
{
	int i;
	struct run_hooks_opt opt = RUN_HOOKS_OPT_INIT_SYNC;
	int ignore_missing = 0;
	const char *hook_name;
	struct list_head *hooks;

	struct option run_options[] = {
		OPT_BOOL(0, "ignore-missing", &ignore_missing,
			 N_("exit quietly with a zero exit code if the requested hook cannot be found")),
		OPT_STRING(0, "to-stdin", &opt.path_to_stdin, N_("path"),
			   N_("file to read into hooks' stdin")),
		OPT_INTEGER('j', "jobs", &opt.jobs,
			    N_("run up to <n> hooks simultaneously")),
		OPT_END(),
	};
	int ret;

	argc = parse_options(argc, argv, prefix, run_options,
			     builtin_hook_run_usage,
			     PARSE_OPT_KEEP_DASHDASH);

	if (!argc)
		goto usage;

	/*
	 * Having a -- for "run" when providing <hook-args> is
	 * mandatory.
	 */
	if (argc > 1 && strcmp(argv[1], "--") &&
	    strcmp(argv[1], "--end-of-options"))
		goto usage;

	/* Add our arguments, start after -- */
	for (i = 2 ; i < argc; i++)
		strvec_push(&opt.args, argv[i]);

	/* Need to take into account core.hooksPath */
	git_config(git_default_config, NULL);

	hook_name = argv[0];
	hooks = hook_list(hook_name, 1);
	if (list_empty(hooks)) {
		/* ... act like run_hooks_oneshot() under --ignore-missing */
		if (ignore_missing)
			return 0;
		error("cannot find a hook named %s", hook_name);
		return 1;
	}

	ret = run_hooks(hook_name, hooks, &opt);
	run_hooks_opt_clear(&opt);
	return ret;
usage:
	usage_with_options(builtin_hook_run_usage, run_options);
}

int cmd_hook(int argc, const char **argv, const char *prefix)
{
	struct option builtin_hook_options[] = {
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL, builtin_hook_options,
			     builtin_hook_usage, PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		goto usage;

	if (!strcmp(argv[0], "list"))
		return list(argc, argv, prefix);
	if (!strcmp(argv[0], "run"))
		return run(argc, argv, prefix);

usage:
	usage_with_options(builtin_hook_usage, builtin_hook_options);
}
