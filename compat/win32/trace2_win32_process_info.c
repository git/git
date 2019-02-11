#include "../../cache.h"
#include "../../json-writer.h"
#include <Psapi.h>
#include <tlHelp32.h>

#define NR_PIDS_LIMIT 42

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
 * Accumulate JSON array of our parent processes:
 *     [
 *         exe-name-parent,
 *         exe-name-grand-parent,
 *         ...
 *     ]
 *
 * We artificially limit this to NR_PIDS_LIMIT to quickly guard against cycles
 * in the parent PIDs without a lot of fuss and because we just want some
 * context and don't need an absolute answer.
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
	DWORD pid_list[NR_PIDS_LIMIT];
	int k, nr_pids = 0;

	pid = GetCurrentProcessId();
	while (find_pid(pid, hSnapshot, &pe32)) {
		/* Only report parents. Omit self from the JSON output. */
		if (nr_pids)
			jw_array_string(jw, pe32.szExeFile);

		/* Check for cycle in snapshot. (Yes, it happened.) */
		for (k = 0; k < nr_pids; k++)
			if (pid == pid_list[k]) {
				jw_array_string(jw, "(cycle)");
				return;
			}

		if (nr_pids == NR_PIDS_LIMIT) {
			jw_array_string(jw, "(truncated)");
			return;
		}

		pid_list[nr_pids++] = pid;

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
