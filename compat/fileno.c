#define COMPAT_CODE_FILENO
#include "../git-compat-util.h"

int git_fileno(FILE *stream)
{
	return fileno(stream);
}
