#include "fsmonitor.h"
#include "fsmonitor-path-utils.h"
#include <sys/param.h>
#include <sys/mount.h>

int fsmonitor__get_fs_info(const char *path, struct fs_info *fs_info)
{
	struct statfs fs;
	if (statfs(path, &fs) == -1) {
		int saved_errno = errno;
		trace_printf_key(&trace_fsmonitor, "statfs('%s') failed: %s",
				 path, strerror(saved_errno));
		errno = saved_errno;
		return -1;
	}

	trace_printf_key(&trace_fsmonitor,
			 "statfs('%s') [type 0x%08x][flags 0x%08x] '%s'",
			 path, fs.f_type, fs.f_flags, fs.f_fstypename);

	if (!(fs.f_flags & MNT_LOCAL))
		fs_info->is_remote = 1;
	else
		fs_info->is_remote = 0;

	fs_info->typename = xstrdup(fs.f_fstypename);

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

	free(fs.typename);

	return fs.is_remote;
}
