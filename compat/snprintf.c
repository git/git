#include "../git-compat-util.h"

/*
 * The size parameter specifies the available space, i.e. includes
 * the trailing NUL byte; but Windows's vsnprintf expects the
 * number of characters to write without the trailing NUL.
 */
#ifndef SNPRINTF_SIZE_CORR
#define SNPRINTF_SIZE_CORR 0
#endif

#undef vsnprintf
int git_vsnprintf(char *str, size_t maxsize, const char *format, va_list ap)
{
	char *s;
	int ret = -1;

	if (maxsize > 0) {
		ret = vsnprintf(str, maxsize-SNPRINTF_SIZE_CORR, format, ap);
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
		ret = vsnprintf(str, maxsize-SNPRINTF_SIZE_CORR, format, ap);
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

