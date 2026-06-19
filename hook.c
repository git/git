#include "git-compat-util.h"
#include "abspath.h"
#include "advice.h"
#include "config.h"
#include "environment.h"
#include "gettext.h"
#include "hook.h"
#include "hook-list.h"
#include "parse.h"
#include "path.h"
#include "run-command.h"
#include "setup.h"
#include "strbuf.h"
#include "strmap.h"
#include "thread-utils.h"

bool is_known_hook(const char *name)
{
	const char **h;
	for (h = hook_name_list; *h; h++)
		if (!strcmp(*h, name))
			return true;
	return false;
}

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

void hook_free(void *p, const char *str UNUSED)
{
	struct hook *h = p;

	if (!h)
		return;

	if (h->kind == HOOK_TRADITIONAL) {
		free((void *)h->u.traditional.path);
	} else if (h->kind == HOOK_CONFIGURED) {
		free((void *)h->u.configured.friendly_name);
		free((void *)h->u.configured.command);
	}

	if (h->data_free && h->feed_pipe_cb_data)
		h->data_free(h->feed_pipe_cb_data);

	free(h);
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

	CALLOC_ARRAY(h, 1);

	/*
	 * If the hook is to run in a specific dir, a relative path can
	 * become invalid in that dir, so convert to an absolute path.
	 */
	if (options && options->dir)
		hook_path = absolute_path(hook_path);

	/*
	 * Setup per-hook internal state callback data.
	 * When provided, the alloc/free callbacks are always provided
	 * together, so use them to alloc/free the internal hook state.
	 */
	if (options && options->feed_pipe_cb_data_alloc) {
		h->feed_pipe_cb_data = options->feed_pipe_cb_data_alloc(options->feed_pipe_ctx);
		h->data_free = options->feed_pipe_cb_data_free;
	}

	h->kind = HOOK_TRADITIONAL;
	h->u.traditional.path = xstrdup(hook_path);

	string_list_append(hook_list, hook_path)->util = h;
}

/*
 * Cache entry stored as the .util pointer of string_list items inside the
 * hook config cache.
 */
struct hook_config_cache_entry {
	char *command;
	enum config_scope scope;
	bool disabled;
	bool parallel;
};

/*
 * Callback struct to collect all hook.* keys in a single config pass.
 * commands: friendly-name to command map.
 * event_hooks: event-name to list of friendly-names map.
 * disabled_hooks: set of all names with hook.<name>.enabled = false; after
 *                 parsing, names that are not friendly-names become event-level
 *                 disables stored in r->disabled_events. This collects all.
 * parallel_hooks: friendly-name to parallel flag.
 * event_jobs: event-name to per-event jobs count (stored as uintptr_t, NULL == unset).
 * jobs: value of the global hook.jobs key. Defaults to 0 if unset (stored in r->hook_jobs).
 */
struct hook_all_config_cb {
	struct strmap commands;
	struct strmap event_hooks;
	struct string_list disabled_hooks;
	struct strmap parallel_hooks;
	struct strmap event_jobs;
	unsigned int jobs;
};

/* repo_config() callback that collects all hook.* configuration in one pass. */
static int hook_config_lookup_all(const char *key, const char *value,
				  const struct config_context *ctx,
				  void *cb_data)
{
	struct hook_all_config_cb *data = cb_data;
	const char *name, *subkey;
	char *hook_name;
	size_t name_len = 0;

	if (parse_config_key(key, "hook", &name, &name_len, &subkey))
		return 0;

	/* Handle plain hook.<key> entries that have no hook name component. */
	if (!name) {
		if (!strcmp(subkey, "jobs") && value) {
			int v;
			if (!git_parse_int(value, &v))
				warning(_("hook.jobs must be an integer, ignoring: '%s'"), value);
			else if (v == -1)
				data->jobs = online_cpus();
			else if (v > 0)
				data->jobs = v;
			else
				warning(_("hook.jobs must be a positive integer"
					  " or -1, ignoring: '%s'"),
					value);
		}
		return 0;
	}

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
				unsorted_string_list_remove(e->value, hook_name, 0);
		} else {
			struct string_list *hooks;

			if (is_known_hook(hook_name))
				die(_("hook friendly-name '%s' collides with "
				      "a known event name; please choose a "
				      "different friendly-name"),
				    hook_name);

			if (!strcmp(hook_name, value))
				warning(_("hook friendly-name '%s' is the "
					  "same as its event; this may cause "
					  "ambiguity with hook.%s.enabled"),
					hook_name, hook_name);

			hooks = strmap_get(&data->event_hooks, value);

			if (!hooks) {
				CALLOC_ARRAY(hooks, 1);
				string_list_init_dup(hooks);
				strmap_put(&data->event_hooks, value, hooks);
			}

			/* Re-insert if necessary to preserve last-seen order. */
			unsorted_string_list_remove(hooks, hook_name, 0);

			if (!ctx->kvi)
				BUG("hook config callback called without key-value info");

			/*
			 * Stash the config scope in the util pointer for
			 * later retrieval in build_hook_config_map(). This
			 * intermediate struct is transient and never leaves
			 * that function, so we pack the enum value into the
			 * pointer rather than heap-allocating a wrapper.
			 */
			string_list_append(hooks, hook_name)->util =
				(void *)(uintptr_t)ctx->kvi->scope;
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
						    hook_name, 0);
			break;
		default:
			break; /* ignore unrecognised values */
		}
	} else if (!strcmp(subkey, "parallel")) {
		int v = git_parse_maybe_bool(value);
		if (v >= 0)
			strmap_put(&data->parallel_hooks, hook_name,
				   (void *)(uintptr_t)v);
		else
			warning(_("hook.%s.parallel must be a boolean,"
				  " ignoring: '%s'"),
				hook_name, value);
	} else if (!strcmp(subkey, "jobs")) {
		int v;
		if (!git_parse_int(value, &v))
			warning(_("hook.%s.jobs must be an integer,"
				  " ignoring: '%s'"),
				hook_name, value);
		else if (v == -1)
			strmap_put(&data->event_jobs, hook_name,
				   (void *)(uintptr_t)online_cpus());
		else if (v > 0)
			strmap_put(&data->event_jobs, hook_name,
				   (void *)(uintptr_t)v);
		else
			warning(_("hook.%s.jobs must be a positive"
				  " integer or -1, ignoring: '%s'"),
				hook_name, value);
	}

	free(hook_name);
	return 0;
}

/*
 * The hook config cache maps each hook event name to a string_list where
 * every item's string is the hook's friendly-name and its util pointer is
 * the corresponding command string. Both strings are owned by the map.
 *
 * Disabled hooks are kept in the cache with entry->disabled set, so that
 * "git hook list" can display them. A non-disabled hook missing a command
 * is fatal; a disabled hook missing a command emits a warning and is kept
 * in the cache with entry->command = NULL.
 */
void hook_cache_clear(struct strmap *cache)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;

	strmap_for_each_entry(cache, &iter, e) {
		struct string_list *hooks = e->value;
		for (size_t i = 0; i < hooks->nr; i++) {
			struct hook_config_cache_entry *entry = hooks->items[i].util;
			free(entry->command);
			free(entry);
		}
		string_list_clear(hooks, 0);
		free(hooks);
	}
	strmap_clear(cache, 0);
}

/*
 * Return true if `name` is a hook friendly-name, i.e. it has at least one of
 * .command, .event, or .parallel configured. These are the reliable clues
 * that distinguish a friendly-name from an event name. Note: .enabled is
 * deliberately excluded because it can appear under both namespaces.
 */
static int is_friendly_name(struct hook_all_config_cb *cb, const char *name)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;

	if (strmap_get(&cb->commands, name) || strmap_get(&cb->parallel_hooks, name))
		return 1;

	strmap_for_each_entry(&cb->event_hooks, &iter, e) {
		if (unsorted_string_list_lookup(e->value, name))
			return 1;
	}

	return 0;
}

/* Warn if any name in event_jobs is also a hook friendly-name. */
static void warn_jobs_on_friendly_names(struct hook_all_config_cb *cb_data)
{
	struct hashmap_iter iter;
	struct strmap_entry *e;

	strmap_for_each_entry(&cb_data->event_jobs, &iter, e) {
		if (is_friendly_name(cb_data, e->key))
			warning(_("hook.%s.jobs is set but '%s' looks like a "
				  "hook friendly-name, not an event name; "
				  "hook.<event>.jobs uses the event name "
				  "(e.g. hook.post-receive.jobs), so this "
				  "setting will be ignored"), e->key, e->key);
	}
}

/* Populate `cache` with the complete hook configuration */
static void build_hook_config_map(struct repository *r, struct strmap *cache)
{
	struct hook_all_config_cb cb_data = { 0 };
	struct hashmap_iter iter;
	struct strmap_entry *e;

	strmap_init(&cb_data.commands);
	strmap_init(&cb_data.event_hooks);
	string_list_init_dup(&cb_data.disabled_hooks);
	strmap_init(&cb_data.parallel_hooks);
	strmap_init(&cb_data.event_jobs);

	/* Parse all configs in one run, capturing hook.* including hook.jobs. */
	repo_config(r, hook_config_lookup_all, &cb_data);

	warn_jobs_on_friendly_names(&cb_data);

	/*
	 * Populate disabled_events: names in disabled_hooks that are not
	 * friendly-names are event-level switches (hook.<event>.enabled = false).
	 * Names that are friendly-names are already handled per-hook via the
	 * hook_config_cache_entry.disabled flag below.
	 */
	if (r) {
		string_list_clear(&r->disabled_events, 0);
		string_list_init_dup(&r->disabled_events);
		for (size_t i = 0; i < cb_data.disabled_hooks.nr; i++) {
			const char *n = cb_data.disabled_hooks.items[i].string;
			if (!is_friendly_name(&cb_data, n))
				string_list_append(&r->disabled_events, n);
		}
	}

	/* Construct the cache from parsed configs. */
	strmap_for_each_entry(&cb_data.event_hooks, &iter, e) {
		struct string_list *hook_names = e->value;
		struct string_list *hooks;

		CALLOC_ARRAY(hooks, 1);
		string_list_init_dup(hooks);

		for (size_t i = 0; i < hook_names->nr; i++) {
			const char *hname = hook_names->items[i].string;
			enum config_scope scope =
				(enum config_scope)(uintptr_t)hook_names->items[i].util;
			struct hook_config_cache_entry *entry;
			char *command;

			bool is_par = !!strmap_get(&cb_data.parallel_hooks, hname);
			bool is_disabled =
				!!unsorted_string_list_lookup(
					&cb_data.disabled_hooks, hname);

			command = strmap_get(&cb_data.commands, hname);
			if (!command) {
				if (is_disabled)
					warning(_("disabled hook '%s' has no "
						  "command configured"), hname);
				else
					die(_("'hook.%s.command' must be configured or "
					      "'hook.%s.event' must be removed;"
					      " aborting."), hname, hname);
			}

			/* util stores a cache entry; owned by the cache. */
			CALLOC_ARRAY(entry, 1);
			entry->command = xstrdup_or_null(command);
			entry->scope = scope;
			entry->disabled = is_disabled;
			entry->parallel = is_par;
			string_list_append(hooks, hname)->util = entry;
		}

		strmap_put(cache, e->key, hooks);
	}

	if (r) {
		r->hook_jobs = cb_data.jobs;
		r->event_jobs = cb_data.event_jobs;
	}

	strmap_clear(&cb_data.commands, 1);
	strmap_clear(&cb_data.parallel_hooks, 0); /* values are uintptr_t, not heap ptrs */
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
			CALLOC_ARRAY(r->hook_config_cache, 1);
			strmap_init(r->hook_config_cache);
			build_hook_config_map(r, r->hook_config_cache);
		}
		cache = r->hook_config_cache;
	} else {
		/*
		 * Out-of-repo calls (no gitdir) allocate and return a temporary
		 * cache which gets freed immediately by the caller.
		 */
		CALLOC_ARRAY(cache, 1);
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
	bool event_is_disabled = r ? !!unsorted_string_list_lookup(&r->disabled_events,
								   hookname) : 0;

	/* Iterate through configured hooks and initialize internal states */
	for (size_t i = 0; configured_hooks && i < configured_hooks->nr; i++) {
		const char *friendly_name = configured_hooks->items[i].string;
		struct hook_config_cache_entry *entry = configured_hooks->items[i].util;
		struct hook *hook;

		CALLOC_ARRAY(hook, 1);

		/*
		 * When provided, the alloc/free callbacks are always provided
		 * together, so use them to alloc/free the internal hook state.
		 */
		if (options && options->feed_pipe_cb_data_alloc) {
			hook->feed_pipe_cb_data =
				options->feed_pipe_cb_data_alloc(
					options->feed_pipe_ctx);
			hook->data_free = options->feed_pipe_cb_data_free;
		}

		hook->kind = HOOK_CONFIGURED;
		hook->u.configured.friendly_name = xstrdup(friendly_name);
		hook->u.configured.command =
			entry->command ? xstrdup(entry->command) : NULL;
		hook->u.configured.scope = entry->scope;
		hook->u.configured.disabled = entry->disabled;
		hook->u.configured.event_disabled = event_is_disabled;
		hook->parallel = entry->parallel;

		string_list_append(list, friendly_name)->util = hook;
	}

	/*
	 * Cleanup temporary cache for out-of-repo calls since they can't be
	 * stored persistently. Next out-of-repo calls will have to re-parse.
	 */
	if (!r || !r->gitdir) {
		hook_cache_clear(cache);
		free(cache);
		if (r)
			string_list_clear(&r->disabled_events, 0);
	}
}

struct string_list *list_hooks(struct repository *r, const char *hookname,
			       struct run_hooks_opt *options)
{
	struct string_list *hook_head;

	if (!hookname)
		BUG("null hookname was provided to hook_list()!");

	CALLOC_ARRAY(hook_head, 1);
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
	int exists = 0;

	for (size_t i = 0; i < hooks->nr; i++) {
		struct hook *h = hooks->items[i].util;
		if (h->kind == HOOK_TRADITIONAL ||
		    (!h->u.configured.disabled && !h->u.configured.event_disabled)) {
			exists = 1;
			break;
		}
	}
	string_list_clear_func(hooks, hook_free);
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

	do {
		if (hook_cb->hook_to_run_index >= hook_list->nr)
			return 0;
		h = hook_list->items[hook_cb->hook_to_run_index++].util;
	} while (h->kind == HOOK_CONFIGURED &&
		 (h->u.configured.disabled || h->u.configured.event_disabled));

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
		if (!h->u.configured.command)
			BUG("non-disabled HOOK_CONFIGURED hook has no command");
		strvec_push(&cp->args, h->u.configured.command);
	} else {
		BUG("unknown hook kind");
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

/*
 * When running in parallel, stdout must be merged into stderr so
 * run-command can buffer and de-interleave outputs correctly. This
 * applies even to hooks like pre-push that normally keep stdout and
 * stderr separate: the user has opted into parallelism, so the output
 * stream behavior changes accordingly.
 */
static void merge_output_if_parallel(struct run_hooks_opt *options)
{
	if (options->jobs > 1)
		options->stdout_to_stderr = 1;
}

static void warn_non_parallel_hooks_override(unsigned int jobs,
					     struct string_list *hook_list)
{
	/* Don't warn for hooks running sequentially. */
	if (jobs == 1)
		return;

	for (size_t i = 0; i < hook_list->nr; i++) {
		struct hook *h = hook_list->items[i].util;
		if (h->kind == HOOK_CONFIGURED && !h->parallel)
			warning(_("hook '%s' is not marked as parallel=true, "
				  "running in parallel anyway due to -j%u"),
				h->u.configured.friendly_name, jobs);
	}
}

/* Resolve a hook.jobs config key, handling -1 as online_cpus(). */
static void resolve_hook_config_jobs(struct repository *r,
				     const char *key,
				     unsigned int *jobs)
{
	int v;

	if (repo_config_get_int(r, key, &v))
		return;

	if (v == -1)
		*jobs = online_cpus();
	else if (v > 0)
		*jobs = v;
	else
		warning(_("%s must be a positive integer or -1,"
			  " ignoring: %d"), key, v);
}

/* Determine how many jobs to use for hook execution. */
static unsigned int get_hook_jobs(struct repository *r,
				  struct run_hooks_opt *options,
				  const char *hook_name,
				  struct string_list *hook_list)
{
	/*
	 * An explicit job count overrides everything else: this covers both
	 * FORCE_SERIAL callers (for hooks that must never run in parallel)
	 * and the -j flag from the CLI. The CLI override is intentional: users
	 * may want to serialize hooks declared parallel or to parallelize more
	 * aggressively than the default.
	 */
	if (options->jobs)
		goto cleanup;

	/*
	 * Use hook.jobs from the already-parsed config cache (in-repo), or
	 * fallback to a direct config lookup (out-of-repo).
	 * Default to 1 (serial execution) on failure.
	 */
	options->jobs = 1;
	if (r) {
		if (r->gitdir && r->hook_config_cache) {
			void *event_jobs;

			if (r->hook_jobs)
				options->jobs = r->hook_jobs;

			event_jobs = strmap_get(&r->event_jobs, hook_name);
			if (event_jobs)
				options->jobs = (unsigned int)(uintptr_t)event_jobs;
		} else {
			char *key;

			resolve_hook_config_jobs(r, "hook.jobs", &options->jobs);

			key = xstrfmt("hook.%s.jobs", hook_name);
			resolve_hook_config_jobs(r, key, &options->jobs);
			free(key);
		}
	}

	/*
	 * Cap to serial any configured hook not marked as parallel = true.
	 * This enforces the parallel = false default, even for "traditional"
	 * hooks from the hookdir which cannot be marked parallel = true.
	 * The same restriction applies whether jobs came from hook.jobs or
	 * hook.<event>.jobs.
	 */
	for (size_t i = 0; i < hook_list->nr; i++) {
		struct hook *h = hook_list->items[i].util;
		if (h->kind == HOOK_CONFIGURED && !h->parallel) {
			options->jobs = 1;
			break;
		}
	}

cleanup:
	merge_output_if_parallel(options);
	warn_non_parallel_hooks_override(options->jobs, hook_list);
	return options->jobs;
}

int run_hooks_opt(struct repository *r, const char *hook_name,
		  struct run_hooks_opt *options)
{
	struct string_list *hook_list = list_hooks(r, hook_name, options);
	struct hook_cb_data cb_data = {
		.rc = 0,
		.hook_name = hook_name,
		.hook_command_list = hook_list,
		.options = options,
	};
	int ret = 0;
	unsigned int jobs = get_hook_jobs(r, options, hook_name, hook_list);
	const struct run_process_parallel_opts opts = {
		.tr2_category = "hook",
		.tr2_label = hook_name,

		.processes = jobs,
		.ungroup = jobs == 1,

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

	/*
	 * Ensure cb_data copy and free functions are either provided together,
	 * or neither one is provided.
	 */
	if (!options->feed_pipe_cb_data_alloc != !options->feed_pipe_cb_data_free)
		BUG("feed_pipe_cb_data_alloc and feed_pipe_cb_data_free must be set together");

	if (options->invoked_hook)
		*options->invoked_hook = 0;

	if (!cb_data.hook_command_list->nr) {
		if (options->error_if_missing)
			ret = error("cannot find a hook named %s", hook_name);
		goto cleanup;
	}

	run_processes_parallel(&opts);
	ret = cb_data.rc;
cleanup:
	string_list_clear_func(cb_data.hook_command_list, hook_free);
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
