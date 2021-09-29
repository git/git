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
	char *hook_path;
};

void fsm_settings__set_ipc(struct repository *r)
{
	struct fsmonitor_settings *s = r->settings.fsmonitor;

	s->mode = FSMONITOR_MODE_IPC;
}

void fsm_settings__set_hook(struct repository *r, const char *path)
{
	struct fsmonitor_settings *s = r->settings.fsmonitor;

	s->mode = FSMONITOR_MODE_HOOK;
	s->hook_path = strdup(path);
}

void fsm_settings__set_disabled(struct repository *r)
{
	struct fsmonitor_settings *s = r->settings.fsmonitor;

	s->mode = FSMONITOR_MODE_DISABLED;
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
	struct fsmonitor_settings *s;

	CALLOC_ARRAY(s, 1);

	r->settings.fsmonitor = s;

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
