#include "cache.h"
#include "wildmatch.h"

int main(int argc, char **argv)
{
	if (!strcmp(argv[1], "wildmatch"))
		return wildmatch(argv[3], argv[2]) ? 0 : 1;
	else if (!strcmp(argv[1], "iwildmatch"))
		return iwildmatch(argv[3], argv[2]) ? 0 : 1;
	else if (!strcmp(argv[1], "fnmatch"))
		return !!fnmatch(argv[3], argv[2], FNM_PATHNAME);
	else
		return 1;
}
