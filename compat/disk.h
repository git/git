#ifndef COMPAT_DISK_H
#define COMPAT_DISK_H

#include "git-compat-util.h"
#include "abspath.h"
#include "gettext.h"

static int get_disk_info(struct strbuf *out)
{
	struct strbuf buf = STRBUF_INIT;
	int res = 0;

#ifdef GIT_WINDOWS_NATIVE
	char volume_name[MAX_PATH], fs_name[MAX_PATH];
	DWORD serial_number, component_length, flags;
	ULARGE_INTEGER avail2caller, total, avail;

	strbuf_realpath(&buf, ".", 1);
	if (!GetDiskFreeSpaceExA(buf.buf, &avail2caller, &total, &avail)) {
		error(_("could not determine free disk size for '%s'"),
		      buf.buf);
		res = -1;
		goto cleanup;
	}

	strbuf_setlen(&buf, offset_1st_component(buf.buf));
	if (!GetVolumeInformationA(buf.buf, volume_name, sizeof(volume_name),
				   &serial_number, &component_length, &flags,
				   fs_name, sizeof(fs_name))) {
		error(_("could not get info for '%s'"), buf.buf);
		res = -1;
		goto cleanup;
	}
	strbuf_addf(out, "Available space on '%s': ", buf.buf);
	strbuf_humanise_bytes(out, avail2caller.QuadPart);
	strbuf_addch(out, '\n');
#else
	struct statvfs stat;

	strbuf_realpath(&buf, ".", 1);
	if (statvfs(buf.buf, &stat) < 0) {
		error_errno(_("could not determine free disk size for '%s'"),
			    buf.buf);
		res = -1;
		goto cleanup;
	}

	strbuf_addf(out, "Available space on '%s': ", buf.buf);
	strbuf_humanise_bytes(out, (off_t)stat.f_bsize * (off_t)stat.f_bavail);
	strbuf_addf(out, " (mount flags 0x%lx)\n", stat.f_flag);
#endif

cleanup:
	strbuf_release(&buf);
	return res;
}

#endif /* COMPAT_DISK_H */
