#include "../git-compat-util.h"
#undef regcomp

int git_regcomp(regex_t *preg, const char *pattern, int cflags)
{
	/*
	 * If you are on macOS with clang and fail to compile this line,
	 * https://lore.kernel.org/git/458ad3c1-96df-4575-ee42-e6eb754f25f6@gmx.de/
	 * might be relevant.
	 */
	if (!(cflags & REG_EXTENDED))
		cflags |= REG_ENHANCED;
	return regcomp(preg, pattern, cflags);
}
