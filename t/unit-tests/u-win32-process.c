#include "unit-test.h"

#ifndef GIT_WINDOWS_NATIVE
void test_win32_process__cleanup(void) {}
void test_win32_process__identity(void) { cl_skip(); }
#else
static HANDLE cleanup_handle;
static int process_open_count;
static FILETIME sampled_tick;

static void record_snapshot_time(FILETIME *time)
{
	GetSystemTimeAsFileTime(time);
	sampled_tick = *time;
}

static HANDLE checked_open_process(DWORD access, BOOL inherit, DWORD pid)
{
	cl_assert(!cleanup_handle);
	cleanup_handle = OpenProcess(access, inherit, pid);
	process_open_count++;
	return cleanup_handle;
}

static BOOL checked_close_handle(HANDLE handle)
{
	BOOL closed = CloseHandle(handle);

	if (closed && handle == cleanup_handle)
		cleanup_handle = NULL;
	return closed;
}

#define OpenProcess checked_open_process
#define CloseHandle checked_close_handle
#define GetSystemTimeAsFileTime record_snapshot_time
#include "compat/win32/exit-process.h"
#undef OpenProcess
#undef CloseHandle
#undef GetSystemTimeAsFileTime

static PROCESS_INFORMATION child_process;
static PROCESS_INFORMATION descendants[3];
static HANDLE process;
static struct process_snapshot *snapshot;
static LPPROC_THREAD_ATTRIBUTE_LIST attributes;
static int attributes_initialized;
static report_fn saved_warn_routine;
static struct strbuf warning_message = STRBUF_INIT;

static void capture_warning(const char *format, va_list args)
{
	strbuf_vaddf(&warning_message, format, args);
}

void test_win32_process__cleanup(void)
{
	size_t i;

	if (saved_warn_routine) {
		set_warn_routine(saved_warn_routine);
		saved_warn_routine = NULL;
	}
	strbuf_release(&warning_message);
	if (cleanup_handle) {
		cl_assert(CloseHandle(cleanup_handle));
		cleanup_handle = NULL;
	}
	FREE_AND_NULL(snapshot);
	if (attributes_initialized)
		DeleteProcThreadAttributeList(attributes);
	FREE_AND_NULL(attributes);
	attributes_initialized = 0;
	if (process) {
		cl_assert(CloseHandle(process));
		process = NULL;
	}
	for (i = 0; i <= ARRAY_SIZE(descendants); i++) {
		PROCESS_INFORMATION *child = i < ARRAY_SIZE(descendants) ?
			&descendants[i] : &child_process;

		if (!child->hProcess)
			continue;
		if (WaitForSingleObject(child->hProcess, 0) == WAIT_TIMEOUT)
			cl_assert(TerminateProcess(child->hProcess, 0));
		cl_assert_equal_i(WaitForSingleObject(child->hProcess, 10000),
				  WAIT_OBJECT_0);
		cl_assert_equal_i(exit_process(child->hProcess, 0), 0);
		cl_assert(CloseHandle(child->hThread));
		child->hProcess = NULL;
	}
}

void test_win32_process__identity(void)
{
	STARTUPINFOW startup = { .cb = sizeof(startup) };
	STARTUPINFOEXW extended = {
		.StartupInfo = { .cb = sizeof(extended) },
	};
	wchar_t path[32768];
	FILETIME parent_time, creation_time, later_time, earlier_time;
	FILETIME snapshot_time;
	FILETIME exit_time, kernel_time, user_time;
	const struct process_snapshot *entry;
	DWORD path_len, exit_code, count;
	SIZE_T size = 0;
	size_t i;
	int ret;

	path_len = GetModuleFileNameW(NULL, path, ARRAY_SIZE(path));
	cl_assert(path_len && path_len < ARRAY_SIZE(path));
	cl_assert(GetProcessTimes(GetCurrentProcess(), &parent_time, &exit_time,
				  &kernel_time, &user_time));
	cl_assert(CreateProcessW(path, NULL, NULL, NULL, FALSE,
				CREATE_SUSPENDED, NULL, NULL,
				&startup, &child_process));
	cl_assert(GetProcessTimes(child_process.hProcess, &creation_time,
				  &exit_time, &kernel_time, &user_time));
	snapshot = get_process_snapshot(&snapshot_time);
	cl_assert(snapshot);
	cl_assert_equal_i(CompareFileTime(&snapshot_time, &sampled_tick), 0);
	for (entry = snapshot; entry; entry = next_process_snapshot(entry))
		if (entry->pid == child_process.dwProcessId)
			break;
	cl_assert(entry);
	cl_assert_equal_i(CompareFileTime(&entry->creation_time,
					&creation_time), 0);
	later_time = creation_time;
	if (!++later_time.dwLowDateTime)
		later_time.dwHighDateTime++;
	cl_assert(process_is_child(entry, GetCurrentProcessId(), &parent_time,
				   &later_time));
	cl_assert(process_started_at(child_process.hProcess, &creation_time));
	cl_assert(process_is_child(entry, GetCurrentProcessId(),
				   &creation_time, &later_time));
	cl_assert(!process_is_child(entry, 0, &parent_time, &later_time));
	cl_assert(!process_started_at(child_process.hProcess, &later_time));
	cl_assert(!process_is_child(entry, GetCurrentProcessId(), &later_time,
				    &snapshot_time));
	cl_assert(!process_is_child(entry, GetCurrentProcessId(), &parent_time,
				    &creation_time));
	cl_assert(process_is_child(entry, GetCurrentProcessId(), &parent_time,
				   &later_time));
	earlier_time = creation_time;
	if (!earlier_time.dwLowDateTime--)
		earlier_time.dwHighDateTime--;
	cl_assert(!process_is_child(entry, GetCurrentProcessId(), &parent_time,
				    &earlier_time));
	FREE_AND_NULL(snapshot);

	process = OpenProcess(PROCESS_TERMINATE, FALSE,
			      child_process.dwProcessId);
	cl_assert(process);
	cl_assert(!process_started_at(process, &creation_time));
	cl_assert_equal_i(GetLastError(), ERROR_ACCESS_DENIED);
	saved_warn_routine = get_warn_routine();
	set_warn_routine(capture_warning);
	ret = terminate_process_tree(process, 143);
	process = NULL;
	set_warn_routine(saved_warn_routine);
	saved_warn_routine = NULL;
	cl_assert_equal_i(ret, 0);
	cl_assert_equal_s(warning_message.buf,
			  "could not query the root process (Windows error 5); "
			  "terminating only that process");
	cl_assert_equal_i(WaitForSingleObject(child_process.hProcess, 10000),
			  WAIT_OBJECT_0);
	cl_assert(GetExitCodeProcess(child_process.hProcess, &exit_code));
	cl_assert_equal_i(exit_code, 143);

	test_win32_process__cleanup();
	cl_assert(CreateProcessW(path, NULL, NULL, NULL, FALSE,
				CREATE_SUSPENDED, NULL, NULL,
				&startup, &child_process));
	cl_assert(!InitializeProcThreadAttributeList(NULL, 1, 0, &size));
	attributes = xmalloc(size);
	extended.lpAttributeList = attributes;
	for (i = 0; i < ARRAY_SIZE(descendants); i++) {
		HANDLE parent = i == 1 ? descendants[0].hProcess :
			child_process.hProcess;

		cl_assert(InitializeProcThreadAttributeList(attributes, 1, 0,
							  &size));
		attributes_initialized = 1;
		cl_assert(UpdateProcThreadAttribute(attributes, 0,
			PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &parent,
			sizeof(parent), NULL, NULL));
		cl_assert(CreateProcessW(path, NULL, NULL, NULL, FALSE,
			CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
			NULL, NULL, &extended.StartupInfo, &descendants[i]));
		DeleteProcThreadAttributeList(attributes);
		attributes_initialized = 0;
	}
	/* The fixtures must precede the cutoff tick to be eligible. */
	cl_assert(GetProcessTimes(descendants[2].hProcess, &creation_time,
				  &exit_time, &kernel_time, &user_time));
	count = GetTickCount();
	GetSystemTimeAsFileTime(&snapshot_time);
	while (CompareFileTime(&creation_time, &snapshot_time) >= 0) {
		cl_assert(GetTickCount() - count < 10000);
		SwitchToThread();
		GetSystemTimeAsFileTime(&snapshot_time);
	}
	process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
			      PROCESS_TERMINATE, FALSE,
			      child_process.dwProcessId);
	cl_assert(process);
	process_open_count = 0;
	ret = terminate_process_tree(process, 143);
	process = NULL;
	cl_assert_equal_i(ret, 0);
	cl_assert_equal_i(process_open_count, ARRAY_SIZE(descendants));
	cl_assert(!cleanup_handle);
	for (i = 0; i <= ARRAY_SIZE(descendants); i++) {
		HANDLE child = i < ARRAY_SIZE(descendants) ?
			descendants[i].hProcess : child_process.hProcess;

		cl_assert_equal_i(WaitForSingleObject(child, 10000),
				  WAIT_OBJECT_0);
		cl_assert(GetExitCodeProcess(child, &exit_code));
		cl_assert_equal_i(exit_code, 143);
	}
}
#endif
