#define DISABLE_SIGN_COMPARE_WARNINGS

#include "test-tool.h"
#include "git-compat-util.h"

#if defined(GIT_WINDOWS_NATIVE)
#include "lazyload.h"
#include <winnt.h>

static int cmd_sync(void)
{
	char Buffer[MAX_PATH];
	DWORD dwRet;
	char szVolumeAccessPath[] = "\\\\.\\XXXX:";
	HANDLE hVolWrite;
	int success = 0, dos_drive_prefix;

	dwRet = GetCurrentDirectory(MAX_PATH, Buffer);
	if ((0 == dwRet) || (dwRet > MAX_PATH))
		return error("Error getting current directory");

	dos_drive_prefix = has_dos_drive_prefix(Buffer);
	if (!dos_drive_prefix)
		return error("'%s': invalid drive letter", Buffer);

	memcpy(szVolumeAccessPath, Buffer, dos_drive_prefix);
	szVolumeAccessPath[dos_drive_prefix] = '\0';

	hVolWrite = CreateFile(szVolumeAccessPath, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (INVALID_HANDLE_VALUE == hVolWrite)
		return error("Unable to open volume for writing, need admin access");

	success = FlushFileBuffers(hVolWrite);
	if (!success)
		error("Unable to flush volume");

	CloseHandle(hVolWrite);

	return !success;
}

#define STATUS_SUCCESS			(0x00000000L)
#define STATUS_PRIVILEGE_NOT_HELD	(0xC0000061L)

typedef enum _SYSTEM_INFORMATION_CLASS {
	SystemMemoryListInformation = 80,
} SYSTEM_INFORMATION_CLASS;

typedef enum _SYSTEM_MEMORY_LIST_COMMAND {
	MemoryCaptureAccessedBits,
	MemoryCaptureAndResetAccessedBits,
	MemoryEmptyWorkingSets,
	MemoryFlushModifiedList,
	MemoryPurgeStandbyList,
	MemoryPurgeLowPriorityStandbyList,
	MemoryCommandMax
} SYSTEM_MEMORY_LIST_COMMAND;

static BOOL GetPrivilege(HANDLE TokenHandle, LPCSTR lpName, int flags)
{
	BOOL bResult;
	DWORD dwBufferLength;
	LUID luid;
	TOKEN_PRIVILEGES tpPreviousState;
	TOKEN_PRIVILEGES tpNewState;

	dwBufferLength = 16;
	bResult = LookupPrivilegeValueA(0, lpName, &luid);
	if (bResult) {
		tpNewState.PrivilegeCount = 1;
		tpNewState.Privileges[0].Luid = luid;
		tpNewState.Privileges[0].Attributes = 0;
		bResult = AdjustTokenPrivileges(TokenHandle, 0, &tpNewState,
			(DWORD)((LPBYTE)&(tpNewState.Privileges[1]) - (LPBYTE)&tpNewState),
			&tpPreviousState, &dwBufferLength);
		if (bResult) {
			tpPreviousState.PrivilegeCount = 1;
			tpPreviousState.Privileges[0].Luid = luid;
			tpPreviousState.Privileges[0].Attributes = flags != 0 ? 2 : 0;
			bResult = AdjustTokenPrivileges(TokenHandle, 0, &tpPreviousState,
				dwBufferLength, 0, 0);
		}
	}
	return bResult;
}

static int cmd_dropcaches(void)
{
	HANDLE hProcess = GetCurrentProcess();
	HANDLE hToken;
	DECLARE_PROC_ADDR(ntdll.dll, DWORD, NTAPI, NtSetSystemInformation, INT, PVOID,
		ULONG);
	SYSTEM_MEMORY_LIST_COMMAND command;
	int status;

	if (!OpenProcessToken(hProcess, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
		return error("Can't open current process token");

	if (!GetPrivilege(hToken, "SeProfileSingleProcessPrivilege", 1))
		return error("Can't get SeProfileSingleProcessPrivilege");

	CloseHandle(hToken);

	if (!INIT_PROC_ADDR(NtSetSystemInformation))
		return error("Could not find NtSetSystemInformation() function");

	command = MemoryPurgeStandbyList;
	status = NtSetSystemInformation(
		SystemMemoryListInformation,
		&command,
		sizeof(SYSTEM_MEMORY_LIST_COMMAND)
	);
	if (status == STATUS_PRIVILEGE_NOT_HELD)
		error("Insufficient privileges to purge the standby list, need admin access");
	else if (status != STATUS_SUCCESS)
		error("Unable to execute the memory list command %d", status);

	return status;
}

#elif defined(__linux__)

static int cmd_sync(void)
{
	return system("sync");
}

static int cmd_dropcaches(void)
{
	return system("echo 3 | sudo tee /proc/sys/vm/drop_caches");
}

#elif defined(__APPLE__)

static int cmd_sync(void)
{
	return system("sync");
}

static int cmd_dropcaches(void)
{
	return system("sudo purge");
}

#else

static int cmd_sync(void)
{
	return 0;
}

static int cmd_dropcaches(void)
{
	return error("drop caches not implemented on this platform");
}

#endif

int cmd__drop_caches(int argc UNUSED, const char **argv UNUSED)
{
	cmd_sync();
	return cmd_dropcaches();
}
