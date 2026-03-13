#define USE_THE_REPOSITORY_VARIABLE
#include "builtin.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hook.h"
#include "parse-options.h"

#define BUILTIN_HOOK_RUN_USAGE \
	N_("git hook run [--ignore-missing] [--to-stdin=<path>] <hook-name> [-- <hook-args>]")
#define BUILTIN_HOOK_LIST_USAGE \
	N_("git hook list [-z] [--show-scope] <hook-name>")

static const char * const builtin_hook_usage[] = {
	BUILTIN_HOOK_RUN_USAGE,
	BUILTIN_HOOK_LIST_USAGE,
	NULL
};

static const char * const builtin_hook_run_usage[] = {
	BUILTIN_HOOK_RUN_USAGE,
	NULL
};

static int list(int argc, const char **argv, const char *prefix,
		 struct repository *repo)
{
	static const char *const builtin_hook_list_usage[] = {
		BUILTIN_HOOK_LIST_USAGE,
		NULL
	};
	struct string_list *head;
	struct string_list_item *item;
	const char *hookname = NULL;
	int line_terminator = '\n';
	int show_scope = 0;
	int ret = 0;

	struct option list_options[] = {
		OPT_SET_INT('z', NULL, &line_terminator,
			    N_("use NUL as line terminator"), '\0'),
		OPT_BOOL(0, "show-scope", &show_scope,
			 N_("show the config scope that defined each hook")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, list_options,
			     builtin_hook_list_usage, 0);

	/*
	 * The only unnamed argument provided should be the hook-name; if we add
	 * arguments later they probably should be caught by parse_options.
	 */
	if (argc != 1)
		usage_msg_opt(_("you must specify a hook event name to list."),
			      builtin_hook_list_usage, list_options);

	hookname = argv[0];

	head = list_hooks(repo, hookname, NULL);

	if (!head->nr) {
		warning(_("no hooks found for event '%s'"), hookname);
		ret = 1; /* no hooks found */
		goto cleanup;
	}

	for_each_string_list_item(item, head) {
		struct hook *h = item->util;

		switch (h->kind) {
		case HOOK_TRADITIONAL:
			printf("%s%c", _("hook from hookdir"), line_terminator);
			break;
		case HOOK_CONFIGURED: {
			const char *name = h->u.configured.friendly_name;
			const char *scope = show_scope ?
				config_scope_name(h->u.configured.scope) : NULL;
			if (scope)
				printf("%s (%s%s)%c", name, scope,
				       h->u.configured.disabled ? ", disabled" : "",
				       line_terminator);
			else if (h->u.configured.disabled)
				printf("%s (disabled)%c", name, line_terminator);
			else
				printf("%s%c", name, line_terminator);
			break;
		}
		default:
			BUG("unknown hook kind");
		}
	}

cleanup:
	string_list_clear_func(head, hook_free);
	free(head);
	return ret;
}

static int run(int argc, const char **argv, const char *prefix,
	       struct repository *repo UNUSED)
{
	int i;
	struct run_hooks_opt opt = RUN_HOOKS_OPT_INIT;
	int ignore_missing = 0;
	const char *hook_name;
	struct option run_options[] = {
		OPT_BOOL(0, "ignore-missing", &ignore_missing,
			 N_("silently ignore missing requested <hook-name>")),
		OPT_STRING(0, "to-stdin", &opt.path_to_stdin, N_("path"),
			   N_("file to read into hooks' stdin")),
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
	repo_config(the_repository, git_default_config, NULL);

	hook_name = argv[0];
	if (!ignore_missing)
		opt.error_if_missing = 1;
	ret = run_hooks_opt(the_repository, hook_name, &opt);
	if (ret < 0) /* error() return */
		ret = 1;
	return ret;
usage:
	usage_with_options(builtin_hook_run_usage, run_options);
}

int cmd_hook(int argc,
	     const char **argv,
	     const char *prefix,
	     struct repository *repo)
{
	parse_opt_subcommand_fn *fn = NULL;
	struct option builtin_hook_options[] = {
		OPT_SUBCOMMAND("run", &fn, run),
		OPT_SUBCOMMAND("list", &fn, list),
		OPT_END(),
	};

	argc = parse_options(argc, argv, NULL, builtin_hook_options,
			     builtin_hook_usage, 0);

	return fn(argc, argv, prefix, repo);
}
