#include "test-tool.h"
#include "grep.h"

int cmd__pcre2_config(int argc, const char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "has-PCRE2_MATCH_INVALID_UTF")) {
		int value = PCRE2_MATCH_INVALID_UTF;
		return !value;
	}
	return 1;
}
