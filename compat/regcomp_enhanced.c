#include "../git-compat-util.h"
#undef regcomp

int git_regcomp(regex_t *preg, const char *pattern, int cflags)
{
	if (!(cflags & REG_EXTENDED))
		cflags |= REG_ENHANCED;
	return regcomp(preg, pattern, cflags);
}
