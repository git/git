#ifndef EXIT_PROCESS_H
#define EXIT_PROCESS_H

/*
 * This file contains functions to terminate a Win32 process, as gently as
 * possible.
 *
 * At first, we will attempt to inject a thread that calls ExitProcess(). If
 * that fails, we will fall back to terminating the entire process tree.
 *
 * For simplicity, these functions are marked as file-local.
 */

#include <winternl.h>
#include "lazyload.h"

/* The common record prefix; the Microsoft SDK hides some of these fields. */
struct process_snapshot {
	ULONG next_entry_offset, number_of_threads;
	LARGE_INTEGER reserved[3];
	FILETIME creation_time;
	LARGE_INTEGER user_time, kernel_time;
	UNICODE_STRING image_name;
	LONG base_priority;
	ULONG_PTR pid, parent_pid;
};

static struct process_snapshot *get_process_snapshot(FILETIME *snapshot_time)
{
	char *buffer = NULL;
	size_t size = 0, base;
	ULONG needed;
	NTSTATUS status;
	DECLARE_PROC_ADDR(ntdll.dll, NTSTATUS, NTAPI, NtQuerySystemInformation,
			 SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);

	(void)BUILD_ASSERT_OR_ZERO(offsetof(struct process_snapshot, pid) ==
		offsetof(SYSTEM_PROCESS_INFORMATION, UniqueProcessId));
	(void)BUILD_ASSERT_OR_ZERO(offsetof(struct process_snapshot, parent_pid) +
		sizeof(ULONG_PTR) == offsetof(SYSTEM_PROCESS_INFORMATION, HandleCount));
	if (!INIT_PROC_ADDR(NtQuerySystemInformation)) {
		warning_errno("could not load NtQuerySystemInformation");
		return NULL;
	}
	for (;;) {
		needed = 0;
		GetSystemTimeAsFileTime(snapshot_time);
		status = NtQuerySystemInformation(SystemProcessInformation,
						 buffer, (ULONG)size, &needed);
		if (status >= 0)
			return (void *)buffer;
		/* STATUS_INFO_LENGTH_MISMATCH requests a larger buffer. */
		if (status != (NTSTATUS)0xc0000004) {
			warning("could not enumerate processes (NTSTATUS %#lx)",
				(unsigned long)status);
			break;
		}
		base = needed > size ? needed : size;
		size = st_add(base, base / 4 + 1);
		if (!needed || size > ULONG_MAX) {
			warning("could not size the process snapshot buffer");
			break;
		}
		buffer = xrealloc(buffer, size);
	}
	free(buffer);
	return NULL;
}

static const struct process_snapshot *next_process_snapshot(
	const struct process_snapshot *entry)
{
	return entry->next_entry_offset ?
		(const void *)((const char *)entry + entry->next_entry_offset) :
		NULL;
}

static int process_started_at(HANDLE process, const FILETIME *expected_time)
{
	FILETIME creation_time, exit_time, kernel_time, user_time;

	return GetProcessTimes(process, &creation_time, &exit_time,
			       &kernel_time, &user_time) &&
		!CompareFileTime(&creation_time, expected_time);
}

static int process_is_child(const struct process_snapshot *entry,
			    DWORD parent_pid, const FILETIME *parent_time,
			    const FILETIME *snapshot_time)
{
	return entry->parent_pid == parent_pid &&
		CompareFileTime(&entry->creation_time, parent_time) >= 0 &&
		CompareFileTime(&entry->creation_time, snapshot_time) < 0;
}

/*
 * Terminates the process corresponding to the process ID and all of its
 * directly and indirectly spawned subprocesses.
 *
 * This way of terminating the processes is not gentle: the processes get
 * no chance of cleaning up after themselves (closing file handles, removing
 * .lock files, terminating spawned processes (if any), etc).
 */
static int terminate_process_tree(HANDLE main_process, int exit_status)
{
	struct process_snapshot *snapshot = NULL;
	const struct process_snapshot *entry;
	FILETIME snapshot_time, exit_time, kernel_time, user_time;
	struct {
		DWORD pid;
		FILETIME creation_time;
	} *processes = NULL;
	int max_len = 16384, i, len = 1, alloc = 0, ret = 0;

	ALLOC_GROW(processes, len, alloc);
	processes[0].pid = GetProcessId(main_process);
	if (!processes[0].pid ||
	    !GetProcessTimes(main_process, &processes[0].creation_time,
			     &exit_time, &kernel_time, &user_time)) {
		warning("could not query the root process (Windows error %lu); "
			"terminating only that process", GetLastError());
		goto cleanup;
	}
	snapshot = get_process_snapshot(&snapshot_time);
	if (!snapshot)
		goto cleanup;

	/*
	 * Do not assume that the snapshot lists parents before their children.
	 * Repeat until no further descendants are found.
	 */
	for (;;) {
		int orig_len = len;

		for (entry = snapshot; entry && len < max_len;
		     entry = next_process_snapshot(entry)) {
			for (i = len - 1; i >= 0; i--) {
				if (processes[i].pid == entry->pid)
					break;
				if (!process_is_child(entry, processes[i].pid,
						      &processes[i].creation_time,
						      &snapshot_time))
					continue;
				ALLOC_GROW(processes, len + 1, alloc);
				processes[len].pid = (DWORD)entry->pid;
				processes[len++].creation_time = entry->creation_time;
				break;
			}
		}

		if (orig_len == len || len >= max_len)
			break;
	}

cleanup:
	free(snapshot);
	for (i = len - 1; i > 0; i--) {
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
					     PROCESS_TERMINATE, FALSE, processes[i].pid);

		if (process) {
			/* A reopened PID must still identify the recorded process. */
			if (process_started_at(process, &processes[i].creation_time) &&
			    !TerminateProcess(process, exit_status))
				ret = -1;
			CloseHandle(process);
		}
	}
	if (!TerminateProcess(main_process, exit_status))
		ret = -1;
	CloseHandle(main_process);
	free(processes);

	return ret;
}

/**
 * Determine whether a process runs in the same architecture as the current
 * one. That test is required before we assume that GetProcAddress() returns
 * a valid address *for the target process*.
 */
static inline int process_architecture_matches_current(HANDLE process)
{
	static BOOL current_is_wow = -1;
	BOOL is_wow;

	if (current_is_wow == -1 &&
	    !IsWow64Process (GetCurrentProcess(), &current_is_wow))
		current_is_wow = -2;
	if (current_is_wow == -2)
		return 0; /* could not determine current process' WoW-ness */
	if (!IsWow64Process (process, &is_wow))
		return 0; /* cannot determine */
	return is_wow == current_is_wow;
}

/**
 * Inject a thread into the given process that runs ExitProcess().
 *
 * Note: as kernel32.dll is loaded before any process, the other process and
 * this process will have ExitProcess() at the same address.
 *
 * This function expects the process handle to have the access rights for
 * CreateRemoteThread(): PROCESS_CREATE_THREAD, PROCESS_QUERY_INFORMATION,
 * PROCESS_VM_OPERATION, PROCESS_VM_WRITE, and PROCESS_VM_READ.
 *
 * The idea comes from the Dr Dobb's article "A Safer Alternative to
 * TerminateProcess()" by Andrew Tucker (July 1, 1999),
 * http://www.drdobbs.com/a-safer-alternative-to-terminateprocess/184416547
 *
 * If this method fails, we fall back to running terminate_process_tree().
 */
static int exit_process(HANDLE process, int exit_code)
{
	DWORD code;

	if (GetExitCodeProcess(process, &code) && code == STILL_ACTIVE) {
		static int initialized;
		static LPTHREAD_START_ROUTINE exit_process_address;
		PVOID arg = (PVOID)(intptr_t)exit_code;
		DWORD thread_id;
		HANDLE thread = NULL;

		if (!initialized) {
			HINSTANCE kernel32 = GetModuleHandleA("kernel32");
			if (!kernel32)
				die("BUG: cannot find kernel32");
			exit_process_address =
				(LPTHREAD_START_ROUTINE)(void (*)(void))
				GetProcAddress(kernel32, "ExitProcess");
			initialized = 1;
		}
		if (!exit_process_address ||
		    !process_architecture_matches_current(process))
			return terminate_process_tree(process, exit_code);

		thread = CreateRemoteThread(process, NULL, 0,
					    exit_process_address,
					    arg, 0, &thread_id);
		if (thread) {
			CloseHandle(thread);
			/*
			 * If the process survives for 10 seconds (a completely
			 * arbitrary value picked from thin air), fall back to
			 * killing the process tree via TerminateProcess().
			 */
			if (WaitForSingleObject(process, 10000) ==
			    WAIT_OBJECT_0) {
				CloseHandle(process);
				return 0;
			}
		}

		return terminate_process_tree(process, exit_code);
	}

	CloseHandle(process);
	return 0;
}

#endif
