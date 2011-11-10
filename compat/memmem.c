#include "../git-compat-util.h"

void *gitmemmem(const void *haystack, size_t haystack_len,
                const void *needle, size_t needle_len)
{
	const char *begin = haystack;
	const char *last_possible = begin + haystack_len - needle_len;
	const char *tail = needle;
	char point;

	/*
	 * The first occurrence of the empty string is deemed to occur at
	 * the beginning of the string.
	 */
	if (needle_len == 0)
		return (void *)begin;

	/*
	 * Sanity check, otherwise the loop might search through the whole
	 * memory.
	 */
	if (haystack_len < needle_len)
		return NULL;

	point = *tail++;
	for (; begin <= last_possible; begin++) {
		if (*begin == point && !memcmp(begin + 1, tail, needle_len - 1))
			return (void *)begin;
	}

	return NULL;
}
