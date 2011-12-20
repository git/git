#include "../git-compat-util.h"

/*
 * The size parameter specifies the available space, i.e. includes
 * the trailing NUL byte; but Windows's vsnprintf uses the entire
 * buffer and avoids the trailing NUL, should the buffer be exactly
 * big enough for the result. Defining SNPRINTF_SIZE_CORR to 1 will
 * therefore remove 1 byte from the reported buffer size, so we
 * always have room for a trailing NUL byte.
 */
#ifndef SNPRINTF_SIZE_CORR
#if defined(WIN32) && (!defined(__GNUC__) || __GNUC__ < 4)
#define SNPRINTF_SIZE_CORR 1
#else
#define SNPRINTF_SIZE_CORR 0
#endif
#endif

#undef vsnprintf
int git_vsnprintf(char *str, size_t maxsize, const char *format, va_list ap)
{
	va_list cp;
	char *s;
	int ret = -1;

	if (maxsize > 0) {
		va_copy(cp, ap);
		ret = vsnprintf(str, maxsize-SNPRINTF_SIZE_CORR, format, cp);
		va_end(cp);
		if (ret == maxsize-1)
			ret = -1;
		/* Windows does not NUL-terminate if result fills buffer */
		str[maxsize-1] = 0;
	}
	if (ret != -1)
		return ret;

	s = NULL;
	if (maxsize < 128)
		maxsize = 128;

	while (ret == -1) {
		maxsize *= 4;
		str = realloc(s, maxsize);
		if (! str)
			break;
		s = str;
		va_copy(cp, ap);
		ret = vsnprintf(str, maxsize-SNPRINTF_SIZE_CORR, format, cp);
		va_end(cp);
		if (ret == maxsize-1)
			ret = -1;
	}
	free(s);
	return ret;
}

int git_snprintf(char *str, size_t maxsize, const char *format, ...)
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = git_vsnprintf(str, maxsize, format, ap);
	va_end(ap);

	return ret;
}

