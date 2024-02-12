#include "git-compat-util.h"
#include "config.h"
#include "repository.h"
#include "fsmonitor-ll.h"
#include "fsmonitor-settings.h"
#include "fsmonitor-path-utils.h"

/*
 * VFS for Git is incompatible with FSMonitor.
 *
 * Granted, core Git does not know anything about VFS for Git and we
 * shouldn't make assumptions about a downstream feature, but users
 * can install both versions.  And this can lead to incorrect results
 * from core Git commands.  So, without bringing in any of the VFS for
 * Git code, do a simple config test for a published config setting.
 * (We do not look at the various *_TEST_* environment variables.)
 */
static enum fsmonitor_reason check_vfs4git(struct repository *r)
{
	const char *const_str;

	if (!repo_config_get_value(r, "core.virtualfilesystem", &const_str))
		return FSMONITOR_REASON_VFS4GIT;

	return FSMONITOR_REASON_OK;
}

enum fsmonitor_reason fsm_os__incompatible(struct repository *r, int ipc UNUSED)
{
	enum fsmonitor_reason reason;

	reason = check_vfs4git(r);
	if (reason != FSMONITOR_REASON_OK)
		return reason;

	return FSMONITOR_REASON_OK;
}
