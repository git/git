#include "../git-compat-util.h"

char *gitstrdup(const char *s1)
{
	size_t len = strlen(s1) + 1;
	char *s2 = malloc(len);

	if (s2)
		memcpy(s2, s1, len);
	return s2;
}
