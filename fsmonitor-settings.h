#ifndef FSMONITOR_SETTINGS_H
#define FSMONITOR_SETTINGS_H

struct repository;

enum fsmonitor_mode {
	FSMONITOR_MODE_INCOMPATIBLE = -1, /* see _reason */
	FSMONITOR_MODE_DISABLED = 0,
	FSMONITOR_MODE_HOOK = 1, /* core.fsmonitor=<hook_path> */
	FSMONITOR_MODE_IPC = 2,  /* core.fsmonitor=<true> */
};

/*
 * Incompatibility reasons.
 */
enum fsmonitor_reason {
	FSMONITOR_REASON_OK = 0, /* no incompatibility or when disbled */
	FSMONITOR_REASON_BARE,
	FSMONITOR_REASON_ERROR, /* FS error probing for compatibility */
	FSMONITOR_REASON_REMOTE,
	FSMONITOR_REASON_VFS4GIT, /* VFS for Git virtualization */
	FSMONITOR_REASON_NOSOCKETS, /* NTFS,FAT32 do not support Unix sockets */
};

void fsm_settings__set_ipc(struct repository *r);
void fsm_settings__set_hook(struct repository *r, const char *path);
void fsm_settings__set_disabled(struct repository *r);

enum fsmonitor_mode fsm_settings__get_mode(struct repository *r);
const char *fsm_settings__get_hook_path(struct repository *r);

enum fsmonitor_reason fsm_settings__get_reason(struct repository *r);
int fsm_settings__error_if_incompatible(struct repository *r);

struct fsmonitor_settings;

#ifdef HAVE_FSMONITOR_OS_SETTINGS
/*
 * Ask platform-specific code whether the repository is incompatible
 * with fsmonitor (both hook and ipc modes).  For example, if the working
 * directory is on a remote volume and mounted via a technology that does
 * not support notification events, then we should not pretend to watch it.
 *
 * fsm_os__* routines should considered private to fsm_settings__
 * routines.
 */
enum fsmonitor_reason fsm_os__incompatible(struct repository *r);
#endif /* HAVE_FSMONITOR_OS_SETTINGS */

#endif /* FSMONITOR_SETTINGS_H */
