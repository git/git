#include "cache.h"
#include "fsmonitor.h"
#include "fsmonitor-path-utils.h"

/*
 * Check remote working directory protocol.
 *
 * Return -1 if client machine cannot get remote protocol information.
 */
static int check_remote_protocol(wchar_t *wpath)
{
	HANDLE h;
	FILE_REMOTE_PROTOCOL_INFO proto_info;

	h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (h == INVALID_HANDLE_VALUE) {
		error(_("[GLE %ld] unable to open for read '%ls'"),
		      GetLastError(), wpath);
		return -1;
	}

	if (!GetFileInformationByHandleEx(h, FileRemoteProtocolInfo,
		&proto_info, sizeof(proto_info))) {
		error(_("[GLE %ld] unable to get protocol information for '%ls'"),
		      GetLastError(), wpath);
		CloseHandle(h);
		return -1;
	}

	CloseHandle(h);

	trace_printf_key(&trace_fsmonitor,
				"check_remote_protocol('%ls') remote protocol %#8.8lx",
				wpath, proto_info.Protocol);

	return 0;
}

/*
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
int fsmonitor__get_fs_info(const char *path, struct fs_info *fs_info)
{
	wchar_t wpath[MAX_PATH];
	wchar_t wfullpath[MAX_PATH];
	size_t wlen;
	UINT driveType;

	/*
	 * Do everything in wide chars because the drive letter might be
	 * a multi-byte sequence.  See win32_has_dos_drive_prefix().
	 */
	if (xutftowcs_path(wpath, path) < 0) {
		return -1;
	}

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
	if (!GetFullPathNameW(wpath, MAX_PATH, wfullpath, NULL)) {
		return -1;
	}

	driveType = GetDriveTypeW(wfullpath);
	trace_printf_key(&trace_fsmonitor,
			 "DriveType '%s' L'%ls' (%u)",
			 path, wfullpath, driveType);

	if (driveType == DRIVE_REMOTE) {
		fs_info->is_remote = 1;
		if (check_remote_protocol(wfullpath) < 0)
			return -1;
	} else {
		fs_info->is_remote = 0;
	}

	trace_printf_key(&trace_fsmonitor,
				"'%s' is_remote: %d",
				path, fs_info->is_remote);

	return 0;
}

int fsmonitor__is_fs_remote(const char *path)
{
	struct fs_info fs;
	if (fsmonitor__get_fs_info(path, &fs))
		return -1;
	return fs.is_remote;
}

/*
 * No-op for now.
 */
int fsmonitor__get_alias(const char *path, struct alias_info *info)
{
	return 0;
}

/*
 * No-op for now.
 */
char *fsmonitor__resolve_alias(const char *path,
	const struct alias_info *info)
{
	return NULL;
}
