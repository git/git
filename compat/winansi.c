/*
 * Copyright 2008 Peter Harris <git@peter.is-a-geek.org>
 */

#include "../git-compat-util.h"

/*
 Functions to be wrapped:
*/
#undef printf
#undef fprintf
#undef fputs
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

static void init(void)
{
	CONSOLE_SCREEN_BUFFER_INFO sbi;

	static int initialized = 0;
	if (initialized)
		return;

	console = GetStdHandle(STD_OUTPUT_HANDLE);
	if (console == INVALID_HANDLE_VALUE)
		console = NULL;

	if (!console)
		return;

	GetConsoleScreenBufferInfo(console, &sbi);
	attr = plain_attr = sbi.wAttributes;
	negative = 0;

	initialized = 1;
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

	while (*pos) {
		pos = strstr(str, "\033[");
		if (pos) {
			size_t len = pos - str;

			if (len) {
				size_t out_len = fwrite(str, 1, len, stream);
				rv += out_len;
				if (out_len < len)
					return rv;
			}

			str = pos + 2;
			rv += 2;

			fflush(stream);

			pos = set_attr(str);
			rv += pos - str;
			str = pos;
		} else {
			rv += strlen(str);
			fputs(str, stream);
			return rv;
		}
	}
	return rv;
}

int winansi_fputs(const char *str, FILE *stream)
{
	int rv;

	if (!isatty(fileno(stream)))
		return fputs(str, stream);

	init();

	if (!console)
		return fputs(str, stream);

	rv = ansi_emulate(str, stream);

	if (rv >= 0)
		return 0;
	else
		return EOF;
}

static int winansi_vfprintf(FILE *stream, const char *format, va_list list)
{
	int len, rv;
	char small_buf[256];
	char *buf = small_buf;
	va_list cp;

	if (!isatty(fileno(stream)))
		goto abort;

	init();

	if (!console)
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
