#include "git-compat-util.h"
#include "config.h"
#include "fsmonitor-ll.h"
#include "fsmonitor-ipc.h"
#include "fsmonitor-settings.h"
#include "fsmonitor-path-utils.h"

 /*
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
 * FAT32 and NTFS working directories are problematic too.
 *
 * The builtin FSMonitor uses a Unix domain socket in the .git
 * directory for IPC.  These Windows drive formats do not support
 * Unix domain sockets, so mark them as incompatible for the daemon.
 *
 */
static enum fsmonitor_reason check_uds_volume(struct repository *r)
{
	struct fs_info fs;
	const char *ipc_path = fsmonitor_ipc__get_path(r);
	struct strbuf path = STRBUF_INIT;
	strbuf_add(&path, ipc_path, strlen(ipc_path));

	if (fsmonitor__get_fs_info(dirname(path.buf), &fs) == -1) {
		strbuf_release(&path);
		return FSMONITOR_REASON_ERROR;
	}

	strbuf_release(&path);

	if (fs.is_remote ||
		!strcmp(fs.typename, "msdos") ||
		!strcmp(fs.typename, "ntfs")) {
		free(fs.typename);
		return FSMONITOR_REASON_NOSOCKETS;
	}

	free(fs.typename);
	return FSMONITOR_REASON_OK;
}

enum fsmonitor_reason fsm_os__incompatible(struct repository *r, int ipc)
{
	enum fsmonitor_reason reason;

	if (ipc) {
		reason = check_uds_volume(r);
		if (reason != FSMONITOR_REASON_OK)
			return reason;
	}

	return FSMONITOR_REASON_OK;
}
