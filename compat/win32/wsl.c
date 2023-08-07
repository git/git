#define USE_THE_REPOSITORY_VARIABLE
#include "../../git-compat-util.h"
#include "../win32.h"
#include "../../repository.h"
#include "config.h"
#include "ntifs.h"
#include "wsl.h"

int are_wsl_compatible_mode_bits_enabled(void)
{
	/* default to `false` during initialization */
	static const int fallback = 0;
	static int enabled = -1;

	if (enabled < 0) {
		/* avoid infinite recursion */
		if (!the_repository)
			return fallback;

		if (the_repository->config &&
		    the_repository->config->hash_initialized &&
		    git_config_get_bool("core.wslcompat", &enabled) < 0)
			enabled = 0;
	}

	return enabled < 0 ? fallback : enabled;
}

int copy_wsl_mode_bits_from_disk(const wchar_t *wpath, ssize_t wpathlen,
				 _mode_t *mode)
{
	int ret = -1;
	HANDLE h;
	if (wpathlen >= 0) {
		/*
		 * It's caller's duty to make sure wpathlen is reasonable so
		 * it does not overflow.
		 */
		wchar_t *fn2 = (wchar_t*)alloca((wpathlen + 1) * sizeof(wchar_t));
		memcpy(fn2, wpath, wpathlen * sizeof(wchar_t));
		fn2[wpathlen] = 0;
		wpath = fn2;
	}
	h = CreateFileW(wpath, FILE_READ_EA | SYNCHRONIZE,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS |
				FILE_FLAG_OPEN_REPARSE_POINT,
			NULL);
	if (h != INVALID_HANDLE_VALUE) {
		ret = get_wsl_mode_bits_by_handle(h, mode);
		CloseHandle(h);
	}
	return ret;
}

#ifndef LX_FILE_METADATA_HAS_UID
#define LX_FILE_METADATA_HAS_UID 0x1
#define LX_FILE_METADATA_HAS_GID 0x2
#define LX_FILE_METADATA_HAS_MODE 0x4
#define LX_FILE_METADATA_HAS_DEVICE_ID 0x8
#define LX_FILE_CASE_SENSITIVE_DIR 0x10
typedef struct _FILE_STAT_LX_INFORMATION {
	LARGE_INTEGER FileId;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER AllocationSize;
	LARGE_INTEGER EndOfFile;
	uint32_t FileAttributes;
	uint32_t ReparseTag;
	uint32_t NumberOfLinks;
	ACCESS_MASK EffectiveAccess;
	uint32_t LxFlags;
	uint32_t LxUid;
	uint32_t LxGid;
	uint32_t LxMode;
	uint32_t LxDeviceIdMajor;
	uint32_t LxDeviceIdMinor;
} FILE_STAT_LX_INFORMATION, *PFILE_STAT_LX_INFORMATION;
#endif

/*
 * This struct is extended from the original FILE_FULL_EA_INFORMATION of
 * Microsoft Windows.
 */
struct wsl_full_ea_info_t {
	uint32_t NextEntryOffset;
	uint8_t Flags;
	uint8_t EaNameLength;
	uint16_t EaValueLength;
	char EaName[7];
	char EaValue[4];
	char Padding[1];
};

enum {
	FileStatLxInformation = 70,
};
__declspec(dllimport) NTSTATUS WINAPI
	NtQueryInformationFile(HANDLE FileHandle,
			       PIO_STATUS_BLOCK IoStatusBlock,
			       PVOID FileInformation, ULONG Length,
			       uint32_t FileInformationClass);
__declspec(dllimport) NTSTATUS WINAPI
	NtSetInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
			     PVOID FileInformation, ULONG Length,
			     uint32_t FileInformationClass);
__declspec(dllimport) NTSTATUS WINAPI
	NtSetEaFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
		    PVOID EaBuffer, ULONG EaBufferSize);

int set_wsl_mode_bits_by_handle(HANDLE h, _mode_t mode)
{
	uint32_t value = mode;
	struct wsl_full_ea_info_t ea_info;
	IO_STATUS_BLOCK iob;
	/* mode should be valid to make WSL happy */
	assert(S_ISREG(mode) || S_ISDIR(mode));
	ea_info.NextEntryOffset = 0;
	ea_info.Flags = 0;
	ea_info.EaNameLength = 6;
	ea_info.EaValueLength = sizeof(value); /* 4 */
	strlcpy(ea_info.EaName, "$LXMOD", sizeof(ea_info.EaName));
	memcpy(ea_info.EaValue, &value, sizeof(value));
	ea_info.Padding[0] = 0;
	return NtSetEaFile(h, &iob, &ea_info, sizeof(ea_info));
}

int get_wsl_mode_bits_by_handle(HANDLE h, _mode_t *mode)
{
	FILE_STAT_LX_INFORMATION fxi;
	IO_STATUS_BLOCK iob;
	if (NtQueryInformationFile(h, &iob, &fxi, sizeof(fxi),
				   FileStatLxInformation) == 0) {
		if (fxi.LxFlags & LX_FILE_METADATA_HAS_MODE)
			*mode = (_mode_t)fxi.LxMode;
		return 0;
	}
	return -1;
}
