#include "cache.h"
#include "config.h"
#include "repository.h"
#include "fsmonitor-settings.h"

/*
 * GVFS (aka VFS for Git) is incompatible with FSMonitor.
 *
 * Granted, core Git does not know anything about GVFS and we
 * shouldn't make assumptions about a downstream feature, but users
 * can install both versions.  And this can lead to incorrect results
 * from core Git commands.  So, without bringing in any of the GVFS
 * code, do a simple config test for a published config setting.  (We
 * do not look at the various *_TEST_* environment variables.)
 */
static enum fsmonitor_reason is_virtual(struct repository *r)
{
	const char *const_str;

	if (!repo_config_get_value(r, "core.virtualfilesystem", &const_str))
		return FSMONITOR_REASON_VIRTUAL;

	return FSMONITOR_REASON_ZERO;
}

enum fsmonitor_reason fsm_os__incompatible(struct repository *r)
{
	enum fsmonitor_reason reason;

	reason = is_virtual(r);
	if (reason)
		return reason;

	return FSMONITOR_REASON_ZERO;
}
