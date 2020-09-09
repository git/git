#include "cache.h"

#include "hook.h"
#include "config.h"
#include "run-command.h"

/*
 * NEEDSWORK: a stateful hook_head means we can't run two hook events in the
 * background at the same time - which might be ok, or might not.
 *
 * Maybe it's better to cache a list head per hookname, since we can probably
 * guess that the hook list won't change during a user-initiated operation. For
 * now, within list_hooks, call clear_hook_list() at the outset.
 */
static LIST_HEAD(hook_head);

void free_hook(struct hook *ptr)
{
	if (ptr) {
		strbuf_release(&ptr->command);
		free(ptr);
	}
}

static void emplace_hook(struct list_head *pos, const char *command, int quoted)
{
	struct hook *to_add = malloc(sizeof(struct hook));
	to_add->origin = current_config_scope();
	strbuf_init(&to_add->command, 0);
	if (quoted)
		strbuf_addf(&to_add->command, "'%s'", command);
	else
		strbuf_addstr(&to_add->command, command);

	list_add_tail(&to_add->list, pos);
}

static void remove_hook(struct list_head *to_remove)
{
	struct hook *hook_to_remove = list_entry(to_remove, struct hook, list);
	list_del(to_remove);
	free_hook(hook_to_remove);
}

void clear_hook_list(void)
{
	struct list_head *pos, *tmp;
	list_for_each_safe(pos, tmp, &hook_head)
		remove_hook(pos);
}

static int hook_config_lookup(const char *key, const char *value, void *hook_key_cb)
{
	const char *hook_key = hook_key_cb;

	if (!strcmp(key, hook_key)) {
		const char *command = value;
		struct strbuf hookcmd_name = STRBUF_INIT;
		struct list_head *pos = NULL, *tmp = NULL;

		/* Check if a hookcmd with that name exists. */
		strbuf_addf(&hookcmd_name, "hookcmd.%s.command", command);
		git_config_get_value(hookcmd_name.buf, &command);

		if (!command)
			BUG("git_config_get_value overwrote a string it shouldn't have");

		/*
		 * TODO: implement an option-getting callback, e.g.
		 *   get configs by pattern hookcmd.$value.*
		 *   for each key+value, do_callback(key, value, cb_data)
		 */

		list_for_each_safe(pos, tmp, &hook_head) {
			struct hook *hook = list_entry(pos, struct hook, list);
			/*
			 * The list of hooks to run can be reordered by being redeclared
			 * in the config. Options about hook ordering should be checked
			 * here.
			 */
			if (0 == strcmp(hook->command.buf, command))
				remove_hook(pos);
		}
		emplace_hook(pos, command, 0);
	}

	return 0;
}

struct list_head* hook_list(const struct strbuf* hookname)
{
	struct strbuf hook_key = STRBUF_INIT;
	const char *legacy_hook_path = NULL;

	if (!hookname)
		return NULL;

	/* hook_head is stateful */
	clear_hook_list();

	strbuf_addf(&hook_key, "hook.%s.command", hookname->buf);

	git_config(hook_config_lookup, (void*)hook_key.buf);

	legacy_hook_path = find_hook(hookname->buf);

	/* TODO: check hook.runHookDir */
	if (legacy_hook_path)
		emplace_hook(&hook_head, legacy_hook_path, 1);

	return &hook_head;
}

int hook_exists(const char *hookname)
{
	const char *value = NULL;
	struct strbuf hook_key = STRBUF_INIT;
	strbuf_addf(&hook_key, "hook.%s.command", hookname);

	return (!git_config_get_value(hook_key.buf, &value)) || !!find_hook(hookname);
}

int run_hooks(const char *const *env, const struct strbuf *hookname,
	      const struct strvec *args)
{
	struct list_head *to_run, *pos = NULL, *tmp = NULL;
	int rc = 0;

	to_run = hook_list(hookname);

	list_for_each_safe(pos, tmp, to_run) {
		struct child_process hook_proc = CHILD_PROCESS_INIT;
		struct hook *hook = list_entry(pos, struct hook, list);

		/* add command */
		strvec_push(&hook_proc.args, hook->command.buf);

		/*
		 * add passed-in argv, without expanding - let the user get back
		 * exactly what they put in
		 */
		if (args)
			strvec_pushv(&hook_proc.args, args->v);

		hook_proc.env = env;
		hook_proc.no_stdin = 1;
		hook_proc.stdout_to_stderr = 1;
		hook_proc.trace2_hook_name = hook->command.buf;
		hook_proc.use_shell = 1;

		rc |= run_command(&hook_proc);
	}

	return rc;
}
