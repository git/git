#include "cache.h"
#include "config.h"
#include "repository.h"
#include "fsmonitor-settings.h"
#include "fsmonitor.h"
#include <sys/param.h>
#include <sys/mount.h>

/*
 * Remote working directories are problematic for FSMonitor.
 *
 * The underlying file system on the server machine and/or the remote
 * mount type (NFS, SAMBA, etc.) dictates whether notification events
 * are available at all to remote client machines.
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
 * For the builtin FSMonitor, we create the Unix domain socket for the
 * IPC in the .git directory.  If the working directory is remote,
 * then the socket will be created on the remote file system.  This
 * can fail if the remote file system does not support UDS file types
 * (e.g. smbfs to a Windows server) or if the remote kernel does not
 * allow a non-local process to bind() the socket.  (These problems
 * could be fixed by moving the UDS out of the .git directory and to a
 * well-known local directory on the client machine, but care should
 * be taken to ensure that $HOME is actually local and not a managed
 * file share.)
 *
 * So (for now at least), mark remote working directories as
 * incompatible.
 */
static enum fsmonitor_reason check_remote(struct repository *r)
{
	struct statfs fs;

	if (statfs(r->worktree, &fs) == -1) {
		int saved_errno = errno;
		trace_printf_key(&trace_fsmonitor, "statfs('%s') failed: %s",
				 r->worktree, strerror(saved_errno));
		errno = saved_errno;
		return FSMONITOR_REASON_ERROR;
	}

	trace_printf_key(&trace_fsmonitor,
			 "statfs('%s') [type 0x%08x][flags 0x%08x] '%s'",
			 r->worktree, fs.f_type, fs.f_flags, fs.f_fstypename);

	if (!(fs.f_flags & MNT_LOCAL))
		return FSMONITOR_REASON_REMOTE;

	return FSMONITOR_REASON_OK;
}

enum fsmonitor_reason fsm_os__incompatible(struct repository *r)
{
	enum fsmonitor_reason reason;

	reason = check_remote(r);
	if (reason != FSMONITOR_REASON_OK)
		return reason;

	return FSMONITOR_REASON_OK;
}
