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

static void set_incompatible(struct repository *r,
			     enum fsmonitor_reason reason)
{
	struct fsmonitor_settings *s = r->settings.fsmonitor;

	s->mode = FSMONITOR_MODE_INCOMPATIBLE;
	s->reason = reason;
}

static int check_for_incompatible(struct repository *r)
{
	if (!r->worktree) {
		/*
		 * Bare repositories don't have a working directory and
		 * therefore have nothing to watch.
		 */
		set_incompatible(r, FSMONITOR_REASON_BARE);
		return 1;
	}

#ifdef HAVE_FSMONITOR_OS_SETTINGS
	{
		enum fsmonitor_reason reason;

		reason = fsm_os__incompatible(r);
		if (reason != FSMONITOR_REASON_ZERO) {
			set_incompatible(r, reason);
			return 1;
		}
	}
#endif

	return 0;
}

static struct fsmonitor_settings *s_init(struct repository *r)
{
	if (!r->settings.fsmonitor)
		CALLOC_ARRAY(r->settings.fsmonitor, 1);

	return r->settings.fsmonitor;
}

void fsm_settings__set_ipc(struct repository *r)
{
	struct fsmonitor_settings *s = s_init(r);

	if (check_for_incompatible(r))
		return;

	s->mode = FSMONITOR_MODE_IPC;
}

void fsm_settings__set_hook(struct repository *r, const char *path)
{
	struct fsmonitor_settings *s = s_init(r);

	if (check_for_incompatible(r))
		return;

	s->mode = FSMONITOR_MODE_HOOK;
	s->hook_path = strdup(path);
}

void fsm_settings__set_disabled(struct repository *r)
{
	struct fsmonitor_settings *s = s_init(r);

	s->mode = FSMONITOR_MODE_DISABLED;
	s->reason = FSMONITOR_REASON_ZERO;
	FREE_AND_NULL(s->hook_path);
}

static int check_for_ipc(struct repository *r)
{
	int value;

	if (!repo_config_get_bool(r, "core.usebuiltinfsmonitor", &value) &&
	    value) {
		fsm_settings__set_ipc(r);
		return 1;
	}

	return 0;
}

static int check_for_hook(struct repository *r)
{
	const char *const_str;

	if (repo_config_get_pathname(r, "core.fsmonitor", &const_str))
		const_str = getenv("GIT_TEST_FSMONITOR");

	if (const_str && *const_str) {
		fsm_settings__set_hook(r, const_str);
		return 1;
	}

	return 0;
}

static void lookup_fsmonitor_settings(struct repository *r)
{
	if (check_for_ipc(r))
		return;

	if (check_for_hook(r))
		return;

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

static void create_reason_message(struct repository *r,
				  struct strbuf *buf_reason)
{
	struct fsmonitor_settings *s = r->settings.fsmonitor;

	switch (s->reason) {
	case FSMONITOR_REASON_ZERO:
		return;

	case FSMONITOR_REASON_BARE:
		strbuf_addstr(buf_reason,
			      _("bare repos are incompatible with fsmonitor"));
		return;

	default:
		BUG("Unhandled case in create_reason_message '%d'", s->reason);
	}
}
enum fsmonitor_reason fsm_settings__get_reason(struct repository *r,
					       struct strbuf *buf_reason)
{
	strbuf_reset(buf_reason);

	if (!r->settings.fsmonitor)
		lookup_fsmonitor_settings(r);

	if (r->settings.fsmonitor->mode == FSMONITOR_MODE_INCOMPATIBLE)
		create_reason_message(r, buf_reason);

	return r->settings.fsmonitor->reason;
}
