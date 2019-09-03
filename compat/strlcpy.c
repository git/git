#include "../git-compat-util.h"

size_t gitstrlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret < size) ? ret : size - 1;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}
