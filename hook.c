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

int pipe_from_string_list(struct strbuf *pipe, void *pp_cb, void *pp_task_cb)
{
	int *item_idx;
	struct hook *ctx = pp_task_cb;
	struct hook_cb_data *hook_cb = pp_cb;
	struct string_list *to_pipe = hook_cb->options->feed_pipe_ctx;

	/* Bootstrap the state manager if necessary. */
	if (!ctx->feed_pipe_cb_data) {
		ctx->feed_pipe_cb_data = xmalloc(sizeof(unsigned int));
		*(int*)ctx->feed_pipe_cb_data = 0;
	}

	item_idx = ctx->feed_pipe_cb_data;

	if (*item_idx < to_pipe->nr) {
		strbuf_addf(pipe, "%s\n", to_pipe->items[*item_idx].string);
		(*item_idx)++;
		return 0;
	}
	return 1;
}

static int pick_next_hook(struct child_process *cp,
			  struct strbuf *out,
			  void *pp_cb,
			  void **pp_task_cb)
{
	struct hook_cb_data *hook_cb = pp_cb;
	struct hook *run_me = hook_cb->run_me;


	/* reopen the file for stdin; run_command closes it. */
	if (hook_cb->options->path_to_stdin) {
		cp->no_stdin = 0;
		cp->in = xopen(hook_cb->options->path_to_stdin, O_RDONLY);
	} else if (hook_cb->options->feed_pipe) {
		/* ask for start_command() to make a pipe for us */
		cp->in = -1;
		cp->no_stdin = 0;
	} else {
		cp->no_stdin = 1;
	}
	cp->env = hook_cb->options->env.v;
	cp->stdout_to_stderr = 1;
	cp->trace2_hook_name = hook_cb->hook_name;
	cp->dir = hook_cb->options->dir;

	/* add command */
	strvec_push(&cp->args, run_me->hook_path);

	/*
	 * add passed-in argv, without expanding - let the user get back
	 * exactly what they put in
	 */
	strvec_pushv(&cp->args, hook_cb->options->args.v);

	/* Provide context for errors if necessary */
	*pp_task_cb = run_me;

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

	if (hook_cb->invoked_hook)
		*hook_cb->invoked_hook = 1;

	return 1;
}

int run_found_hooks(const char *hook_name, const char *hook_path,
		    struct run_hooks_opt *options)
{
	struct strbuf abs_path = STRBUF_INIT;
	struct hook my_hook = {
		.hook_path = hook_path,
	};
	struct hook_cb_data cb_data = {
		.rc = 0,
		.hook_name = hook_name,
		.options = options,
		.invoked_hook = options->invoked_hook,
	};
	if (options->absolute_path) {
		strbuf_add_absolute_path(&abs_path, hook_path);
		my_hook.hook_path = abs_path.buf;
	}
	cb_data.run_me = &my_hook;

	if (options->jobs != 1)
		BUG("we do not handle %d or any other != 1 job number yet", options->jobs);

	run_processes_parallel_tr2(options->jobs,
				   pick_next_hook,
				   notify_start_failure,
				   options->feed_pipe,
				   options->consume_sideband,
				   notify_hook_finished,
				   &cb_data,
				   "hook",
				   hook_name);
	if (options->absolute_path)
		strbuf_release(&abs_path);

	return cb_data.rc;
}

int run_hooks(const char *hook_name, struct run_hooks_opt *options)
{
	const char *hook_path;
	int ret;
	if (!options)
		BUG("a struct run_hooks_opt must be provided to run_hooks");

	if (options->path_to_stdin && options->feed_pipe)
		BUG("choose only one method to populate stdin");

	hook_path = find_hook(hook_name);

	/*
	 * If you need to act on a missing hook, use run_found_hooks()
	 * instead
	 */
	if (!hook_path)
		return 0;

	ret = run_found_hooks(hook_name, hook_path, options);
	return ret;
}
