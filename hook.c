#include "git-compat-util.h"
#include "abspath.h"
#include "advice.h"
#include "gettext.h"
#include "hook.h"
#include "path.h"
#include "run-command.h"
#include "config.h"
#include "strbuf.h"

const char *find_hook(const char *name)
{
	static struct strbuf path = STRBUF_INIT;

	strbuf_reset(&path);
	strbuf_git_path(&path, "hooks/%s", name);
	if (access(path.buf, X_OK) < 0) {
		int err = errno;

#ifdef STRIP_EXTENSION
		strbuf_addstr(&path, STRIP_EXTENSION);
		if (access(path.buf, X_OK) >= 0)
			return path.buf;
		if (errno == EACCES)
			err = errno;
#endif

		if (err == EACCES && advice_enabled(ADVICE_IGNORED_HOOK)) {
			static struct string_list advise_given = STRING_LIST_INIT_DUP;

			if (!string_list_lookup(&advise_given, name)) {
				string_list_insert(&advise_given, name);
				advise(_("The '%s' hook was ignored because "
					 "it's not set as executable.\n"
					 "You can disable this warning with "
					 "`git config advice.ignoredHook false`."),
				       path.buf);
			}
		}
		return NULL;
	}
	return path.buf;
}

int hook_exists(const char *name)
{
	return !!find_hook(name);
}

static int pick_next_hook(struct child_process *cp,
			  struct strbuf *out UNUSED,
			  void *pp_cb,
			  void **pp_task_cb UNUSED)
{
	struct hook_cb_data *hook_cb = pp_cb;
	const char *hook_path = hook_cb->hook_path;

	if (!hook_path)
		return 0;

	cp->no_stdin = 1;
	strvec_pushv(&cp->env, hook_cb->options->env.v);
	/* reopen the file for stdin; run_command closes it. */
	if (hook_cb->options->path_to_stdin) {
		cp->no_stdin = 0;
		cp->in = xopen(hook_cb->options->path_to_stdin, O_RDONLY);
	}
	cp->stdout_to_stderr = 1;
	cp->trace2_hook_name = hook_cb->hook_name;
	cp->dir = hook_cb->options->dir;

	strvec_push(&cp->args, hook_path);
	strvec_pushv(&cp->args, hook_cb->options->args.v);

	/*
	 * This pick_next_hook() will be called again, we're only
	 * running one hook, so indicate that no more work will be
	 * done.
	 */
	hook_cb->hook_path = NULL;

	return 1;
}

static int notify_start_failure(struct strbuf *out UNUSED,
				void *pp_cb,
				void *pp_task_cp UNUSED)
{
	struct hook_cb_data *hook_cb = pp_cb;

	hook_cb->rc |= 1;

	return 1;
}

static int notify_hook_finished(int result,
				struct strbuf *out UNUSED,
				void *pp_cb,
				void *pp_task_cb UNUSED)
{
	struct hook_cb_data *hook_cb = pp_cb;
	struct run_hooks_opt *opt = hook_cb->options;

	hook_cb->rc |= result;

	if (opt->invoked_hook)
		*opt->invoked_hook = 1;

	return 0;
}

static void run_hooks_opt_clear(struct run_hooks_opt *options)
{
	strvec_clear(&options->env);
	strvec_clear(&options->args);
}

int run_hooks_opt(const char *hook_name, struct run_hooks_opt *options)
{
	struct strbuf abs_path = STRBUF_INIT;
	struct hook_cb_data cb_data = {
		.rc = 0,
		.hook_name = hook_name,
		.options = options,
	};
	const char *const hook_path = find_hook(hook_name);
	int ret = 0;
	const struct run_process_parallel_opts opts = {
		.tr2_category = "hook",
		.tr2_label = hook_name,

		.processes = 1,
		.ungroup = 1,

		.get_next_task = pick_next_hook,
		.start_failure = notify_start_failure,
		.task_finished = notify_hook_finished,

		.data = &cb_data,
	};

	if (!options)
		BUG("a struct run_hooks_opt must be provided to run_hooks");

	if (options->invoked_hook)
		*options->invoked_hook = 0;

	if (!hook_path && !options->error_if_missing)
		goto cleanup;

	if (!hook_path) {
		ret = error("cannot find a hook named %s", hook_name);
		goto cleanup;
	}

	cb_data.hook_path = hook_path;
	if (options->dir) {
		strbuf_add_absolute_path(&abs_path, hook_path);
		cb_data.hook_path = abs_path.buf;
	}

	run_processes_parallel(&opts);
	ret = cb_data.rc;
cleanup:
	strbuf_release(&abs_path);
	run_hooks_opt_clear(options);
	return ret;
}

int run_hooks(const char *hook_name)
{
	struct run_hooks_opt opt = RUN_HOOKS_OPT_INIT;

	return run_hooks_opt(hook_name, &opt);
}

int run_hooks_l(const char *hook_name, ...)
{
	struct run_hooks_opt opt = RUN_HOOKS_OPT_INIT;
	va_list ap;
	const char *arg;

	va_start(ap, hook_name);
	while ((arg = va_arg(ap, const char *)))
		strvec_push(&opt.args, arg);
	va_end(ap);

	return run_hooks_opt(hook_name, &opt);
}
