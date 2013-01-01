#include "cache.h"
#include "wildmatch.h"

int main(int argc, char **argv)
{
	int i;
	for (i = 2; i < argc; i++) {
		if (argv[i][0] == '/')
			die("Forward slash is not allowed at the beginning of the\n"
			    "pattern because Windows does not like it. Use `XXX/' instead.");
		else if (!strncmp(argv[i], "XXX/", 4))
			argv[i] += 3;
	}
	if (!strcmp(argv[1], "wildmatch"))
		return !!wildmatch(argv[3], argv[2], 0, NULL);
	else if (!strcmp(argv[1], "iwildmatch"))
		return !!wildmatch(argv[3], argv[2], WM_CASEFOLD, NULL);
	else if (!strcmp(argv[1], "fnmatch"))
		return !!fnmatch(argv[3], argv[2], FNM_PATHNAME);
	else
		return 1;
}
