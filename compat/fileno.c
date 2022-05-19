#define COMPAT_CODE_FILENO
#include "../but-compat-util.h"

int but_fileno(FILE *stream)
{
	return fileno(stream);
}
