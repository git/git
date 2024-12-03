#define USE_THE_REPOSITORY_VARIABLE

#include "../../git-compat-util.h"
#include "../../json-writer.h"
#include "../../repository.h"
#include "../../trace2.h"
#include "lazyload.h"
#include <psapi.h>
#include <tlhelp32.h>

/*
 * An arbitrarily chosen value to limit the size of the ancestor
 * array built in git_processes().
 */
#define NR_PIDS_LIMIT 10

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
 * Note: we only report the filename of the process executable; the
 *       only way to get its full pathname is to use OpenProcess()
 *       and GetModuleFileNameEx() or QueryfullProcessImageName()
 *       and that seems rather expensive (on top of the cost of
 *       getting the snapshot).
 *
 * Note: we compute the set of parent processes by walking the PPID
 *       link in each visited PROCESSENTRY32 record.  This search
 *       stops when an ancestor process is not found in the snapshot
 *       (because it exited before the current or intermediate parent
 *       process exited).
 *
 *       This search may compute an incorrect result if the PPID link
 *       refers to the PID of an exited parent and that PID has been
 *       recycled and given to a new unrelated process.
 *
 *       Worse, it is possible for a child or descendant of the
 *       current process to be given the recycled PID and cause a
 *       PPID-cycle.  This would cause an infinite loop building our
 *       parent process array.
 *
 * Note: for completeness, the "System Idle" process has PID=0 and
 *       PPID=0 and could cause another PPID-cycle.  We don't expect
 *       Git to be a descendant of the idle process, but because of
 *       PID recycling, it might be possible to get a PPID link value
 *       of 0.  This too would cause an infinite loop.
 *
 * Therefore, we keep an array of the visited PPIDs to guard against
 * cycles.
 *
 * We use a fixed-size array rather than ALLOC_GROW to keep things
 * simple and avoid the alloc/realloc overhead.  It is OK if we
 * truncate the search and return a partial answer.
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

/*
 * Emit JSON data with the peak memory usage of the current process.
 */
static void get_peak_memory_info(void)
{
	DECLARE_PROC_ADDR(psapi.dll, BOOL, WINAPI, GetProcessMemoryInfo,
			  HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);

	if (INIT_PROC_ADDR(GetProcessMemoryInfo)) {
		PROCESS_MEMORY_COUNTERS pmc;

		if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc,
					 sizeof(pmc))) {
			struct json_writer jw = JSON_WRITER_INIT;

			jw_object_begin(&jw, 0);

#define KV(kv) #kv, (intmax_t)pmc.kv

			jw_object_intmax(&jw, KV(PageFaultCount));
			jw_object_intmax(&jw, KV(PeakWorkingSetSize));
			jw_object_intmax(&jw, KV(PeakPagefileUsage));

			jw_end(&jw);

			trace2_data_json("process", the_repository,
					 "windows/memory", &jw);
			jw_release(&jw);
		}
	}
}

void trace2_collect_process_info(enum trace2_process_info_reason reason)
{
	if (!trace2_is_enabled())
		return;

	switch (reason) {
	case TRACE2_PROCESS_INFO_STARTUP:
		get_is_being_debugged();
		get_ancestry();
		return;

	case TRACE2_PROCESS_INFO_EXIT:
		get_peak_memory_info();
		return;

	default:
		BUG("trace2_collect_process_info: unknown reason '%d'", reason);
	}
}
