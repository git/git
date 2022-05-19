#include "../but-compat-util.h"

intmax_t butstrtoimax (const char *nptr, char **endptr, int base)
{
#if defined(NO_STRTOULL)
	return strtol(nptr, endptr, base);
#else
	return strtoll(nptr, endptr, base);
#endif
}
