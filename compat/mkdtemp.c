#include "../git-compat-util.h"

char *gitmkdtemp(char *template)
{
	return git_mkdtemp(template);
}
