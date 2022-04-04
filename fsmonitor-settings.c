#include "git-compat-util.h"
#include "config.h"
#include "gettext.h"
#include "repository.h"
#include "fsmonitor-ipc.h"
#include "fsmonitor-settings.h"
#include "fsmonitor-path-utils.h"
#include "advice.h"

/*
 * We keep this structure defintion private and have getters
 * for all fields so that we can lazy load it as needed.
 */
struct fsmonitor_settings {
	enum fsmonitor_mode mode;
	enum fsmonitor_reason reason;
	char *hook_path;
};

/*
 * Remote working directories are problematic for FSMonitor.
 *
 * The underlying file system on the server machine and/or the remote
 * mount type dictates whether notification events are available at
 * all to remote client machines.
 *
 * Kernel differences between the server and client machines also
 * dictate the how (buffering, frequency, de-dup) the events are
 * delivered to client machine processes.
 *
 * A client machine (such as a laptop) may choose to suspend/resume
 * and it is unclear (without lots of testing) whether the watcher can
 * resync after a resume.  We might be able to treat this as a normal
 * "events were dropped by the kernel" event and do our normal "flush
 * and resync" --or-- we might need to close the existing (zombie?)
 * notification fd and create a new one.
 *
 * In theory, the above issues need to be addressed whether we are
 * using the Hook or IPC API.
 *
 * So (for now at least), mark remote working directories as
 * incompatible unless 'fsmonitor.allowRemote' is true.
 *
 */
#ifdef HAVE_FSMONITOR_OS_SETTINGS
static enum fsmonitor_reason check_remote(struct repository *r)
{
	int allow_remote = -1; /* -1 unset, 0 not allowed, 1 allowed */
	int is_remote = fsmonitor__is_fs_remote(r->worktree);

	switch (is_remote) {
		case 0:
			return FSMONITOR_REASON_OK;
		case 1:
			repo_config_get_bool(r, "fsmonitor.allowremote", &allow_remote);
			if (allow_remote < 1)
				return FSMONITOR_REASON_REMOTE;
			else
				return FSMONITOR_REASON_OK;
		default:
			return FSMONITOR_REASON_ERROR;
	}
}
#endif

static enum fsmonitor_reason check_for_incompatible(struct repository *r,
						    int ipc MAYBE_UNUSED)
{
	if (!r->worktree) {
		/*
		 * Bare repositories don't have a working directory and
		 * therefore have nothing to watch.
		 */
		return FSMONITOR_REASON_BARE;
	}

#ifdef HAVE_FSMONITOR_OS_SETTINGS
	{
		enum fsmonitor_reason reason;

		reason = check_remote(r);
		if (reason != FSMONITOR_REASON_OK)
			return reason;
		reason = fsm_os__incompatible(r, ipc);
		if (reason != FSMONITOR_REASON_OK)
			return reason;
	}
#endif

	return FSMONITOR_REASON_OK;
}

static struct fsmonitor_settings *alloc_settings(void)
{
	struct fsmonitor_settings *s;

	CALLOC_ARRAY(s, 1);
	s->mode = FSMONITOR_MODE_DISABLED;
	s->reason = FSMONITOR_REASON_UNTESTED;

	return s;
}

static int check_deprecated_builtin_config(struct repository *r)
{
	int core_use_builtin_fsmonitor = 0;

	/*
	 * If 'core.useBuiltinFSMonitor' is set, print a deprecation warning
	 * suggesting the use of 'core.fsmonitor' instead. If the config is
	 * set to true, set the appropriate mode and return 1 indicating that
	 * the check resulted the config being set by this (deprecated) setting.
	 */
	if(!repo_config_get_bool(r, "core.useBuiltinFSMonitor", &core_use_builtin_fsmonitor) &&
	   core_use_builtin_fsmonitor) {
		if (!git_env_bool("GIT_SUPPRESS_USEBUILTINFSMONITOR_ADVICE", 0)) {
			advise_if_enabled(ADVICE_USE_CORE_FSMONITOR_CONFIG,
					  _("core.useBuiltinFSMonitor=true is deprecated;"
					    "please set core.fsmonitor=true instead"));
			setenv("GIT_SUPPRESS_USEBUILTINFSMONITOR_ADVICE", "1", 1);
		}
		fsm_settings__set_ipc(r);
		return 1;
	}

	return 0;
}

static void lookup_fsmonitor_settings(struct repository *r)
{
	const char *const_str;
	int bool_value;

	if (r->settings.fsmonitor)
		return;

	/*
	 * Overload the existing "core.fsmonitor" config setting (which
	 * has historically been either unset or a hook pathname) to
	 * now allow a boolean value to enable the builtin FSMonitor
	 * or to turn everything off.  (This does imply that you can't
	 * use a hook script named "true" or "false", but that's OK.)
	 */
	switch (repo_config_get_maybe_bool(r, "core.fsmonitor", &bool_value)) {

	case 0: /* config value was set to <bool> */
		if (bool_value)
			fsm_settings__set_ipc(r);
		else
			fsm_settings__set_disabled(r);
		return;

	case 1: /* config value was unset */
		if (check_deprecated_builtin_config(r))
			return;

		const_str = getenv("GIT_TEST_FSMONITOR");
		break;

	case -1: /* config value set to an arbitrary string */
		if (check_deprecated_builtin_config(r) ||
		    repo_config_get_pathname(r, "core.fsmonitor", &const_str))
			return;
		break;

	default: /* should not happen */
		return;
	}

	if (const_str && *const_str)
		fsm_settings__set_hook(r, const_str);
	else
		fsm_settings__set_disabled(r);
}

enum fsmonitor_mode fsm_settings__get_mode(struct repository *r)
{
	if (!r->settings.fsmonitor)
		lookup_fsmonitor_settings(r);

	return r->settings.fsmonitor->mode;
}

const char *fsm_settings__get_hook_path(struct repository *r)
{
	if (!r->settings.fsmonitor)
		lookup_fsmonitor_settings(r);

	return r->settings.fsmonitor->hook_path;
}

void fsm_settings__set_ipc(struct repository *r)
{
	enum fsmonitor_reason reason = check_for_incompatible(r, 1);

	if (reason != FSMONITOR_REASON_OK) {
		fsm_settings__set_incompatible(r, reason);
		return;
	}

	/*
	 * Caller requested IPC explicitly, so avoid (possibly
	 * recursive) config lookup.
	 */
	if (!r->settings.fsmonitor)
		r->settings.fsmonitor = alloc_settings();

	r->settings.fsmonitor->mode = FSMONITOR_MODE_IPC;
	r->settings.fsmonitor->reason = reason;
	FREE_AND_NULL(r->settings.fsmonitor->hook_path);
}

void fsm_settings__set_hook(struct repository *r, const char *path)
{
	enum fsmonitor_reason reason = check_for_incompatible(r, 0);

	if (reason != FSMONITOR_REASON_OK) {
		fsm_settings__set_incompatible(r, reason);
		return;
	}

	/*
	 * Caller requested hook explicitly, so avoid (possibly
	 * recursive) config lookup.
	 */
	if (!r->settings.fsmonitor)
		r->settings.fsmonitor = alloc_settings();

	r->settings.fsmonitor->mode = FSMONITOR_MODE_HOOK;
	r->settings.fsmonitor->reason = reason;
	FREE_AND_NULL(r->settings.fsmonitor->hook_path);
	r->settings.fsmonitor->hook_path = strdup(path);
}

void fsm_settings__set_disabled(struct repository *r)
{
	if (!r->settings.fsmonitor)
		r->settings.fsmonitor = alloc_settings();

	r->settings.fsmonitor->mode = FSMONITOR_MODE_DISABLED;
	r->settings.fsmonitor->reason = FSMONITOR_REASON_OK;
	FREE_AND_NULL(r->settings.fsmonitor->hook_path);
}

void fsm_settings__set_incompatible(struct repository *r,
				    enum fsmonitor_reason reason)
{
	if (!r->settings.fsmonitor)
		r->settings.fsmonitor = alloc_settings();

	r->settings.fsmonitor->mode = FSMONITOR_MODE_INCOMPATIBLE;
	r->settings.fsmonitor->reason = reason;
	FREE_AND_NULL(r->settings.fsmonitor->hook_path);
}

enum fsmonitor_reason fsm_settings__get_reason(struct repository *r)
{
	if (!r->settings.fsmonitor)
		lookup_fsmonitor_settings(r);

	return r->settings.fsmonitor->reason;
}

char *fsm_settings__get_incompatible_msg(struct repository *r,
					 enum fsmonitor_reason reason)
{
	struct strbuf msg = STRBUF_INIT;
	const char *socket_dir;

	switch (reason) {
	case FSMONITOR_REASON_UNTESTED:
	case FSMONITOR_REASON_OK:
		goto done;

	case FSMONITOR_REASON_BARE: {
		char *cwd = xgetcwd();

		strbuf_addf(&msg,
			    _("bare repository '%s' is incompatible with fsmonitor"),
			    cwd);
		free(cwd);
		goto done;
	}

	case FSMONITOR_REASON_ERROR:
		strbuf_addf(&msg,
			    _("repository '%s' is incompatible with fsmonitor due to errors"),
			    r->worktree);
		goto done;

	case FSMONITOR_REASON_REMOTE:
		strbuf_addf(&msg,
			    _("remote repository '%s' is incompatible with fsmonitor"),
			    r->worktree);
		goto done;

	case FSMONITOR_REASON_VFS4GIT:
		strbuf_addf(&msg,
			    _("virtual repository '%s' is incompatible with fsmonitor"),
			    r->worktree);
		goto done;

	case FSMONITOR_REASON_NOSOCKETS:
		socket_dir = dirname((char *)fsmonitor_ipc__get_path(r));
		strbuf_addf(&msg,
			    _("socket directory '%s' is incompatible with fsmonitor due"
			      " to lack of Unix sockets support"),
			    socket_dir);
		goto done;
	}

	BUG("Unhandled case in fsm_settings__get_incompatible_msg: '%d'",
	    reason);

done:
	return strbuf_detach(&msg, NULL);
}
