#include "../git-compat-util.h"

char *gitstrcasestr(const char *haystack, const char *needle)
{
	int nlen = strlen(needle);
	int hlen = strlen(haystack) - nlen + 1;

	for (int i = 0; i < hlen; i++) {
		for (int j = 0; j < nlen; j++) {
			unsigned char c1 = haystack[i+j];
			unsigned char c2 = needle[j];
			if (toupper(c1) != toupper(c2))
				goto next;
		}
		return (char *) haystack + i;
	next:
		;
	}
	return NULL;
}
