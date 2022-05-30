/*
 * Copyright 2008 Peter Harris <git@peter.is-a-geek.org>
 */

#undef NOGDI

#include "../git-compat-util.h"
#include <wingdi.h>
#include <winreg.h>
#include "win32.h"
#include "win32/lazyload.h"

static int fd_is_interactive[3] = { 0, 0, 0 };
#define FD_CONSOLE 0x1
#define FD_SWAPPED 0x2
#define FD_MSYS    0x4

/*
 ANSI codes used by git: m, K

 This file is git-specific. Therefore, this file does not attempt
 to implement any codes that are not used by git.
*/

static HANDLE console;
static WORD plain_attr;
static WORD attr;
static int negative;
static int non_ascii_used = 0;
static HANDLE hthread, hread, hwrite;
static HANDLE hconsole1, hconsole2;

#ifdef __MINGW32__
#if !defined(__MINGW64_VERSION_MAJOR) || __MINGW64_VERSION_MAJOR < 5
typedef struct _CONSOLE_FONT_INFOEX {
	ULONG cbSize;
	DWORD nFont;
	COORD dwFontSize;
	UINT FontFamily;
	UINT FontWeight;
	WCHAR FaceName[LF_FACESIZE];
} CONSOLE_FONT_INFOEX, *PCONSOLE_FONT_INFOEX;
#endif
#endif

static void warn_if_raster_font(void)
{
	DWORD fontFamily = 0;
	DECLARE_PROC_ADDR(kernel32.dll, BOOL, WINAPI,
			GetCurrentConsoleFontEx, HANDLE, BOOL,
			PCONSOLE_FONT_INFOEX);

	/* don't bother if output was ascii only */
	if (!non_ascii_used)
		return;

	/* GetCurrentConsoleFontEx is available since Vista */
	if (INIT_PROC_ADDR(GetCurrentConsoleFontEx)) {
		CONSOLE_FONT_INFOEX cfi;
		cfi.cbSize = sizeof(cfi);
		if (GetCurrentConsoleFontEx(console, 0, &cfi))
			fontFamily = cfi.FontFamily;
	} else {
		/* pre-Vista: check default console font in registry */
		HKEY hkey;
		if (ERROR_SUCCESS == RegOpenKeyExA(HKEY_CURRENT_USER, "Console",
				0, KEY_READ, &hkey)) {
			DWORD size = sizeof(fontFamily);
			RegQueryValueExA(hkey, "FontFamily", NULL, NULL,
					(LPVOID) &fontFamily, &size);
			RegCloseKey(hkey);
		}
	}

	if (!(fontFamily & TMPF_TRUETYPE)) {
		const wchar_t *msg = L"\nWarning: Your console font probably "
			L"doesn\'t support Unicode. If you experience strange "
			L"characters in the output, consider switching to a "
			L"TrueType font such as Consolas!\n";
		DWORD dummy;
		WriteConsoleW(console, msg, wcslen(msg), &dummy, NULL);
	}
}

static int is_console(int fd)
{
	CONSOLE_SCREEN_BUFFER_INFO sbi;
	DWORD mode;
	HANDLE hcon;

	static int initialized = 0;

	/* get OS handle of the file descriptor */
	hcon = (HANDLE) _get_osfhandle(fd);
	if (hcon == INVALID_HANDLE_VALUE)
		return 0;

	/* check if its a device (i.e. console, printer, serial port) */
	if (GetFileType(hcon) != FILE_TYPE_CHAR)
		return 0;

	/* check if its a handle to a console output screen buffer */
	if (!fd) {
		if (!GetConsoleMode(hcon, &mode))
			return 0;
		/*
		 * This code path is only reached if there is no console
		 * attached to stdout/stderr, i.e. we will not need to output
		 * any text to any console, therefore we might just as well
		 * use black as foreground color.
		 */
		sbi.wAttributes = 0;
	} else if (!GetConsoleScreenBufferInfo(hcon, &sbi))
		return 0;

	if (fd >= 0 && fd <= 2)
		fd_is_interactive[fd] |= FD_CONSOLE;

	/* initialize attributes */
	if (!initialized) {
		console = hcon;
		attr = plain_attr = sbi.wAttributes;
		negative = 0;
		initialized = 1;
	}

	return 1;
}

#define BUFFER_SIZE 4096
#define MAX_PARAMS 16

static void write_console(unsigned char *str, size_t len)
{
	/* only called from console_thread, so a static buffer will do */
	static wchar_t wbuf[2 * BUFFER_SIZE + 1];
	DWORD dummy;

	/* convert utf-8 to utf-16 */
	int wlen = xutftowcsn(wbuf, (char*) str, ARRAY_SIZE(wbuf), len);
	if (wlen < 0) {
		const wchar_t *err = L"[invalid]";
		WriteConsoleW(console, err, wcslen(err), &dummy, NULL);
		return;
	}

	/* write directly to console */
	WriteConsoleW(console, wbuf, wlen, &dummy, NULL);

	/* remember if non-ascii characters are printed */
	if (wlen != len)
		non_ascii_used = 1;
}

#define FOREGROUND_ALL (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define BACKGROUND_ALL (BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE)

static void set_console_attr(void)
{
	WORD attributes = attr;
	if (negative) {
		attributes &= ~FOREGROUND_ALL;
		attributes &= ~BACKGROUND_ALL;

		/* This could probably use a bitmask
		   instead of a series of ifs */
		if (attr & FOREGROUND_RED)
			attributes |= BACKGROUND_RED;
		if (attr & FOREGROUND_GREEN)
			attributes |= BACKGROUND_GREEN;
		if (attr & FOREGROUND_BLUE)
			attributes |= BACKGROUND_BLUE;

		if (attr & BACKGROUND_RED)
			attributes |= FOREGROUND_RED;
		if (attr & BACKGROUND_GREEN)
			attributes |= FOREGROUND_GREEN;
		if (attr & BACKGROUND_BLUE)
			attributes |= FOREGROUND_BLUE;
	}
	SetConsoleTextAttribute(console, attributes);
}

static void erase_in_line(void)
{
	CONSOLE_SCREEN_BUFFER_INFO sbi;
	DWORD dummy; /* Needed for Windows 7 (or Vista) regression */

	if (!console)
		return;

	GetConsoleScreenBufferInfo(console, &sbi);
	FillConsoleOutputCharacterA(console, ' ',
		sbi.dwSize.X - sbi.dwCursorPosition.X, sbi.dwCursorPosition,
		&dummy);
}

static void set_attr(char func, const int *params, int paramlen)
{
	int i;
	switch (func) {
	case 'm':
		for (i = 0; i < paramlen; i++) {
			switch (params[i]) {
			case 0: /* reset */
				attr = plain_attr;
				negative = 0;
				break;
			case 1: /* bold */
				attr |= FOREGROUND_INTENSITY;
				break;
			case 2:  /* faint */
			case 22: /* normal */
				attr &= ~FOREGROUND_INTENSITY;
				break;
			case 3:  /* italic */
				/* Unsupported */
				break;
			case 4:  /* underline */
			case 21: /* double underline */
				/* Wikipedia says this flag does nothing */
				/* Furthermore, mingw doesn't define this flag
				attr |= COMMON_LVB_UNDERSCORE; */
				break;
			case 24: /* no underline */
				/* attr &= ~COMMON_LVB_UNDERSCORE; */
				break;
			case 5:  /* slow blink */
			case 6:  /* fast blink */
				/* We don't have blink, but we do have
				   background intensity */
				attr |= BACKGROUND_INTENSITY;
				break;
			case 25: /* no blink */
				attr &= ~BACKGROUND_INTENSITY;
				break;
			case 7:  /* negative */
				negative = 1;
				break;
			case 27: /* positive */
				negative = 0;
				break;
			case 8:  /* conceal */
			case 28: /* reveal */
				/* Unsupported */
				break;
			case 30: /* Black */
				attr &= ~FOREGROUND_ALL;
				break;
			case 31: /* Red */
				attr &= ~FOREGROUND_ALL;
				attr |= FOREGROUND_RED;
				break;
			case 32: /* Green */
				attr &= ~FOREGROUND_ALL;
				attr |= FOREGROUND_GREEN;
				break;
			case 33: /* Yellow */
				attr &= ~FOREGROUND_ALL;
				attr |= FOREGROUND_RED | FOREGROUND_GREEN;
				break;
			case 34: /* Blue */
				attr &= ~FOREGROUND_ALL;
				attr |= FOREGROUND_BLUE;
				break;
			case 35: /* Magenta */
				attr &= ~FOREGROUND_ALL;
				attr |= FOREGROUND_RED | FOREGROUND_BLUE;
				break;
			case 36: /* Cyan */
				attr &= ~FOREGROUND_ALL;
				attr |= FOREGROUND_GREEN | FOREGROUND_BLUE;
				break;
			case 37: /* White */
				attr |= FOREGROUND_RED |
					FOREGROUND_GREEN |
					FOREGROUND_BLUE;
				break;
			case 38: /* Unknown */
				break;
			case 39: /* reset */
				attr &= ~FOREGROUND_ALL;
				attr |= (plain_attr & FOREGROUND_ALL);
				break;
			case 40: /* Black */
				attr &= ~BACKGROUND_ALL;
				break;
			case 41: /* Red */
				attr &= ~BACKGROUND_ALL;
				attr |= BACKGROUND_RED;
				break;
			case 42: /* Green */
				attr &= ~BACKGROUND_ALL;
				attr |= BACKGROUND_GREEN;
				break;
			case 43: /* Yellow */
				attr &= ~BACKGROUND_ALL;
				attr |= BACKGROUND_RED | BACKGROUND_GREEN;
				break;
			case 44: /* Blue */
				attr &= ~BACKGROUND_ALL;
				attr |= BACKGROUND_BLUE;
				break;
			case 45: /* Magenta */
				attr &= ~BACKGROUND_ALL;
				attr |= BACKGROUND_RED | BACKGROUND_BLUE;
				break;
			case 46: /* Cyan */
				attr &= ~BACKGROUND_ALL;
				attr |= BACKGROUND_GREEN | BACKGROUND_BLUE;
				break;
			case 47: /* White */
				attr |= BACKGROUND_RED |
					BACKGROUND_GREEN |
					BACKGROUND_BLUE;
				break;
			case 48: /* Unknown */
				break;
			case 49: /* reset */
				attr &= ~BACKGROUND_ALL;
				attr |= (plain_attr & BACKGROUND_ALL);
				break;
			default:
				/* Unsupported code */
				break;
			}
		}
		set_console_attr();
		break;
	case 'K':
		erase_in_line();
		break;
	default:
		/* Unsupported code */
		break;
	}
}

enum {
	TEXT = 0, ESCAPE = 033, BRACKET = '['
};

static DWORD WINAPI console_thread(LPVOID data UNUSED)
{
	unsigned char buffer[BUFFER_SIZE];
	DWORD bytes;
	int start, end = 0, c, parampos = 0, state = TEXT;
	int params[MAX_PARAMS];

	while (1) {
		/* read next chunk of bytes from the pipe */
		if (!ReadFile(hread, buffer + end, BUFFER_SIZE - end, &bytes,
				NULL)) {
			/* exit if pipe has been closed or disconnected */
			if (GetLastError() == ERROR_PIPE_NOT_CONNECTED ||
					GetLastError() == ERROR_BROKEN_PIPE)
				break;
			/* ignore other errors */
			continue;
		}

		/* scan the bytes and handle ANSI control codes */
		bytes += end;
		start = end = 0;
		while (end < bytes) {
			c = buffer[end++];
			switch (state) {
			case TEXT:
				if (c == ESCAPE) {
					/* print text seen so far */
					if (end - 1 > start)
						write_console(buffer + start,
							end - 1 - start);

					/* then start parsing escape sequence */
					start = end - 1;
					memset(params, 0, sizeof(params));
					parampos = 0;
					state = ESCAPE;
				}
				break;

			case ESCAPE:
				/* continue if "\033[", otherwise bail out */
				state = (c == BRACKET) ? BRACKET : TEXT;
				break;

			case BRACKET:
				/* parse [0-9;]* into array of parameters */
				if (c >= '0' && c <= '9') {
					params[parampos] *= 10;
					params[parampos] += c - '0';
				} else if (c == ';') {
					/*
					 * next parameter, bail out if out of
					 * bounds
					 */
					parampos++;
					if (parampos >= MAX_PARAMS)
						state = TEXT;
				} else {
					/*
					 * end of escape sequence, change
					 * console attributes
					 */
					set_attr(c, params, parampos + 1);
					start = end;
					state = TEXT;
				}
				break;
			}
		}

		/* print remaining text unless parsing an escape sequence */
		if (state == TEXT && end > start) {
			/* check for incomplete UTF-8 sequences and fix end */
			if (buffer[end - 1] >= 0x80) {
				if (buffer[end -1] >= 0xc0)
					end--;
				else if (end - 1 > start &&
						buffer[end - 2] >= 0xe0)
					end -= 2;
				else if (end - 2 > start &&
						buffer[end - 3] >= 0xf0)
					end -= 3;
			}

			/* print remaining complete UTF-8 sequences */
			if (end > start)
				write_console(buffer + start, end - start);

			/* move remaining bytes to the front */
			if (end < bytes)
				memmove(buffer, buffer + end, bytes - end);
			end = bytes - end;
		} else {
			/* all data has been consumed, mark buffer empty */
			end = 0;
		}
	}

	/* check if the console font supports unicode */
	warn_if_raster_font();

	CloseHandle(hread);
	return 0;
}

static void winansi_exit(void)
{
	/* flush all streams */
	_flushall();

	/* signal console thread to exit */
	FlushFileBuffers(hwrite);
	DisconnectNamedPipe(hwrite);

	/* wait for console thread to copy remaining data */
	WaitForSingleObject(hthread, INFINITE);

	/* cleanup handles... */
	CloseHandle(hwrite);
	CloseHandle(hthread);
}

static void die_lasterr(const char *fmt, ...)
{
	va_list params;
	va_start(params, fmt);
	errno = err_win_to_posix(GetLastError());
	die_errno(fmt, params);
	va_end(params);
}

#undef dup2
int winansi_dup2(int oldfd, int newfd)
{
	int ret = dup2(oldfd, newfd);

	if (!ret && newfd >= 0 && newfd <= 2)
		fd_is_interactive[newfd] = oldfd < 0 || oldfd > 2 ?
			0 : fd_is_interactive[oldfd];

	return ret;
}

static HANDLE duplicate_handle(HANDLE hnd)
{
	HANDLE hresult, hproc = GetCurrentProcess();
	if (!DuplicateHandle(hproc, hnd, hproc, &hresult, 0, TRUE,
			DUPLICATE_SAME_ACCESS))
		die_lasterr("DuplicateHandle(%li) failed",
			(long) (intptr_t) hnd);
	return hresult;
}

static HANDLE swap_osfhnd(int fd, HANDLE new_handle)
{
	/*
	 * Create a copy of the original handle associated with fd
	 * because the original will get closed when we dup2().
	 */
	HANDLE handle = (HANDLE)_get_osfhandle(fd);
	HANDLE duplicate = duplicate_handle(handle);

	/* Create a temp fd associated with the already open "new_handle". */
	int new_fd = _open_osfhandle((intptr_t)new_handle, O_BINARY);

	assert((fd == 1) || (fd == 2));

	/*
	 * Use stock dup2() to re-bind fd to the new handle.  Note that
	 * this will implicitly close(1) and close both fd=1 and the
	 * originally associated handle.  It will open a new fd=1 and
	 * call DuplicateHandle() on the handle associated with new_fd.
	 * It is because of this implicit close() that we created the
	 * copy of the original.
	 *
	 * Note that we need to update the cached console handle to the
	 * duplicated one because the dup2() call will implicitly close
	 * the original one.
	 *
	 * Note that dup2() when given target := {0,1,2} will also
	 * call SetStdHandle(), so we don't need to worry about that.
	 */
	if (console == handle)
		console = duplicate;
	dup2(new_fd, fd);

	/* Close the temp fd.  This explicitly closes "new_handle"
	 * (because it has been associated with it).
	 */
	close(new_fd);

	if (fd == 2)
		setvbuf(stderr, NULL, _IONBF, BUFSIZ);
	fd_is_interactive[fd] |= FD_SWAPPED;

	return duplicate;
}

#ifdef DETECT_MSYS_TTY

#include <winternl.h>

#if defined(_MSC_VER)

typedef struct _OBJECT_NAME_INFORMATION
{
	UNICODE_STRING Name;
	WCHAR NameBuffer[FLEX_ARRAY];
} OBJECT_NAME_INFORMATION, *POBJECT_NAME_INFORMATION;

#define ObjectNameInformation 1

#else
#include <ntstatus.h>
#endif

static void detect_msys_tty(int fd)
{
	ULONG result;
	BYTE buffer[1024];
	POBJECT_NAME_INFORMATION nameinfo = (POBJECT_NAME_INFORMATION) buffer;
	PWSTR name;

	/* check if fd is a pipe */
	HANDLE h = (HANDLE) _get_osfhandle(fd);
	if (GetFileType(h) != FILE_TYPE_PIPE)
		return;

	/* get pipe name */
	if (!NT_SUCCESS(NtQueryObject(h, ObjectNameInformation,
			buffer, sizeof(buffer) - 2, &result)))
		return;
	if (result < sizeof(*nameinfo) || !nameinfo->Name.Buffer ||
		!nameinfo->Name.Length)
		return;
	name = nameinfo->Name.Buffer;
	name[nameinfo->Name.Length / sizeof(*name)] = 0;

	/*
	 * Check if this could be a MSYS2 pty pipe ('msys-XXXX-ptyN-XX')
	 * or a cygwin pty pipe ('cygwin-XXXX-ptyN-XX')
	 */
	if ((!wcsstr(name, L"msys-") && !wcsstr(name, L"cygwin-")) ||
			!wcsstr(name, L"-pty"))
		return;

	if (fd == 2)
		setvbuf(stderr, NULL, _IONBF, BUFSIZ);
	fd_is_interactive[fd] |= FD_MSYS;
}

#endif

/*
 * Wrapper for isatty().  Most calls in the main git code
 * call isatty(1 or 2) to see if the instance is interactive
 * and should: be colored, show progress, paginate output.
 * We lie and give results for what the descriptor WAS at
 * startup (and ignore any pipe redirection we internally
 * do).
 */
#undef isatty
int winansi_isatty(int fd)
{
	if (fd >= 0 && fd <= 2)
		return fd_is_interactive[fd] != 0;
	return isatty(fd);
}

void winansi_init(void)
{
	int con1, con2;
	wchar_t name[32];

	/* check if either stdout or stderr is a console output screen buffer */
	con1 = is_console(1);
	con2 = is_console(2);

	/* Also compute console bit for fd 0 even though we don't need the result here. */
	is_console(0);

	if (!con1 && !con2) {
#ifdef DETECT_MSYS_TTY
		/* check if stdin / stdout / stderr are MSYS2 pty pipes */
		detect_msys_tty(0);
		detect_msys_tty(1);
		detect_msys_tty(2);
#endif
		return;
	}

	/* create a named pipe to communicate with the console thread */
	if (swprintf(name, ARRAY_SIZE(name) - 1, L"\\\\.\\pipe\\winansi%lu",
		     GetCurrentProcessId()) < 0)
		die("Could not initialize winansi pipe name");
	hwrite = CreateNamedPipeW(name, PIPE_ACCESS_OUTBOUND,
		PIPE_TYPE_BYTE | PIPE_WAIT, 1, BUFFER_SIZE, 0, 0, NULL);
	if (hwrite == INVALID_HANDLE_VALUE)
		die_lasterr("CreateNamedPipe failed");

	hread = CreateFileW(name, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hread == INVALID_HANDLE_VALUE)
		die_lasterr("CreateFile for named pipe failed");

	/* start console spool thread on the pipe's read end */
	hthread = CreateThread(NULL, 0, console_thread, NULL, 0, NULL);
	if (!hthread)
		die_lasterr("CreateThread(console_thread) failed");

	/* schedule cleanup routine */
	if (atexit(winansi_exit))
		die_errno("atexit(winansi_exit) failed");

	/* redirect stdout / stderr to the pipe */
	if (con1)
		hconsole1 = swap_osfhnd(1, duplicate_handle(hwrite));
	if (con2)
		hconsole2 = swap_osfhnd(2, duplicate_handle(hwrite));
}

/*
 * Returns the real console handle if stdout / stderr is a pipe redirecting
 * to the console. Allows spawn / exec to pass the console to the next process.
 */
HANDLE winansi_get_osfhandle(int fd)
{
	HANDLE ret;

	if (fd == 1 && (fd_is_interactive[1] & FD_SWAPPED))
		return hconsole1;
	if (fd == 2 && (fd_is_interactive[2] & FD_SWAPPED))
		return hconsole2;

	ret = (HANDLE)_get_osfhandle(fd);

	/*
	 * There are obviously circumstances under which _get_osfhandle()
	 * returns (HANDLE)-2. This is not documented anywhere, but that is so
	 * clearly an invalid handle value that we can just work around this
	 * and return the correct value for invalid handles.
	 */
	return ret == (HANDLE)-2 ? INVALID_HANDLE_VALUE : ret;
}
