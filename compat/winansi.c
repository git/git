/*
 * Copyright 2008 Peter Harris <git@peter.is-a-geek.org>
 */

#undef NOGDI
#include "../git-compat-util.h"
#include <malloc.h>
#include <wingdi.h>
#include <winreg.h>

/*
 Functions to be wrapped:
*/
#undef printf
#undef fprintf
#undef fputs
#undef vfprintf
/* TODO: write */

/*
 ANSI codes used by git: m, K

 This file is git-specific. Therefore, this file does not attempt
 to implement any codes that are not used by git.
*/

static HANDLE console;
static WORD plain_attr;
static WORD attr;
static int negative;
static FILE *last_stream = NULL;
static int non_ascii_used = 0;

#ifdef __MINGW32__
typedef struct _CONSOLE_FONT_INFOEX {
	ULONG cbSize;
	DWORD nFont;
	COORD dwFontSize;
	UINT FontFamily;
	UINT FontWeight;
	WCHAR FaceName[LF_FACESIZE];
} CONSOLE_FONT_INFOEX, *PCONSOLE_FONT_INFOEX;
#endif

typedef BOOL (WINAPI *PGETCURRENTCONSOLEFONTEX)(HANDLE, BOOL,
		PCONSOLE_FONT_INFOEX);

static void warn_if_raster_font(void)
{
	DWORD fontFamily = 0;
	PGETCURRENTCONSOLEFONTEX pGetCurrentConsoleFontEx;

	/* don't bother if output was ascii only */
	if (!non_ascii_used)
		return;

	/* GetCurrentConsoleFontEx is available since Vista */
	pGetCurrentConsoleFontEx = (PGETCURRENTCONSOLEFONTEX) GetProcAddress(
			GetModuleHandle("kernel32.dll"), "GetCurrentConsoleFontEx");
	if (pGetCurrentConsoleFontEx) {
		CONSOLE_FONT_INFOEX cfi;
		cfi.cbSize = sizeof(cfi);
		if (pGetCurrentConsoleFontEx(console, 0, &cfi))
			fontFamily = cfi.FontFamily;
	} else {
		/* pre-Vista: check default console font in registry */
		HKEY hkey;
		if (ERROR_SUCCESS == RegOpenKeyExA(HKEY_CURRENT_USER, "Console", 0,
				KEY_READ, &hkey)) {
			DWORD size = sizeof(fontFamily);
			RegQueryValueExA(hkey, "FontFamily", NULL, NULL,
					(LPVOID) &fontFamily, &size);
			RegCloseKey(hkey);
		}
	}

	if (!(fontFamily & TMPF_TRUETYPE))
		warning("Your console font probably doesn\'t support "
			"Unicode. If you experience strange characters in the output, "
			"consider switching to a TrueType font such as Lucida Console!");
}

static int is_console(FILE *stream)
{
	CONSOLE_SCREEN_BUFFER_INFO sbi;
	HANDLE hcon;

	static int initialized = 0;

	/* use cached value if stream hasn't changed */
	if (stream == last_stream)
		return console != NULL;

	last_stream = stream;
	console = NULL;

	/* get OS handle of the stream */
	hcon = (HANDLE) _get_osfhandle(_fileno(stream));
	if (hcon == INVALID_HANDLE_VALUE)
		return 0;

	/* check if its a handle to a console output screen buffer */
	if (!GetConsoleScreenBufferInfo(hcon, &sbi))
		return 0;

	if (!initialized) {
		attr = plain_attr = sbi.wAttributes;
		negative = 0;
		initialized = 1;
		/* check console font on exit */
		atexit(warn_if_raster_font);
	}

	console = hcon;
	return 1;
}

static int write_console(const char *str, size_t len)
{
	/* convert utf-8 to utf-16, write directly to console */
	int wlen = MultiByteToWideChar(CP_UTF8, 0, str, len, NULL, 0);
	wchar_t *wbuf = (wchar_t *) alloca(wlen * sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8, 0, str, len, wbuf, wlen);

	WriteConsoleW(console, wbuf, wlen, NULL, NULL);

	/* remember if non-ascii characters are printed */
	if (wlen != len)
		non_ascii_used = 1;

	/* return original (utf-8 encoded) length */
	return len;
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


static const char *set_attr(const char *str)
{
	const char *func;
	size_t len = strspn(str, "0123456789;");
	func = str + len;

	switch (*func) {
	case 'm':
		do {
			long val = strtol(str, (char **)&str, 10);
			switch (val) {
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
			str++;
		} while (*(str-1) == ';');

		set_console_attr();
		break;
	case 'K':
		erase_in_line();
		break;
	default:
		/* Unsupported code */
		break;
	}

	return func + 1;
}

static int ansi_emulate(const char *str, FILE *stream)
{
	int rv = 0;
	const char *pos = str;

	fflush(stream);

	while (*pos) {
		pos = strstr(str, "\033[");
		if (pos) {
			size_t len = pos - str;

			if (len) {
				size_t out_len = write_console(str, len);
				rv += out_len;
				if (out_len < len)
					return rv;
			}

			str = pos + 2;
			rv += 2;

			pos = set_attr(str);
			rv += pos - str;
			str = pos;
		} else {
			size_t len = strlen(str);
			rv += write_console(str, len);
			return rv;
		}
	}
	return rv;
}

int winansi_fputs(const char *str, FILE *stream)
{
	int rv;

	if (!is_console(stream))
		return fputs(str, stream);

	rv = ansi_emulate(str, stream);

	if (rv >= 0)
		return 0;
	else
		return EOF;
}

int winansi_vfprintf(FILE *stream, const char *format, va_list list)
{
	int len, rv;
	char small_buf[256];
	char *buf = small_buf;
	va_list cp;

	if (!is_console(stream))
		goto abort;

	va_copy(cp, list);
	len = vsnprintf(small_buf, sizeof(small_buf), format, cp);
	va_end(cp);

	if (len > sizeof(small_buf) - 1) {
		buf = malloc(len + 1);
		if (!buf)
			goto abort;

		len = vsnprintf(buf, len + 1, format, list);
	}

	rv = ansi_emulate(buf, stream);

	if (buf != small_buf)
		free(buf);
	return rv;

abort:
	rv = vfprintf(stream, format, list);
	return rv;
}

int winansi_fprintf(FILE *stream, const char *format, ...)
{
	va_list list;
	int rv;

	va_start(list, format);
	rv = winansi_vfprintf(stream, format, list);
	va_end(list);

	return rv;
}

int winansi_printf(const char *format, ...)
{
	va_list list;
	int rv;

	va_start(list, format);
	rv = winansi_vfprintf(stdout, format, list);
	va_end(list);

	return rv;
}
