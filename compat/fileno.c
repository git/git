#define COMPAT_CODE
#include "../git-compat-util.h"

int git_fileno(FILE *stream)
{
	return fileno(stream);
}
