#include "../git-compat-util.h"

char *gitstrcasestr(const char *haystack, const char *needle)
{
	size_t nlen = strlen(needle);
	size_t hlen = strlen(haystack) - nlen + 1;
	size_t i = 0;
	size_t j;
	while (i < hlen) {
		j = 0;
		while (j < nlen) {
			unsigned char c1 = haystack[i+j];
			unsigned char c2 = needle[j];
			if (toupper(c1) != toupper(c2)){
				i++;
				goto next;
			}
			j++;
		}
		return (char *) haystack + i;
	next:
		;
	}
	return NULL;
}
