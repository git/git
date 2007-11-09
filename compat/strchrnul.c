#include "../git-compat-util.h"

char *gitstrchrnul(const char *s, int c)
{
	while (*s && *s != c)
		s++;
	return (char *)s;
}
