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

static void setup_environment(LPWSTR exepath, int full_path)
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
	if (!full_path)
		PathAppend(path2, L"cmd;");
	else {
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
	LPWSTR prefix_args, int prefix_args_len, int is_git_command,
	int skip_arguments)
{
	int wargc = 0;
	LPWSTR cmd = NULL, cmdline = NULL;
	LPWSTR *wargv = NULL, p = NULL;

	cmdline = GetCommandLine();
	wargv = CommandLineToArgvW(cmdline, &wargc);
	cmd = (LPWSTR)malloc(sizeof(WCHAR) *
		(wcslen(cmdline) + prefix_args_len + 1 + MAX_PATH));
	if (prefix_args) {
		if (is_git_command)
			_swprintf(cmd, L"\"%s\\%s\" %.*s", exepath, L"git.exe",
					prefix_args_len, prefix_args);
		else
			_swprintf(cmd, L"%.*s", prefix_args_len, prefix_args);

	}
	else
		wcscpy(cmd, L"git.exe");

	/* skip wargv[0], append the remaining arguments */
	++skip_arguments;
	if (skip_arguments < wargc) {
		int i;
		for (i = 0, p = cmdline; p && *p && i < skip_arguments; i++) {
			if (i)
				while (isspace(*p))
					p++;
			if (*p == L'"')
				p++;
			p += wcslen(wargv[i]);
			if (*p == L'"')
				p++;
			while (*p && !isspace(*p))
				p++;
		}
		wcscat(cmd, p);
	}
	LocalFree(wargv);

	return cmd;
}

static int configure_via_resource(LPWSTR basename, LPWSTR exepath, LPWSTR exep,
	LPWSTR *prefix_args, int *prefix_args_len,
	int *is_git_command, LPWSTR *working_directory, int *full_path,
	int *skip_arguments)
{
	int id, minimal_search_path, wargc;
	LPWSTR *wargv;

#define BUFSIZE 65536
	static WCHAR buf[BUFSIZE];
	int len;

	for (id = 0; ; id++) {
		minimal_search_path = 0;
		len = LoadString(NULL, id, buf, BUFSIZE);

		if (!len) {
			if (!id)
				return 0; /* no resources found */

			fwprintf(stderr, L"Need a valid command-line; "
				L"Edit the string resources accordingly\n");
			exit(1);
		}

		if (len >= BUFSIZE) {
			fwprintf(stderr,
				L"Could not read resource (too large)\n");
			exit(1);
		}

		if (!wcsncmp(L"MINIMAL_PATH=1 ", buf, 15)) {
			minimal_search_path = 1;
			memmove(buf, buf + 15,
				sizeof(WCHAR) * (wcslen(buf + 15) + 1));
		}

		buf[len] = L'\0';

		if (!id)
			SetEnvironmentVariable(L"EXEPATH", exepath);

		for (;;) {
			LPWSTR atat = wcsstr(buf, L"@@"), atat2;
			WCHAR save;
			int env_len, delta;

			if (!atat)
				break;

			atat2 = wcsstr(atat + 2, L"@@");
			if (!atat2)
				break;

			*atat2 = L'\0';
			env_len = GetEnvironmentVariable(atat + 2, NULL, 0);
			delta = env_len - 1 - (atat2 + 2 - atat);
			if (len + delta >= BUFSIZE) {
				fwprintf(stderr,
					L"Substituting '%s' results in too "
					L"large a command-line\n", atat + 2);
				exit(1);
			}
			if (delta)
				memmove(atat2 + 2 + delta, atat2 + 2,
					sizeof(WCHAR) * (len + 1
						- (atat2 + 2 - buf)));
			len += delta;
			save = atat[env_len - 1];
			GetEnvironmentVariable(atat + 2, atat, env_len);
			atat[env_len - 1] = save;
		}

		/* parse first argument */
		wargv = CommandLineToArgvW(buf, &wargc);
		if (wargc < 1) {
			fwprintf(stderr, L"Invalid command-line: '%s'\n", buf);
			exit(1);
		}
		if (*wargv[0] == L'\\' ||
				(isalpha(*wargv[0]) && wargv[0][1] == L':'))
			wcscpy(exep, wargv[0]);
		else {
			wcscpy(exep, exepath);
			PathAppend(exep, wargv[0]);
		}
		LocalFree(wargv);

		if (_waccess(exep, 0) != -1)
			break;
		fwprintf(stderr,
			L"Skipping command-line '%s'\n('%s' not found)\n",
			buf, exep);
	}

	*prefix_args = buf;
	*prefix_args_len = wcslen(buf);

	*is_git_command = 0;
	*working_directory = (LPWSTR) 1;
	wargv = CommandLineToArgvW(GetCommandLine(), &wargc);
	if (wargc > 1) {
		if (!wcscmp(L"--no-cd", wargv[1])) {
			*working_directory = NULL;
			*skip_arguments = 1;
		}
		else if (!wcsncmp(L"--cd=", wargv[1], 5)) {
			*working_directory = wcsdup(wargv[1] + 5);
			*skip_arguments = 1;
		}
	}
	if (minimal_search_path)
		*full_path = 0;
	LocalFree(wargv);

	return 1;
}

int main(void)
{
	int r = 1, wait = 1, prefix_args_len = -1, needs_env_setup = 1,
		is_git_command = 1, full_path = 1, skip_arguments = 0;
	WCHAR exepath[MAX_PATH], exe[MAX_PATH];
	LPWSTR cmd = NULL, exep = exe, prefix_args = NULL, basename;
	LPWSTR working_directory = NULL;
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
	if (configure_via_resource(basename, exepath, exep,
			&prefix_args, &prefix_args_len,
			&is_git_command, &working_directory,
			&full_path, &skip_arguments)) {
		/* do nothing */
	}
	else if (!wcscmp(basename, L"git-gui.exe")) {
		static WCHAR buffer[BUFSIZE];
		if (!PathRemoveFileSpec(exepath)) {
			fwprintf(stderr,
				L"Invalid executable path: %s\n", exepath);
			ExitProcess(1);
		}

		/* set the default exe module */
		wcscpy(exe, exepath);
		PathAppend(exe, msystem_bin);
		PathAppend(exe, L"wish.exe");
		if (_waccess(exe, 0) != -1)
			swprintf(buffer, BUFSIZE,
				L"\"%s\\%.*s\\libexec\\git-core\"",
				exepath, wcslen(msystem_bin) - 4, msystem_bin);
		else {
			wcscpy(exe, exepath);
			PathAppend(exe, L"mingw\\bin\\wish.exe");
			swprintf(buffer, BUFSIZE,
				L"\"%s\\mingw\\libexec\\git-core\"", exepath);
		}
		PathAppend(buffer, L"git-gui");
		prefix_args = buffer;
		prefix_args_len = wcslen(buffer);
	}
	else if (!wcsncmp(basename, L"git-", 4)) {
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
	else if (!wcscmp(basename, L"gitk.exe")) {
		static WCHAR buffer[BUFSIZE];
		if (!PathRemoveFileSpec(exepath)) {
			fwprintf(stderr,
				L"Invalid executable path: %s\n", exepath);
			ExitProcess(1);
		}

		/* set the default exe module */
		wcscpy(exe, exepath);
		swprintf(buffer, BUFSIZE, L"\"%s\"", exepath);
		PathAppend(exe, msystem_bin);
		PathAppend(exe, L"wish.exe");
		if (_waccess(exe, 0) != -1)
			PathAppend(buffer, msystem_bin);
		else {
			wcscpy(exe, exepath);
			PathAppend(exe, L"mingw\\bin\\wish.exe");
			PathAppend(buffer, L"mingw\\bin");
		}
		PathAppend(buffer, L"gitk");
		prefix_args = buffer;
		prefix_args_len = wcslen(buffer);
	}

	if (needs_env_setup)
		setup_environment(exepath, full_path);
	cmd = fixup_commandline(exepath, &exep, &wait,
		prefix_args, prefix_args_len, is_git_command, skip_arguments);

	if (working_directory == (LPWSTR)1) {
		int len = GetEnvironmentVariable(L"HOME", NULL, 0);

		if (len) {
			working_directory = malloc(sizeof(WCHAR) * len);
			GetEnvironmentVariable(L"HOME", working_directory, len);
		}
	}

	/* set the console to ANSI/GUI codepage */
	codepage = GetConsoleCP();
	SetConsoleCP(GetACP());

	{
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		DWORD creation_flags = CREATE_UNICODE_ENVIRONMENT;
		HANDLE console_handle;
		BOOL br = FALSE;
		ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
		ZeroMemory(&si, sizeof(STARTUPINFO));
		si.cb = sizeof(STARTUPINFO);

		console_handle = CreateFile(L"CONOUT$", GENERIC_WRITE,
				FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL, NULL);
		if (console_handle != INVALID_HANDLE_VALUE)
			CloseHandle(console_handle);
		else {
			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
			si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

			creation_flags |= CREATE_NO_WINDOW;
		}
		br = CreateProcess(/* module: null means use command line */
				exep,
				cmd,  /* modified command line */
				NULL, /* process handle inheritance */
				NULL, /* thread handle inheritance */
				TRUE, /* handles inheritable? */
				creation_flags,
				NULL, /* environment: use parent */
				working_directory, /* use parent's */
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
