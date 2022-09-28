#include "git-compat-util.h"
#include "abspath.h"
#include "fsmonitor.h"
#include "fsmonitor-path-utils.h"
#include "fsm-path-utils-linux.h"
#include <errno.h>
#include <mntent.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>

static int is_remote_fs(const char *path)
{
	struct statfs fs;

	if (statfs(path, &fs))
		return error_errno(_("statfs('%s') failed"), path);

	switch (fs.f_type) {
	case ACFS_SUPER_MAGIC:
	case AFS_SUPER_MAGIC:
	case CEPH_SUPER_MAGIC:
	case CIFS_SUPER_MAGIC:
	case CODA_SUPER_MAGIC:
	case FHGFS_SUPER_MAGIC:
	case GFS_SUPER_MAGIC:
	case GPFS_SUPER_MAGIC:
	case IBRIX_SUPER_MAGIC:
	case KAFS_SUPER_MAGIC:
	case LUSTRE_SUPER_MAGIC:
	case NCP_SUPER_MAGIC:
	case NFS_SUPER_MAGIC:
	case NFSD_SUPER_MAGIC:
	case OCFS2_SUPER_MAGIC:
	case PANFS_SUPER_MAGIC:
	case SMB_SUPER_MAGIC:
	case SMB2_SUPER_MAGIC:
	case SNFS_SUPER_MAGIC:
	case VMHGFS_SUPER_MAGIC:
	case VXFS_SUPER_MAGIC:
		return 1;
	default:
		return 0;
	}
}

static int find_mount(const char *path, const struct statvfs *fs,
			struct mntent *entry)
{
	const char *const mounts = "/proc/mounts";
	char *rp = real_pathdup(path, 1);
	struct mntent *ment = NULL;
	struct statvfs mntfs;
	FILE *fp;
	int found = 0;
	int ret = 0;
	size_t dlen, plen, flen = 0;

	entry->mnt_fsname = NULL;
	entry->mnt_dir = NULL;
	entry->mnt_type = NULL;

	fp = setmntent(mounts, "r");
	if (!fp) {
		free(rp);
		return error_errno(_("setmntent('%s') failed"), mounts);
	}

	plen = strlen(rp);

	/* read all the mount information and compare to path */
	while ((ment = getmntent(fp))) {
		if (statvfs(ment->mnt_dir, &mntfs)) {
			switch (errno) {
			case EPERM:
			case ESRCH:
			case EACCES:
				continue;
			default:
				error_errno(_("statvfs('%s') failed"), ment->mnt_dir);
				ret = -1;
				goto done;
			}
		}

		/* is mount on the same filesystem and is a prefix of the path */
		if ((fs->f_fsid == mntfs.f_fsid) &&
			!strncmp(ment->mnt_dir, rp, strlen(ment->mnt_dir))) {
			dlen = strlen(ment->mnt_dir);
			if (dlen > plen)
				continue;
			/*
			 * look for the longest prefix (including root)
			 */
			if (dlen > flen &&
				((dlen == 1 && ment->mnt_dir[0] == '/') ||
				 (!rp[dlen] || rp[dlen] == '/'))) {
				flen = dlen;
				found = 1;

				/*
				 * https://man7.org/linux/man-pages/man3/getmntent.3.html
				 *
				 * The pointer points to a static area of memory which is
				 * overwritten by subsequent calls to getmntent().
				 */
				free(entry->mnt_fsname);
				free(entry->mnt_dir);
				free(entry->mnt_type);
				entry->mnt_fsname = xstrdup(ment->mnt_fsname);
				entry->mnt_dir = xstrdup(ment->mnt_dir);
				entry->mnt_type = xstrdup(ment->mnt_type);
			}
		}
	}

done:
	free(rp);
	endmntent(fp);

	if (!found)
		return -1;

	return ret;
}

int fsmonitor__get_fs_info(const char *path, struct fs_info *fs_info)
{
	int ret = 0;
	struct mntent entry;
	struct statvfs fs;

	fs_info->is_remote = -1;
	fs_info->typename = NULL;

	if (statvfs(path, &fs))
		return error_errno(_("statvfs('%s') failed"), path);

	if (find_mount(path, &fs, &entry) < 0) {
		ret = -1;
		goto done;
	}

	trace_printf_key(&trace_fsmonitor,
			 "statvfs('%s') [flags 0x%08lx] '%s' '%s'",
			 path, fs.f_flag, entry.mnt_type, entry.mnt_fsname);

	fs_info->is_remote = is_remote_fs(entry.mnt_dir);
	fs_info->typename = xstrdup(entry.mnt_fsname);

	if (fs_info->is_remote < 0)
		ret = -1;

	trace_printf_key(&trace_fsmonitor,
				"'%s' is_remote: %d",
				path, fs_info->is_remote);

done:
	free(entry.mnt_fsname);
	free(entry.mnt_dir);
	free(entry.mnt_type);
	return ret;
}

int fsmonitor__is_fs_remote(const char *path)
{
	int ret = 0;
	struct fs_info fs;

	if (fsmonitor__get_fs_info(path, &fs))
		ret = -1;
	else
		ret = fs.is_remote;

	free(fs.typename);

	return ret;
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
