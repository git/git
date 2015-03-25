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

#ifdef MAGIC_RESOURCE

#include <stdint.h>

#pragma pack(2)
struct resource_directory
{
	int8_t width;
	int8_t height;
	int8_t color_count;
	int8_t reserved;
	int16_t planes;
	int16_t bit_count;
	int32_t bytes_in_resource;
	int16_t id;
};

struct header
{
	int16_t reserved;
	int16_t type;
	int16_t count;
};

struct icon_header
{
	int8_t width;
	int8_t height;
	int8_t color_count;
	int8_t reserved;
	int16_t planes;
	int16_t bit_count;
	int32_t bytes_in_resource;
	int32_t image_offset;
};

struct icon_image
{
	BITMAPINFOHEADER header;
	RGBQUAD colors;
	int8_t xors[1];
	int8_t ands[1];
};

struct icon
{
	int count;
	struct header *header;
	struct resource_directory *items;
	struct icon_image **images;
};

static int parse_ico_file(LPWSTR ico_path, struct icon *result)
{
	struct header file_header;
	FILE *file = _wfopen(ico_path, L"rb");
	int i;

	if (!file) {
		fwprintf(stderr, L"could not open icon file '%s'", ico_path);
		return 1;
	}

	fread(&file_header, sizeof(struct header), 1, file);
	result->count = file_header.count;

	result->header = malloc(sizeof(struct header) + result->count
			* sizeof(struct resource_directory));
	result->header->reserved = 0;
	result->header->type = 1;
	result->header->count = result->count;
	result->items = (struct resource_directory *)(result->header + 1);
	struct icon_header *icon_headers = malloc(result->count
			* sizeof(struct icon_header));
	fread(icon_headers, result->count * sizeof(struct icon_header),
			1, file);
	result->images = malloc(result->count * sizeof(struct icon_image *));

	for (i = 0; i < result->count; i++) {
		struct icon_image** image = result->images + i;
		struct icon_header* icon_header = icon_headers + i;
		struct resource_directory *item = result->items + i;

		*image = malloc(icon_header->bytes_in_resource);
		fseek(file, icon_header->image_offset, SEEK_SET);
		fread(*image, icon_header->bytes_in_resource, 1, file);

		memcpy(item, icon_header, sizeof(struct resource_directory));
		item->id = (int16_t)(i + 1);
	}

	fclose(file);

	return 0;
}

static int wsuffixcmp(LPWSTR text, LPWSTR suffix)
{
	int text_len = wcslen(text), suffix_len = wcslen(suffix);

	if (text_len < suffix_len)
		return -1;

	return wcscmp(text + (text_len - suffix_len), suffix);
}

static int edit_resources(LPWSTR exe_path,
	LPWSTR ico_path, LPWSTR *commands, int command_count)
{
	WORD language = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
	struct icon icon;
	HANDLE handle;
	int i;

	if (command_count > 16) {
		fwprintf(stderr, L"Cannot handle more than 16 commands\n");
		return -1;
	}

	if (wsuffixcmp(exe_path, L".exe")) {
		fwprintf(stderr, L"Not an .exe file: '%s'", exe_path);
		return -1;
	}
	if (_waccess(exe_path, 0) == -1) {
		fwprintf(stderr, L"File not found: '%s'", exe_path);
		return -1;
	}

	if (ico_path) {
		if (wsuffixcmp(ico_path, L".ico")) {
			fwprintf(stderr, L"Not an .ico file: '%s'", ico_path);
			return -1;
		}
		if (_waccess(ico_path, 0) == -1) {
			fwprintf(stderr, L"File not found: '%s'", ico_path);
			return -1;
		}

		if (parse_ico_file(ico_path, &icon))
			return -1;
	}

	handle = BeginUpdateResource(exe_path, FALSE);
	if (!handle) {
		fwprintf(stderr,
			L"Could not update resources of '%s'", exe_path);
		return -1;
	}

	if (ico_path) {
		int id = 1;
		UpdateResource(handle, RT_GROUP_ICON,
				L"MAINICON", language,
				icon.header, sizeof(struct header) + icon.count
					* sizeof(struct resource_directory));
		for (i = 0; i < icon.count; i++) {
			UpdateResource(handle, RT_ICON,
					MAKEINTRESOURCE(id++), language,
					icon.images[i],
					icon.items[i].bytes_in_resource);
		}
	}

	if (command_count >= 0) {
		LPWSTR buffer, p;
		int alloc = 16; /* 16 words with string lengths, for sure... */

		for (i = 0; i < command_count; i++) {
			int len = wcslen(commands[i]);
			if (len > 0xffff) {
				fwprintf(stderr, L"Too long command: %s\n",
					commands[i]);
				return -1;
			}
			alloc += len;
		}

		p = buffer = calloc(alloc, sizeof(WCHAR));

		for (i = 0; i < command_count; i++)
			p += swprintf(p, alloc - (p - buffer), L"%c%s",
				(WCHAR) wcslen(commands[i]), commands[i]);

		UpdateResource(handle, RT_STRING, MAKEINTRESOURCE(1),
			language, buffer, sizeof(WCHAR) * alloc);
	}

	if (EndUpdateResource(handle, FALSE))
		return 0;

	fwprintf(stderr, L"Error %d updating resources\n",
		(int) GetLastError());
	return -1;
}

static int configure_via_resource(LPWSTR basename, LPWSTR exepath, LPWSTR exep,
	LPWSTR *prefix_args, int *prefix_args_len,
	int *is_git_command, int *start_in_home)
{
	int id = 0, wargc;
	LPWSTR *wargv;

#define BUFSIZE 65536
	static WCHAR buf[BUFSIZE];
	int len;

	if (!wcscmp(basename, L"edit-res.exe")) {
		LPWSTR cmdline = GetCommandLine();

		wargv = CommandLineToArgvW(cmdline, &wargc);

		if (wargv[1]) {
			if (wargc == 4 && !wcscmp(wargv[1], L"icon"))
				exit(edit_resources(wargv[2], wargv[3],
					NULL, -1));
			if (wargc > 1 && !wcscmp(wargv[1], L"command"))
				exit(edit_resources(wargv[2], NULL,
					wargv + 3, wargc - 3));
		}
		fwprintf(stderr,
			L"Usage: %s (icon | command) <exe> <args>...\n",
			basename);
		exit(1);
	}

	SetEnvironmentVariable(L"EXEPATH", exepath);
	for (id = 0; ; id++) {
		len = LoadString(NULL, id, buf, BUFSIZE);

		if (!len) {
			fwprintf(stderr, L"Need a valid command-line; "
				L"Copy %s to edit-res.exe and call\n"
				L"\n\tedit-res.exe command %s "
				L"\"<command-line>\"\n",
				basename, basename);
			exit(1);
		}

		if (len >= BUFSIZE) {
			fwprintf(stderr,
				L"Could not read resource (too large)\n");
			exit(1);
		}

		buf[len] = L'\0';

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

		if (_waccess(exep, 0) != -1)
			break;
		fwprintf(stderr,
			L"Skipping command-line '%s'\n('%s' not found)\n",
			buf, exep);
	}

	*prefix_args = buf;
	*prefix_args_len = wcslen(buf);

	*is_git_command = 0;
	*start_in_home = 1;

	return 1;
}

#endif

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
#ifdef MAGIC_RESOURCE
	if (configure_via_resource(basename, exepath, exep,
			&prefix_args, &prefix_args_len,
			&is_git_command, &start_in_home)) {
		/* do nothing */
	}
	else
#endif
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
