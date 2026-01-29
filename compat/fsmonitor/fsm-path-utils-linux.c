#include "git-compat-util.h"
#include "fsmonitor-ll.h"
#include "fsmonitor-path-utils.h"
#include "gettext.h"
#include "trace.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/statfs.h>

#ifdef HAVE_LINUX_MAGIC_H
#include <linux/magic.h>
#endif

/*
 * Filesystem magic numbers for remote filesystems.
 * Defined here if not available in linux/magic.h.
 */
#ifndef CIFS_SUPER_MAGIC
#define CIFS_SUPER_MAGIC 0xff534d42
#endif
#ifndef SMB_SUPER_MAGIC
#define SMB_SUPER_MAGIC 0x517b
#endif
#ifndef SMB2_SUPER_MAGIC
#define SMB2_SUPER_MAGIC 0xfe534d42
#endif
#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif
#ifndef AFS_SUPER_MAGIC
#define AFS_SUPER_MAGIC 0x5346414f
#endif
#ifndef CODA_SUPER_MAGIC
#define CODA_SUPER_MAGIC 0x73757245
#endif
#ifndef V9FS_MAGIC
#define V9FS_MAGIC 0x01021997
#endif
#ifndef FUSE_SUPER_MAGIC
#define FUSE_SUPER_MAGIC 0x65735546
#endif

/*
 * Check if filesystem type is a remote filesystem.
 */
static int is_remote_fs(unsigned long f_type)
{
	switch (f_type) {
	case CIFS_SUPER_MAGIC:
	case SMB_SUPER_MAGIC:
	case SMB2_SUPER_MAGIC:
	case NFS_SUPER_MAGIC:
	case AFS_SUPER_MAGIC:
	case CODA_SUPER_MAGIC:
	case FUSE_SUPER_MAGIC:
		return 1;
	default:
		return 0;
	}
}

/*
 * Get the filesystem type name for logging purposes.
 */
static const char *get_fs_typename(unsigned long f_type)
{
	switch (f_type) {
	case CIFS_SUPER_MAGIC:
		return "cifs";
	case SMB_SUPER_MAGIC:
		return "smb";
	case SMB2_SUPER_MAGIC:
		return "smb2";
	case NFS_SUPER_MAGIC:
		return "nfs";
	case AFS_SUPER_MAGIC:
		return "afs";
	case CODA_SUPER_MAGIC:
		return "coda";
	case V9FS_MAGIC:
		return "9p";
	case FUSE_SUPER_MAGIC:
		return "fuse";
	default:
		return "unknown";
	}
}

/*
 * Find the mount point for a given path by reading /proc/mounts.
 * Returns the filesystem type for the longest matching mount point.
 */
static char *find_mount(const char *path, struct statfs *fs)
{
	FILE *fp;
	struct strbuf line = STRBUF_INIT;
	struct strbuf match = STRBUF_INIT;
	struct strbuf fstype = STRBUF_INIT;
	char *result = NULL;
	struct statfs path_fs;

	if (statfs(path, &path_fs) < 0)
		return NULL;

	fp = fopen("/proc/mounts", "r");
	if (!fp)
		return NULL;

	while (strbuf_getline(&line, fp) != EOF) {
		char *fields[6];
		char *p = line.buf;
		int i;

		/* Parse mount entry: device mountpoint fstype options dump pass */
		for (i = 0; i < 6 && p; i++) {
			fields[i] = p;
			p = strchr(p, ' ');
			if (p)
				*p++ = '\0';
		}

		if (i >= 3) {
			const char *mountpoint = fields[1];
			const char *type = fields[2];
			struct statfs mount_fs;

			/* Check if this mount point is a prefix of our path */
			if (starts_with(path, mountpoint) &&
			    (path[strlen(mountpoint)] == '/' ||
			     path[strlen(mountpoint)] == '\0')) {
				/* Check if filesystem ID matches */
				if (statfs(mountpoint, &mount_fs) == 0 &&
				    !memcmp(&mount_fs.f_fsid, &path_fs.f_fsid,
					    sizeof(mount_fs.f_fsid))) {
					/* Keep the longest matching mount point */
					if (strlen(mountpoint) > match.len) {
						strbuf_reset(&match);
						strbuf_addstr(&match, mountpoint);
						strbuf_reset(&fstype);
						strbuf_addstr(&fstype, type);
						*fs = mount_fs;
					}
				}
			}
		}
	}

	fclose(fp);
	strbuf_release(&line);
	strbuf_release(&match);

	if (fstype.len)
		result = strbuf_detach(&fstype, NULL);
	else
		strbuf_release(&fstype);

	return result;
}

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
			 "statfs('%s') [type 0x%08lx]",
			 path, (unsigned long)fs.f_type);

	fs_info->is_remote = is_remote_fs(fs.f_type);

	/*
	 * Try to get filesystem type from /proc/mounts for a more
	 * descriptive name.
	 */
	fs_info->typename = find_mount(path, &fs);
	if (!fs_info->typename)
		fs_info->typename = xstrdup(get_fs_typename(fs.f_type));

	trace_printf_key(&trace_fsmonitor,
			 "'%s' is_remote: %d, typename: %s",
			 path, fs_info->is_remote, fs_info->typename);

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
 * No-op for Linux - we don't have firmlinks like macOS.
 */
int fsmonitor__get_alias(const char *path UNUSED,
			 struct alias_info *info UNUSED)
{
	return 0;
}

/*
 * No-op for Linux - we don't have firmlinks like macOS.
 */
char *fsmonitor__resolve_alias(const char *path UNUSED,
			       const struct alias_info *info UNUSED)
{
	return NULL;
}
