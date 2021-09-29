#include "cache.h"
#include "config.h"
#include "repository.h"
#include "fsmonitor-settings.h"
#include "fsmonitor.h"

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
 * incompatible.
 *
 * Notes for testing:
 *
 * (a) Windows allows a network share to be mapped to a drive letter.
 *     (This is the normal method to access it.)
 *
 *     $ NET USE Z: \\server\share
 *     $ git -C Z:/repo status
 *
 * (b) Windows allows a network share to be referenced WITHOUT mapping
 *     it to drive letter.
 *
 *     $ NET USE \\server\share\dir
 *     $ git -C //server/share/repo status
 *
 * (c) Windows allows "SUBST" to create a fake drive mapping to an
 *     arbitrary path (which may be remote)
 *
 *     $ SUBST Q: Z:\repo
 *     $ git -C Q:/ status
 *
 * (d) Windows allows a directory symlink to be created on a local
 *     file system that points to a remote repo.
 *
 *     $ mklink /d ./link //server/share/repo
 *     $ git -C ./link status
 */
static enum fsmonitor_reason is_remote(struct repository *r)
{
	wchar_t wpath[MAX_LONG_PATH];
	wchar_t wfullpath[MAX_LONG_PATH];
	size_t wlen;
	UINT driveType;

	/*
	 * Do everything in wide chars because the drive letter might be
	 * a multi-byte sequence.  See win32_has_dos_drive_prefix().
	 */
	if (xutftowcs_long_path(wpath, r->worktree) < 0)
		return FSMONITOR_REASON_ZERO;

	/*
	 * GetDriveTypeW() requires a final slash.  We assume that the
	 * worktree pathname points to an actual directory.
	 */
	wlen = wcslen(wpath);
	if (wpath[wlen - 1] != L'\\' && wpath[wlen - 1] != L'/') {
		wpath[wlen++] = L'\\';
		wpath[wlen] = 0;
	}

	/*
	 * Normalize the path.  If nothing else, this converts forward
	 * slashes to backslashes.  This is essential to get GetDriveTypeW()
	 * correctly handle some UNC "\\server\share\..." paths.
	 */
	if (!GetFullPathNameW(wpath, MAX_LONG_PATH, wfullpath, NULL))
		return FSMONITOR_REASON_ZERO;

	driveType = GetDriveTypeW(wfullpath);
	trace_printf_key(&trace_fsmonitor,
			 "DriveType '%s' L'%ls' (%u)",
			 r->worktree, wfullpath, driveType);

	if (driveType == DRIVE_REMOTE) {
		trace_printf_key(&trace_fsmonitor,
				 "is_remote('%s') true",
				 r->worktree);
		return FSMONITOR_REASON_REMOTE;
	}

	return FSMONITOR_REASON_ZERO;
}

enum fsmonitor_reason fsm_os__incompatible(struct repository *r)
{
	enum fsmonitor_reason reason;

	reason = is_virtual(r);
	if (reason)
		return reason;

	reason = is_remote(r);
	if (reason)
		return reason;

	return FSMONITOR_REASON_ZERO;
}
