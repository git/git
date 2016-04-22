#include "git-compat-util.h"
#include "urlmatch.h"

int main(int argc, char **argv)
{
	const char usage[] = "test-urlmatch-normalization [-p | -l] <url1> | <url1> <url2>";
	char *url1, *url2;
	int opt_p = 0, opt_l = 0;

	/*
	 * For one url, succeed if url_normalize succeeds on it, fail otherwise.
	 * For two urls, succeed only if url_normalize succeeds on both and
	 * the results compare equal with strcmp.  If -p is given (one url only)
	 * and url_normalize succeeds, print the result followed by "\n".  If
	 * -l is given (one url only) and url_normalize succeeds, print the
	 * returned length in decimal followed by "\n".
	 */

	if (argc > 1 && !strcmp(argv[1], "-p")) {
		opt_p = 1;
		argc--;
		argv++;
	} else if (argc > 1 && !strcmp(argv[1], "-l")) {
		opt_l = 1;
		argc--;
		argv++;
	}

	if (argc < 2 || argc > 3)
		die("%s", usage);

	if (argc == 2) {
		struct url_info info;
		url1 = url_normalize(argv[1], &info);
		if (!url1)
			return 1;
		if (opt_p)
			printf("%s\n", url1);
		if (opt_l)
			printf("%u\n", (unsigned)info.url_len);
		return 0;
	}

	if (opt_p || opt_l)
		die("%s", usage);

	url1 = url_normalize(argv[1], NULL);
	url2 = url_normalize(argv[2], NULL);
	return (url1 && url2 && !strcmp(url1, url2)) ? 0 : 1;
}
