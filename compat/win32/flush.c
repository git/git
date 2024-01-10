#include "git-compat-util.h"
#include <winternl.h>
#include "lazyload.h"

int win32_fsync_no_flush(int fd)
{
       IO_STATUS_BLOCK io_status;

#define FLUSH_FLAGS_FILE_DATA_ONLY 1

       DECLARE_PROC_ADDR(ntdll.dll, NTSTATUS, NTAPI, NtFlushBuffersFileEx,
			 HANDLE FileHandle, ULONG Flags, PVOID Parameters, ULONG ParameterSize,
			 PIO_STATUS_BLOCK IoStatusBlock);

       if (!INIT_PROC_ADDR(NtFlushBuffersFileEx)) {
		errno = ENOSYS;
		return -1;
       }

       memset(&io_status, 0, sizeof(io_status));
       if (NtFlushBuffersFileEx((HANDLE)_get_osfhandle(fd), FLUSH_FLAGS_FILE_DATA_ONLY,
				NULL, 0, &io_status)) {
		errno = EINVAL;
		return -1;
       }

       return 0;
}
