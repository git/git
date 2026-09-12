#define USE_THE_REPOSITORY_VARIABLE

#include "test-tool.h"
#include "copy.h"
#include "environment.h"
#include "repository.h"

int cmd__copy_file(int argc, const char **argv)
{
	if (argc != 3)
		return 129;
	return copy_file(the_repository, argv[2], argv[1], 0666) ? 1 : 0;
}
