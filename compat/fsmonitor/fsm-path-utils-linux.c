#include "fsmonitor.h"
#include "fsmonitor-path-utils.h"
#include <errno.h>
#include <mntent.h>
#include <sys/mount.h>
#include <sys/vfs.h>
#include <sys/statvfs.h>

static int is_remote_fs(const char* path) {
	struct statfs fs;

	if (statfs(path, &fs)) {
		error_errno(_("statfs('%s') failed"), path);
		return -1;
	}

	switch (fs.f_type) {
		case 0x61636673:  /* ACFS */
		case 0x5346414F:  /* AFS */
		case 0x00C36400:  /* CEPH */
		case 0xFF534D42:  /* CIFS */
		case 0x73757245:  /* CODA */
		case 0x19830326:  /* FHGFS */
		case 0x1161970:   /* GFS */
		case 0x47504653:  /* GPFS */
		case 0x013111A8:  /* IBRIX */
		case 0x6B414653:  /* KAFS */
		case 0x0BD00BD0:  /* LUSTRE */
		case 0x564C:      /* NCP */
		case 0x6969:      /* NFS */
		case 0x6E667364:  /* NFSD */
		case 0x7461636f:  /* OCFS2 */
		case 0xAAD7AAEA:  /* PANFS */
		case 0x517B:      /* SMB */
		case 0xBEEFDEAD:  /* SNFS */
		case 0xFE534D42:  /* SMB2 */
		case 0xBACBACBC:  /* VMHGFS */
		case 0xA501FCF5:  /* VXFS */
			return 1;
		default:
			break;
	}

	return 0;
}

static int find_mount(const char *path, const struct statvfs *fs,
	struct mntent *ent)
{
	const char *const mounts = "/proc/mounts";
	const char *rp = real_pathdup(path, 1);
	struct mntent *ment = NULL;
	struct statvfs mntfs;
	FILE *fp;
	int found = 0;
	int dlen, plen, flen = 0;

	ent->mnt_fsname = NULL;
	ent->mnt_dir = NULL;
	ent->mnt_type = NULL;

	fp = setmntent(mounts, "r");
	if (!fp) {
		error_errno(_("setmntent('%s') failed"), mounts);
		return -1;
	}

	plen = strlen(rp);

	/* read all the mount information and compare to path */
	while ((ment = getmntent(fp)) != NULL) {
		if (statvfs(ment->mnt_dir, &mntfs)) {
			switch (errno) {
			case EPERM:
			case ESRCH:
			case EACCES:
				continue;
			default:
				error_errno(_("statvfs('%s') failed"), ment->mnt_dir);
				endmntent(fp);
				return -1;
			}
		}

		/* is mount on the same filesystem and is a prefix of the path */
		if ((fs->f_fsid == mntfs.f_fsid) &&
			!strncmp(ment->mnt_dir, rp, strlen(ment->mnt_dir))) {
			dlen = strlen(ment->mnt_dir);
			if (dlen > plen)
				continue;
			/*
			 * root is always a potential match; otherwise look for
			 * directory prefix
			 */
			if ((dlen == 1 && ment->mnt_dir[0] == '/') ||
				(dlen > flen && (!rp[dlen] || rp[dlen] == '/'))) {
				flen = dlen;
				/*
				 * https://man7.org/linux/man-pages/man3/getmntent.3.html
				 *
				 * The pointer points to a static area of memory which is
				 * overwritten by subsequent calls to getmntent().
				 */
				found = 1;
				free(ent->mnt_fsname);
				free(ent->mnt_dir);
				free(ent->mnt_type);
				ent->mnt_fsname = xstrdup(ment->mnt_fsname);
				ent->mnt_dir = xstrdup(ment->mnt_dir);
				ent->mnt_type = xstrdup(ment->mnt_type);
			}
		}
	}
	endmntent(fp);

	if (!found)
		return -1;

	return 0;
}

int fsmonitor__get_fs_info(const char *path, struct fs_info *fs_info)
{
	struct mntent ment;
	struct statvfs fs;

	if (statvfs(path, &fs))
		return error_errno(_("statvfs('%s') failed"), path);


	if (find_mount(path, &fs, &ment) < 0) {
		free(ment.mnt_fsname);
		free(ment.mnt_dir);
		free(ment.mnt_type);
		return -1;
	}

	trace_printf_key(&trace_fsmonitor,
			 "statvfs('%s') [flags 0x%08lx] '%s' '%s'",
			 path, fs.f_flag, ment.mnt_type, ment.mnt_fsname);

	fs_info->is_remote = is_remote_fs(ment.mnt_dir);
	fs_info->typename = ment.mnt_fsname;
	free(ment.mnt_dir);
	free(ment.mnt_type);

	if (fs_info->is_remote < 0) {
		free(ment.mnt_fsname);
		return -1;
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

	free(fs.typename);

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
