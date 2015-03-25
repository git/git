/*
 * git-wrapper - replace cmd\git.cmd with an executable
 *
 * Copyright (C) 2012 Pat Thoyts <patthoyts@users.sourceforge.net>
 */

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <stdio.h>
#include <wchar.h>

static WCHAR msystem_bin[64];

static void print_error(LPCWSTR prefix, DWORD error_number)
{
	LPWSTR buffer = NULL;
	DWORD count = 0;

	count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, error_number, LANG_NEUTRAL,
			(LPTSTR)&buffer, 0, NULL);
	if (count < 1)
		count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER
				| FORMAT_MESSAGE_FROM_STRING
				| FORMAT_MESSAGE_ARGUMENT_ARRAY,
				L"Code 0x%1!08x!",
				0, LANG_NEUTRAL, (LPTSTR)&buffer, 0,
				(va_list*)&error_number);
	fwprintf(stderr, L"%s: %s", prefix, buffer);
	LocalFree((HLOCAL)buffer);
}

static void setup_environment(LPWSTR exepath)
{
	WCHAR msystem[64];
	LPWSTR path2 = NULL;
	int len;

	/* Set MSYSTEM */
	swprintf(msystem, sizeof(msystem),
		L"MINGW%d", (int) sizeof(void *) * 8);
	SetEnvironmentVariable(L"MSYSTEM", msystem);

	/* if not set, set TERM to cygwin */
	if (!GetEnvironmentVariable(L"TERM", NULL, 0))
		SetEnvironmentVariable(L"TERM", L"cygwin");

	/* if not set, set PLINK_PROTOCOL to ssh */
	if (!GetEnvironmentVariable(L"PLINK_PROTOCOL", NULL, 0))
		SetEnvironmentVariable(L"PLINK_PROTOCOL", L"ssh");

	/* set HOME to %HOMEDRIVE%%HOMEPATH% or %USERPROFILE%
	 * With roaming profiles: HOMEPATH is the roaming location and
	 * USERPROFILE is the local location
	 */
	if (!GetEnvironmentVariable(L"HOME", NULL, 0)) {
		LPWSTR e = NULL;
		len = GetEnvironmentVariable(L"HOMEPATH", NULL, 0);
		if (len == 0) {
			len = GetEnvironmentVariable(L"USERPROFILE", NULL, 0);
			if (len != 0) {
				e = (LPWSTR)malloc(len * sizeof(WCHAR));
				GetEnvironmentVariable(L"USERPROFILE", e, len);
				SetEnvironmentVariable(L"HOME", e);
				free(e);
			}
		}
		else {
			int n;
			len += GetEnvironmentVariable(L"HOMEDRIVE", NULL, 0);
			e = (LPWSTR)malloc(sizeof(WCHAR) * (len + 2));
			n = GetEnvironmentVariable(L"HOMEDRIVE", e, len);
			GetEnvironmentVariable(L"HOMEPATH", &e[n], len-n);
			SetEnvironmentVariable(L"HOME", e);
			free(e);
		}
	}

	/* extend the PATH */
	len = GetEnvironmentVariable(L"PATH", NULL, 0);
	len = sizeof(WCHAR) * (len + 2 * MAX_PATH);
	path2 = (LPWSTR)malloc(len);
	wcscpy(path2, exepath);
	PathAppend(path2, msystem_bin);
	if (_waccess(path2, 0) != -1) {
		/* We are in an MSys2-based setup */
		wcscat(path2, L";");
		wcscat(path2, exepath);
		PathAppend(path2, L"usr\\bin;");
	}
	else {
		/* Fall back to MSys1 paths */
		wcscpy(path2, exepath);
		PathAppend(path2, L"bin;");
		wcscat(path2, exepath);
		PathAppend(path2, L"mingw\\bin;");
	}
	GetEnvironmentVariable(L"PATH", path2 + wcslen(path2),
				(len / sizeof(WCHAR)) - wcslen(path2));
	SetEnvironmentVariable(L"PATH", path2);
	free(path2);

}

/*
 * Fix up the command line to call git.exe
 * We have to be very careful about quoting here so we just
 * trim off the first argument and replace it leaving the rest
 * untouched.
 */
static LPWSTR fixup_commandline(LPWSTR exepath, LPWSTR *exep, int *wait,
	LPWSTR prefix_args, int prefix_args_len, int is_git_command)
{
	int wargc = 0, gui = 0;
	LPWSTR cmd = NULL, cmdline = NULL;
	LPWSTR *wargv = NULL, p = NULL;

	cmdline = GetCommandLine();
	wargv = CommandLineToArgvW(cmdline, &wargc);
	cmd = (LPWSTR)malloc(sizeof(WCHAR) *
		(wcslen(cmdline) + prefix_args_len + 1 + MAX_PATH));
	if (wargc > 1 && wcsicmp(L"gui", wargv[1]) == 0) {
		*wait = 0;
		if (wargc > 2 && wcsicmp(L"citool", wargv[2]) == 0) {
			*wait = 1;
			wcscpy(cmd, L"git.exe");
		}
		else {
			WCHAR script[MAX_PATH];
			gui = 1;
			wcscpy(script, exepath);
			PathAppend(script,
				L"libexec\\git-core\\git-gui");
			PathQuoteSpaces(script);
			wcscpy(cmd, L"wish.exe ");
			wcscat(cmd, script);
			wcscat(cmd, L" --");
			/* find the module from the commandline */
			*exep = NULL;
		}
	}
	else if (prefix_args) {
		if (is_git_command)
			_swprintf(cmd, L"%s\\%s %.*s", exepath, L"git.exe",
					prefix_args_len, prefix_args);
		else
			_swprintf(cmd, L"%.*s", prefix_args_len, prefix_args);

	}
	else
		wcscpy(cmd, L"git.exe");

	/* append all after first space after the initial parameter */
	p = wcschr(&cmdline[wcslen(wargv[0])], L' ');
	if (p && *p) {
		/* for git gui subcommands, remove the 'gui' word */
		if (gui) {
			while (*p == L' ') ++p;
			p = wcschr(p, L' ');
		}
		if (p && *p)
			wcscat(cmd, p);
	}
	LocalFree(wargv);

	return cmd;
}

int main(void)
{
	int r = 1, wait = 1, prefix_args_len = -1, needs_env_setup = 1,
		is_git_command = 1, start_in_home = 0;
	WCHAR exepath[MAX_PATH], exe[MAX_PATH];
	LPWSTR cmd = NULL, dir = NULL, exep = exe, prefix_args = NULL, basename;
	UINT codepage = 0;

	/* Determine MSys2-based Git path. */
	swprintf(msystem_bin, sizeof(msystem_bin),
		L"mingw%d\\bin", (int) sizeof(void *) * 8);

	/* get the installation location */
	GetModuleFileName(NULL, exepath, MAX_PATH);
	if (!PathRemoveFileSpec(exepath)) {
		fwprintf(stderr, L"Invalid executable path: %s\n", exepath);
		ExitProcess(1);
	}
	basename = exepath + wcslen(exepath) + 1;
	if (!wcsncmp(basename, L"git-", 4)) {
		needs_env_setup = 0;

		/* Call a builtin */
		prefix_args = basename + 4;
		prefix_args_len = wcslen(prefix_args);
		if (!wcscmp(prefix_args + prefix_args_len - 4, L".exe"))
			prefix_args_len -= 4;

		/* set the default exe module */
		wcscpy(exe, exepath);
		PathAppend(exe, L"git.exe");
	}
	else if (!wcscmp(basename, L"git.exe")) {
		if (!PathRemoveFileSpec(exepath)) {
			fwprintf(stderr,
				L"Invalid executable path: %s\n", exepath);
			ExitProcess(1);
		}

		/* set the default exe module */
		wcscpy(exe, exepath);
		PathAppend(exe, msystem_bin);
		PathAppend(exe, L"git.exe");
		if (_waccess(exe, 0) == -1) {
			wcscpy(exe, exepath);
			PathAppend(exe, L"bin\\git.exe");
		}
	}

	if (needs_env_setup)
		setup_environment(exepath);
	cmd = fixup_commandline(exepath, &exep, &wait,
		prefix_args, prefix_args_len, is_git_command);

	if (start_in_home) {
		int len = GetEnvironmentVariable(L"HOME", NULL, 0);

		if (len) {
			dir = malloc(sizeof(WCHAR) * len);
			GetEnvironmentVariable(L"HOME", dir, len);
		}
	}

	/* set the console to ANSI/GUI codepage */
	codepage = GetConsoleCP();
	SetConsoleCP(GetACP());

	{
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		BOOL br = FALSE;
		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&si, sizeof(STARTUPINFO));
		si.cb = sizeof(STARTUPINFO);
		br = CreateProcess(/* module: null means use command line */
				exep,
				cmd,  /* modified command line */
				NULL, /* process handle inheritance */
				NULL, /* thread handle inheritance */
				TRUE, /* handles inheritable? */
				CREATE_UNICODE_ENVIRONMENT,
				NULL, /* environment: use parent */
				dir, /* starting directory: use parent */
				&si, &pi);
		if (br) {
			if (wait)
				WaitForSingleObject(pi.hProcess, INFINITE);
			if (!GetExitCodeProcess(pi.hProcess, (DWORD *)&r))
				print_error(L"error reading exit code",
					GetLastError());
			CloseHandle(pi.hProcess);
		}
		else {
			print_error(L"error launching git", GetLastError());
			r = 1;
		}
	}

	free(cmd);

	/* reset the console codepage */
	SetConsoleCP(codepage);
	ExitProcess(r);
}
