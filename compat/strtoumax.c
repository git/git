#include "../git-compat-util.h"

uintmax_t gitstrtoumax (const char *nptr, char **endptr, int base)
{
#if defined(NO_STRTOULL)
	return strtoul(nptr, endptr, base);
#else
	return strtoull(nptr, endptr, base);
#endif
}
