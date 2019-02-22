#include "../../cache.h"
#include "../../json-writer.h"
#include <Psapi.h>
#include <tlHelp32.h>

/*
 * Find the process data for the given PID in the given snapshot
 * and update the PROCESSENTRY32 data.
 */
static int find_pid(DWORD pid, HANDLE hSnapshot, PROCESSENTRY32 *pe32)
{
	pe32->dwSize = sizeof(PROCESSENTRY32);

	if (Process32First(hSnapshot, pe32)) {
		do {
			if (pe32->th32ProcessID == pid)
				return 1;
		} while (Process32Next(hSnapshot, pe32));
	}
	return 0;
}

/*
 * Accumulate JSON array:
 *     [
 *         exe-name-parent,
 *         exe-name-grand-parent,
 *         ...
 *     ]
 *
 * Note: we only report the filename of the process executable; the
 *       only way to get its full pathname is to use OpenProcess()
 *       and GetModuleFileNameEx() or QueryfullProcessImageName()
 *       and that seems rather expensive (on top of the cost of
 *       getting the snapshot).
 */
static void get_processes(struct json_writer *jw, HANDLE hSnapshot)
{
	PROCESSENTRY32 pe32;
	DWORD pid;

	pid = GetCurrentProcessId();

	/* We only want parent processes, so skip self. */
	if (!find_pid(pid, hSnapshot, &pe32))
		return;
	pid = pe32.th32ParentProcessID;

	while (find_pid(pid, hSnapshot, &pe32)) {
		jw_array_string(jw, pe32.szExeFile);

		pid = pe32.th32ParentProcessID;
	}
}

/*
 * Emit JSON data for the current and parent processes.  Individual
 * trace2 targets can decide how to actually print it.
 */
static void get_ancestry(void)
{
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

	if (hSnapshot != INVALID_HANDLE_VALUE) {
		struct json_writer jw = JSON_WRITER_INIT;

		jw_array_begin(&jw, 0);
		get_processes(&jw, hSnapshot);
		jw_end(&jw);

		trace2_data_json("process", the_repository, "windows/ancestry",
				 &jw);

		jw_release(&jw);
		CloseHandle(hSnapshot);
	}
}

/*
 * Is a debugger attached to the current process?
 *
 * This will catch debug runs (where the debugger started the process).
 * This is the normal case.  Since this code is called during our startup,
 * it will not report instances where a debugger is attached dynamically
 * to a running git process, but that is relatively rare.
 */
static void get_is_being_debugged(void)
{
	if (IsDebuggerPresent())
		trace2_data_intmax("process", the_repository,
				   "windows/debugger_present", 1);
}

void trace2_collect_process_info(void)
{
	if (!trace2_is_enabled())
		return;

	get_is_being_debugged();
	get_ancestry();
}
