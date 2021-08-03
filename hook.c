#include "cache.h"
#include "hook.h"
#include "run-command.h"
#include "hook-list.h"
#include "config.h"

static int known_hook(const char *name)
{
	const char **p;
	size_t len = strlen(name);
	static int test_hooks_ok = -1;

	for (p = hook_name_list; *p; p++) {
		const char *hook = *p;

		if (!strncmp(name, hook, len) && hook[len] == '\0')
			return 1;
	}

	if (test_hooks_ok == -1)
		test_hooks_ok = git_env_bool("GIT_TEST_FAKE_HOOKS", 0);

	if (test_hooks_ok &&
	    (!strcmp(name, "test-hook") ||
	     !strcmp(name, "does-not-exist")))
		return 1;

	return 0;
}

const char *find_hook(const char *name)
{
	static struct strbuf path = STRBUF_INIT;

	if (!known_hook(name))
		die(_("the hook '%s' is not known to git, should be in hook-list.h via githooks(5)"),
		    name);

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

		if (err == EACCES && advice_ignored_hook) {
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

void run_hooks_opt_clear(struct run_hooks_opt *o)
{
	strvec_clear(&o->env);
	strvec_clear(&o->args);
}

static int pick_next_hook(struct child_process *cp,
			  struct strbuf *out,
			  void *pp_cb,
			  void **pp_task_cb)
{
	struct hook_cb_data *hook_cb = pp_cb;
	struct hook *run_me = hook_cb->run_me;

	if (!run_me)
		return 0;

	cp->no_stdin = 1;
	cp->env = hook_cb->options->env.v;
	cp->stdout_to_stderr = 1;
	cp->trace2_hook_name = hook_cb->hook_name;

	/* add command */
	strvec_push(&cp->args, run_me->hook_path);

	/*
	 * add passed-in argv, without expanding - let the user get back
	 * exactly what they put in
	 */
	strvec_pushv(&cp->args, hook_cb->options->args.v);

	/* Provide context for errors if necessary */
	*pp_task_cb = run_me;

	/*
	 * This pick_next_hook() will be called again, we're only
	 * running one hook, so indicate that no more work will be
	 * done.
	 */
	hook_cb->run_me = NULL;

	return 1;
}

static int notify_start_failure(struct strbuf *out,
				void *pp_cb,
				void *pp_task_cp)
{
	struct hook_cb_data *hook_cb = pp_cb;
	struct hook *attempted = pp_task_cp;

	hook_cb->rc |= 1;

	strbuf_addf(out, _("Couldn't start hook '%s'\n"),
		    attempted->hook_path);

	return 1;
}

static int notify_hook_finished(int result,
				struct strbuf *out,
				void *pp_cb,
				void *pp_task_cb)
{
	struct hook_cb_data *hook_cb = pp_cb;

	hook_cb->rc |= result;

	return 0;
}

int run_hooks(const char *hook_name, const char *hook_path,
	      struct run_hooks_opt *options)
{
	struct hook my_hook = {
		.hook_path = hook_path,
	};
	struct hook_cb_data cb_data = {
		.rc = 0,
		.hook_name = hook_name,
		.options = options,
	};
	int jobs = 1;

	if (!options)
		BUG("a struct run_hooks_opt must be provided to run_hooks");

	cb_data.run_me = &my_hook;

	run_processes_parallel_tr2(jobs,
				   pick_next_hook,
				   notify_start_failure,
				   notify_hook_finished,
				   &cb_data,
				   "hook",
				   hook_name);

	return cb_data.rc;
}

int run_hooks_oneshot(const char *hook_name, struct run_hooks_opt *options)
{
	const char *hook_path;
	int ret;
	struct run_hooks_opt hook_opt_scratch = RUN_HOOKS_OPT_INIT;

	if (!options)
		options = &hook_opt_scratch;

	hook_path = find_hook(hook_name);
	if (!hook_path) {
		ret = 0;
		goto cleanup;
	}

	ret = run_hooks(hook_name, hook_path, options);
cleanup:
	run_hooks_opt_clear(options);
	return ret;
}
