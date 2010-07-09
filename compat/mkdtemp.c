#include "../git-compat-util.h"

char *gitmkdtemp(char *template)
{
	if (!*mktemp(template) || mkdir(template, 0700))
		return NULL;
	return template;
}
