#include "cache.h"
#include "config.h"
#include "repository.h"
#include "fsmonitor-settings.h"

/*
 * We keep this structure defintion private and have getters
 * for all fields so that we can lazy load it as needed.
 */
struct fsmonitor_settings {
	enum fsmonitor_mode mode;
	enum fsmonitor_reason reason;
	char *hook_path;
};

static enum fsmonitor_reason check_for_incompatible(struct repository *r)
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

		reason = fsm_os__incompatible(r);
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
		const_str = getenv("GIT_TEST_FSMONITOR");
		break;

	case -1: /* config value set to an arbitrary string */
		if (repo_config_get_pathname(r, "core.fsmonitor", &const_str))
			return; /* should not happen */
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
	if (!r)
		r = the_repository;
	if (!r->settings.fsmonitor)
		lookup_fsmonitor_settings(r);

	return r->settings.fsmonitor->mode;
}

const char *fsm_settings__get_hook_path(struct repository *r)
{
	if (!r)
		r = the_repository;
	if (!r->settings.fsmonitor)
		lookup_fsmonitor_settings(r);

	return r->settings.fsmonitor->hook_path;
}

void fsm_settings__set_ipc(struct repository *r)
{
	enum fsmonitor_reason reason = check_for_incompatible(r);

	if (reason != FSMONITOR_REASON_OK) {
		fsm_settings__set_incompatible(r, reason);
		return;
	}

	/*
	 * Caller requested IPC explicitly, so avoid (possibly
	 * recursive) config lookup.
	 */
	if (!r)
		r = the_repository;
	if (!r->settings.fsmonitor)
		r->settings.fsmonitor = alloc_settings();

	r->settings.fsmonitor->mode = FSMONITOR_MODE_IPC;
	r->settings.fsmonitor->reason = reason;
	FREE_AND_NULL(r->settings.fsmonitor->hook_path);
}

void fsm_settings__set_hook(struct repository *r, const char *path)
{
	enum fsmonitor_reason reason = check_for_incompatible(r);

	if (reason != FSMONITOR_REASON_OK) {
		fsm_settings__set_incompatible(r, reason);
		return;
	}

	/*
	 * Caller requested hook explicitly, so avoid (possibly
	 * recursive) config lookup.
	 */
	if (!r)
		r = the_repository;
	if (!r->settings.fsmonitor)
		r->settings.fsmonitor = alloc_settings();

	r->settings.fsmonitor->mode = FSMONITOR_MODE_HOOK;
	r->settings.fsmonitor->reason = reason;
	FREE_AND_NULL(r->settings.fsmonitor->hook_path);
	r->settings.fsmonitor->hook_path = strdup(path);
}

void fsm_settings__set_disabled(struct repository *r)
{
	if (!r)
		r = the_repository;
	if (!r->settings.fsmonitor)
		r->settings.fsmonitor = alloc_settings();

	r->settings.fsmonitor->mode = FSMONITOR_MODE_DISABLED;
	r->settings.fsmonitor->reason = FSMONITOR_REASON_OK;
	FREE_AND_NULL(r->settings.fsmonitor->hook_path);
}

void fsm_settings__set_incompatible(struct repository *r,
				    enum fsmonitor_reason reason)
{
	if (!r)
		r = the_repository;
	if (!r->settings.fsmonitor)
		r->settings.fsmonitor = alloc_settings();

	r->settings.fsmonitor->mode = FSMONITOR_MODE_INCOMPATIBLE;
	r->settings.fsmonitor->reason = reason;
	FREE_AND_NULL(r->settings.fsmonitor->hook_path);
}

enum fsmonitor_reason fsm_settings__get_reason(struct repository *r)
{
	if (!r)
		r = the_repository;
	if (!r->settings.fsmonitor)
		lookup_fsmonitor_settings(r);

	return r->settings.fsmonitor->reason;
}

char *fsm_settings__get_incompatible_msg(const struct repository *r,
					 enum fsmonitor_reason reason)
{
	struct strbuf msg = STRBUF_INIT;

	switch (reason) {
	case FSMONITOR_REASON_UNTESTED:
	case FSMONITOR_REASON_OK:
		goto done;

	case FSMONITOR_REASON_BARE:
		strbuf_addf(&msg,
			    _("bare repository '%s' is incompatible with fsmonitor"),
			    xgetcwd());
		goto done;

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
		strbuf_addf(&msg,
			    _("repository '%s' is incompatible with fsmonitor due to lack of Unix sockets"),
			    r->worktree);
		goto done;
	}

	BUG("Unhandled case in fsm_settings__get_incompatible_msg: '%d'",
	    reason);

done:
	return strbuf_detach(&msg, NULL);
}
