#include "git-compat-util.h"
#include "abspath.h"
#include "advice.h"
#include "gettext.h"
#include "hook.h"
#include "path.h"
#include "parse.h"
#include "run-command.h"
#include "config.h"
#include "strbuf.h"
#include "strmap.h"
#include "environment.h"
#include "setup.h"

const char *find_hook(struct repository *r, const char *name)
{
	static struct strbuf path = STRBUF_INIT;

	int found_hook;

	if (!r || !r->gitdir)
		return NULL;

	repo_git_path_replace(r, &path, "hooks/%s", name);
	found_hook = access(path.buf, X_OK) >= 0;
#ifdef STRIP_EXTENSION
	if (!found_hook) {
		int err = errno;

		strbuf_addstr(&path, STRIP_EXTENSION);
		found_hook = access(path.buf, X_OK) >= 0;
		if (!found_hook)
			errno = err;
	}
#endif

	if (!found_hook) {
		if (errno == EACCES && advice_enabled(ADVICE_IGNORED_HOOK)) {
			static struct string_list advise_given = STRING_LIST_INIT_DUP;

			if (!string_list_lookup(&advise_given, name)) {
				string_list_insert(&advise_given, name);
				advise(_("The '%s' hook was ignored because "
					 "it's not set as executable.\n"
					 "You can disable this warning with "
					 "`git config set advice.ignoredHook false`."),
				       path.buf);
			}
		}
		return NULL;
	}
	return path.buf;
}

static void hook_clear(struct hook *h, cb_data_free_fn cb_data_free)
{
	if (!h)
		return;

	if (h->kind == HOOK_TRADITIONAL)
		free((void *)h->u.traditional.path);
	else if (h->kind == HOOK_CONFIGURED) {
		free((void *)h->u.configured.friendly_name);
		free((void *)h->u.configured.command);
	}

	if (cb_data_free)
		cb_data_free(h->feed_pipe_cb_data);

	free(h);
}

void hook_list_clear(struct string_list *hooks, cb_data_free_fn cb_data_free)
{
	struct string_list_item *item;

	for_each_string_list_item(item, hooks)
		hook_clear(item->util, cb_data_free);

	string_list_clear(hooks, 0);
}

/* Helper to detect and add default "traditional" hooks from the hookdir. */
static void list_hooks_add_default(struct repository *r, const char *hookname,
				   struct string_list *hook_list,
				   struct run_hooks_opt *options)
{
	const char *hook_path = find_hook(r, hookname);
	struct hook *h;

	if (!hook_path)
		return;

	h = xcalloc(1, sizeof(struct hook));

	/*
	 * If the hook is to run in a specific dir, a relative path can
	 * become invalid in that dir, so convert to an absolute path.
	 */
	if (options && options->dir)
		hook_path = absolute_path(hook_path);

	/* Setup per-hook internal state cb data */
	if (options && options->feed_pipe_cb_data_alloc)
		h->feed_pipe_cb_data = options->feed_pipe_cb_data_alloc(options->feed_pipe_ctx);

	h->kind = HOOK_TRADITIONAL;
	h->u.traditional.path = xstrdup(hook_path);

	string_list_append(hook_list, hook_path)->util = h;
}

static void unsorted_string_list_remove(struct string_list *list,
					const char *str)
{
	struct string_list_item *item = unsorted_string_list_lookup(list, str);
	if (item)
		unsorted_string_list_delete_item(list, item - list->items, 0);
}

/*
 * Callback struct to collect all hook.* keys in a single config pass.
 * commands: friendly-name to command map.
 * event_hooks: event-name to list of friendly-names map.
 * disabled_hooks: set of friendly-names with hook.name.enabled = false.
 */
struct hook_all_config_cb {
	struct strmap commands;
	struct strmap event_hooks;
	struct string_list disabled_hooks;
};

/* repo_config() callback that collects all hook.* configuration in one pass. */
static int hook_config_lookup_all(const char *key, const char *value,
				  const struct config_context *ctx UNUSED,
				  void *cb_data)
{
	struct hook_all_config_cb *data = cb_data;
	const char *name, *subkey;
	char *hook_name;
	size_t name_len = 0;

	if (parse_config_key(key, "hook", &name, &name_len, &subkey))
		return 0;

	if (!value)
		return config_error_nonbool(key);

	/* Extract name, ensuring it is null-terminated. */
	hook_name = xmemdupz(name, name_len);

	if (!strcmp(subkey, "event")) {
		if (!*value) {
			/* Empty values reset previous events for this hook. */
			struct hashmap_iter iter;
			struct strmap_entry *e;

			strmap_for_each_entry(&data->event_hooks, &iter, e)
				unsorted_string_list_remove(e->value, hook_name);
		} else {
			struct string_list *hooks =
				strmap_get(&data->event_hooks, value);

			if (!hooks) {
				hooks = xcalloc(1, sizeof(*hooks));
				string_list_init_dup(hooks);
				strmap_put(&data->event_hooks, value, hooks);
			}

			/* Re-insert if necessary to preserve last-seen order. */
			unsorted_string_list_remove(hooks, hook_name);
			string_list_append(hooks, hook_name);
		}
	} else if (!strcmp(subkey, "command")) {
		/* Store command overwriting the old value */
		char *old = strmap_put(&data->commands, hook_name,
				       xstrdup(value));
		free(old);
	} else if (!strcmp(subkey, "enabled")) {
		switch (git_parse_maybe_bool(value)) {
		case 0: /* disabled */
			if (!unsorted_string_list_lookup(&data->disabled_hooks,
							 hook_name))
				string_list_append(&data->disabled_hooks,
						   hook_name);
			break;
		case 1: /* enabled: undo a prior disabled entry */
			unsorted_string_list_remove(&data->disabled_hooks,
						    hook_name);
			break;
		default:
			break; /* ignore unrecognised values */
		}
	}

	free(hook_name);
	return 0;
}

/*
 * The hook config cache maps each hook event name to a string_list where
 * every item's string is the hook's friendly-name and its util pointer is
 * the corresponding command string. Both strings are owned by the map.
 *
 * Disabled hooks and hooks missing a command are already filtered out at
 * parse time, so callers can iterate the list directly.
 */
void hook_cache_clear(struct strmap *cache)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;

	strmap_for_each_entry(cache, &iter, e) {
		struct string_list *hooks = e->value;
		string_list_clear(hooks, 1); /* free util (command) pointers */
		free(hooks);
	}
	strmap_clear(cache, 0);
}

/* Populate `cache` with the complete hook configuration */
static void build_hook_config_map(struct repository *r, struct strmap *cache)
{
	struct hook_all_config_cb cb_data;
	struct hashmap_iter iter;
	struct strmap_entry *e;

	strmap_init(&cb_data.commands);
	strmap_init(&cb_data.event_hooks);
	string_list_init_dup(&cb_data.disabled_hooks);

	/* Parse all configs in one run. */
	repo_config(r, hook_config_lookup_all, &cb_data);

	/* Construct the cache from parsed configs. */
	strmap_for_each_entry(&cb_data.event_hooks, &iter, e) {
		struct string_list *hook_names = e->value;
		struct string_list *hooks = xcalloc(1, sizeof(*hooks));

		string_list_init_dup(hooks);

		for (size_t i = 0; i < hook_names->nr; i++) {
			const char *hname = hook_names->items[i].string;
			char *command;

			/* filter out disabled hooks */
			if (unsorted_string_list_lookup(&cb_data.disabled_hooks,
							hname))
				continue;

			command = strmap_get(&cb_data.commands, hname);
			if (!command)
				die(_("'hook.%s.command' must be configured or "
				      "'hook.%s.event' must be removed;"
				      " aborting."), hname, hname);

			/* util stores the command; owned by the cache. */
			string_list_append(hooks, hname)->util =
				xstrdup(command);
		}

		strmap_put(cache, e->key, hooks);
	}

	strmap_clear(&cb_data.commands, 1);
	string_list_clear(&cb_data.disabled_hooks, 0);
	strmap_for_each_entry(&cb_data.event_hooks, &iter, e) {
		string_list_clear(e->value, 0);
		free(e->value);
	}
	strmap_clear(&cb_data.event_hooks, 0);
}

/*
 * Return the hook config map for `r`, populating it first if needed.
 *
 * Out-of-repo calls (r->gitdir == NULL) allocate and return a temporary
 * cache map; the caller is responsible for freeing it with
 * hook_cache_clear() + free().
 */
static struct strmap *get_hook_config_cache(struct repository *r)
{
	struct strmap *cache = NULL;

	if (r && r->gitdir) {
		/*
		 * For in-repo calls, the map is stored in r->hook_config_cache,
		 * so repeated invocations don't parse the configs, so allocate
		 * it just once on the first call.
		 */
		if (!r->hook_config_cache) {
			r->hook_config_cache = xcalloc(1, sizeof(*cache));
			strmap_init(r->hook_config_cache);
			build_hook_config_map(r, r->hook_config_cache);
		}
		cache = r->hook_config_cache;
	} else {
		/*
		 * Out-of-repo calls (no gitdir) allocate and return a temporary
		 * map cache which gets free'd immediately by the caller.
		 */
		cache = xcalloc(1, sizeof(*cache));
		strmap_init(cache);
		build_hook_config_map(r, cache);
	}

	return cache;
}

static void list_hooks_add_configured(struct repository *r,
				      const char *hookname,
				      struct string_list *list,
				      struct run_hooks_opt *options)
{
	struct strmap *cache = get_hook_config_cache(r);
	struct string_list *configured_hooks = strmap_get(cache, hookname);

	/* Iterate through configured hooks and initialize internal states */
	for (size_t i = 0; configured_hooks && i < configured_hooks->nr; i++) {
		const char *friendly_name = configured_hooks->items[i].string;
		const char *command = configured_hooks->items[i].util;
		struct hook *hook = xcalloc(1, sizeof(struct hook));

		if (options && options->feed_pipe_cb_data_alloc)
			hook->feed_pipe_cb_data =
				options->feed_pipe_cb_data_alloc(
					options->feed_pipe_ctx);

		hook->kind = HOOK_CONFIGURED;
		hook->u.configured.friendly_name = xstrdup(friendly_name);
		hook->u.configured.command = xstrdup(command);

		string_list_append(list, friendly_name)->util = hook;
	}

	/*
	 * Cleanup temporary cache for out-of-repo calls since they can't be
	 * stored persistently. Next out-of-repo calls will have to re-parse.
	 */
	if (!r || !r->gitdir) {
		hook_cache_clear(cache);
		free(cache);
	}
}

struct string_list *list_hooks(struct repository *r, const char *hookname,
			       struct run_hooks_opt *options)
{
	struct string_list *hook_head;

	if (!hookname)
		BUG("null hookname was provided to hook_list()!");

	hook_head = xmalloc(sizeof(struct string_list));
	string_list_init_dup(hook_head);

	/* Add hooks from the config, e.g. hook.myhook.event = pre-commit */
	list_hooks_add_configured(r, hookname, hook_head, options);

	/* Add the default "traditional" hooks from hookdir. */
	list_hooks_add_default(r, hookname, hook_head, options);

	return hook_head;
}

int hook_exists(struct repository *r, const char *name)
{
	struct string_list *hooks = list_hooks(r, name, NULL);
	int exists = hooks->nr > 0;
	hook_list_clear(hooks, NULL);
	free(hooks);
	return exists;
}

static int pick_next_hook(struct child_process *cp,
			  struct strbuf *out UNUSED,
			  void *pp_cb,
			  void **pp_task_cb)
{
	struct hook_cb_data *hook_cb = pp_cb;
	struct string_list *hook_list = hook_cb->hook_command_list;
	struct hook *h;

	if (hook_cb->hook_to_run_index >= hook_list->nr)
		return 0;

	h = hook_list->items[hook_cb->hook_to_run_index++].util;

	cp->no_stdin = 1;
	strvec_pushv(&cp->env, hook_cb->options->env.v);

	if (hook_cb->options->path_to_stdin && hook_cb->options->feed_pipe)
		BUG("options path_to_stdin and feed_pipe are mutually exclusive");

	/* reopen the file for stdin; run_command closes it. */
	if (hook_cb->options->path_to_stdin) {
		cp->no_stdin = 0;
		cp->in = xopen(hook_cb->options->path_to_stdin, O_RDONLY);
	}

	if (hook_cb->options->feed_pipe) {
		cp->no_stdin = 0;
		/* start_command() will allocate a pipe / stdin fd for us */
		cp->in = -1;
	}

	cp->stdout_to_stderr = hook_cb->options->stdout_to_stderr;
	cp->trace2_hook_name = hook_cb->hook_name;
	cp->dir = hook_cb->options->dir;

	/* Add hook exec paths or commands */
	if (h->kind == HOOK_TRADITIONAL) {
		strvec_push(&cp->args, h->u.traditional.path);
	} else if (h->kind == HOOK_CONFIGURED) {
		/* to enable oneliners, let config-specified hooks run in shell. */
		cp->use_shell = true;
		strvec_push(&cp->args, h->u.configured.command);
	}

	if (!cp->args.nr)
		BUG("hook must have at least one command or exec path");

	strvec_pushv(&cp->args, hook_cb->options->args.v);

	/*
	 * Provide per-hook internal state via task_cb for easy access, so
	 * hook callbacks don't have to go through hook_cb->options.
	 */
	*pp_task_cb = h->feed_pipe_cb_data;

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

int run_hooks_opt(struct repository *r, const char *hook_name,
		  struct run_hooks_opt *options)
{
	struct hook_cb_data cb_data = {
		.rc = 0,
		.hook_name = hook_name,
		.options = options,
	};
	int ret = 0;
	const struct run_process_parallel_opts opts = {
		.tr2_category = "hook",
		.tr2_label = hook_name,

		.processes = options->jobs,
		.ungroup = options->jobs == 1,

		.get_next_task = pick_next_hook,
		.start_failure = notify_start_failure,
		.feed_pipe = options->feed_pipe,
		.task_finished = notify_hook_finished,

		.data = &cb_data,
	};

	if (!options)
		BUG("a struct run_hooks_opt must be provided to run_hooks");

	if (options->path_to_stdin && options->feed_pipe)
		BUG("options path_to_stdin and feed_pipe are mutually exclusive");

	if (!options->jobs)
		BUG("run_hooks_opt must be called with options.jobs >= 1");

	/*
	 * Ensure cb_data copy and free functions are either provided together,
	 * or neither one is provided.
	 */
	if ((options->feed_pipe_cb_data_alloc && !options->feed_pipe_cb_data_free) ||
	    (!options->feed_pipe_cb_data_alloc && options->feed_pipe_cb_data_free))
		BUG("feed_pipe_cb_data_alloc and feed_pipe_cb_data_free must be set together");

	if (options->invoked_hook)
		*options->invoked_hook = 0;

	cb_data.hook_command_list = list_hooks(r, hook_name, options);
	if (!cb_data.hook_command_list->nr) {
		if (options->error_if_missing)
			ret = error("cannot find a hook named %s", hook_name);
		goto cleanup;
	}

	run_processes_parallel(&opts);
	ret = cb_data.rc;
cleanup:
	hook_list_clear(cb_data.hook_command_list, options->feed_pipe_cb_data_free);
	free(cb_data.hook_command_list);
	run_hooks_opt_clear(options);
	return ret;
}

int run_hooks(struct repository *r, const char *hook_name)
{
	struct run_hooks_opt opt = RUN_HOOKS_OPT_INIT;

	return run_hooks_opt(r, hook_name, &opt);
}

int run_hooks_l(struct repository *r, const char *hook_name, ...)
{
	struct run_hooks_opt opt = RUN_HOOKS_OPT_INIT;
	va_list ap;
	const char *arg;

	va_start(ap, hook_name);
	while ((arg = va_arg(ap, const char *)))
		strvec_push(&opt.args, arg);
	va_end(ap);

	return run_hooks_opt(r, hook_name, &opt);
}
