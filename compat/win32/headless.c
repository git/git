/*
 * headless Git - run Git without opening a console window on Windows
 */

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#pragma GCC diagnostic ignored "-Wunused-parameter"

/*
 * If `dir` contains the path to a Git exec directory, extend `PATH` to
 * include the corresponding `bin/` directory (which is where all those
 * `.dll` files needed by `git.exe` are, on Windows).
 */
static int extend_path(wchar_t *dir, size_t dir_len)
{
	const wchar_t *suffix = L"\\libexec\\git-core";
	size_t suffix_len = wcslen(suffix);
	wchar_t *env;
	DWORD len;

	if (dir_len < suffix_len)
		return 0;

	dir_len -= suffix_len;
	if (memcmp(dir + dir_len, suffix, suffix_len * sizeof(wchar_t)))
		return 0;

	len = GetEnvironmentVariableW(L"PATH", NULL, 0);
	if (!len)
		return 0;

	env = _alloca((dir_len + 5 + len) * sizeof(wchar_t));
	wcsncpy(env, dir, dir_len);
	wcscpy(env + dir_len, L"\\bin;");
	if (!GetEnvironmentVariableW(L"PATH", env + dir_len + 5, len))
		return 0;

	SetEnvironmentVariableW(L"PATH", env);
	return 1;
}

int WINAPI wWinMain(_In_ HINSTANCE instance,
		    _In_opt_ HINSTANCE previous_instance,
		    _In_ LPWSTR command_line, _In_ int show)
{
	wchar_t git_command_line[32768];
	size_t size = sizeof(git_command_line) / sizeof(wchar_t);
	const wchar_t *needs_quotes = L"";
	size_t slash = 0;
	int len;

	STARTUPINFO startup_info = {
		.cb = sizeof(STARTUPINFO),
		.dwFlags = STARTF_USESHOWWINDOW,
		.wShowWindow = SW_HIDE,
	};
	PROCESS_INFORMATION process_info = { 0 };
	DWORD creation_flags = CREATE_UNICODE_ENVIRONMENT |
		CREATE_NEW_CONSOLE | CREATE_NO_WINDOW;
	DWORD exit_code;

	/* First, determine the full path of argv[0] */
	for (size_t i = 0; _wpgmptr[i]; i++)
		if (_wpgmptr[i] == L' ')
			needs_quotes = L"\"";
		else if (_wpgmptr[i] == L'\\')
			slash = i;

	if (slash >= size - 11)
		return 127; /* Too long path */

	/* If it is in Git's exec path, add the bin/ directory to the PATH */
	extend_path(_wpgmptr, slash);

	/* Then, add the full path of `git.exe` as argv[0] */
	len = swprintf_s(git_command_line, size, L"%ls%.*ls\\git.exe%ls",
			 needs_quotes, (int) slash, _wpgmptr, needs_quotes);
	if (len < 0)
		return 127; /* Too long path */

	if (*command_line) {
		/* Now, append the command-line arguments */
		len = swprintf_s(git_command_line + len, size - len,
				 L" %ls", command_line);
		if (len < 0)
			return 127;
	}

	startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup_info.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	if (!CreateProcess(NULL, /* infer argv[0] from the command line */
			   git_command_line, /* modified command line */
			   NULL, /* inherit process handles? */
			   NULL, /* inherit thread handles? */
			   FALSE, /* handles inheritable? */
			   creation_flags,
			   NULL, /* use this process' environment */
			   NULL, /* use this process' working directory */
			   &startup_info, &process_info))
		return 129; /* could not start */
	WaitForSingleObject(process_info.hProcess, INFINITE);
	if (!GetExitCodeProcess(process_info.hProcess, &exit_code))
		exit_code = 130; /* Could not determine exit code? */

	CloseHandle(process_info.hProcess);
	CloseHandle(process_info.hThread);

	return (int)exit_code;
}
