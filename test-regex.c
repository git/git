#include "git-compat-util.h"

static int test_regex_bug(void)
{
	char *pat = "[^={} \t]+";
	char *str = "={}\nfred";
	regex_t r;
	regmatch_t m[1];

	if (regcomp(&r, pat, REG_EXTENDED | REG_NEWLINE))
		die("failed regcomp() for pattern '%s'", pat);
	if (regexec(&r, str, 1, m, 0))
		die("no match of pattern '%s' to string '%s'", pat, str);

	/* http://sourceware.org/bugzilla/show_bug.cgi?id=3957  */
	if (m[0].rm_so == 3) /* matches '\n' when it should not */
		die("regex bug confirmed: re-build git with NO_REGEX=1");

	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "--bug"))
		return test_regex_bug();
	else
		usage("test-regex --bug");
}
