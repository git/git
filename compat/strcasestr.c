#include "../git-compat-util.h"

char *gitstrcasestr(const char *haystack, const char *needle)
{
	size_t nlen = strlen(needle);
	size_t hlen = strlen(haystack) - nlen + 1;
	size_t i;

	for (i = 0; i < hlen; i++) {
		size_t j;
		for (j = 0; j < nlen; j++) {
			unsigned char c1 = haystack[i+j];
			unsigned char c2 = needle[j];
			if (tolower(c1) != tolower(c2))
				goto next;
		}
		return (char *) haystack + i;
	next:
		;
	}
	return NULL;
}
