#include "builtin.h"
#include "attr.h"
#include "quote.h"

static const char check_attr_usage[] =
"git-check-attr attr... [--] pathname...";

int cmd_check_attr(int argc, const char **argv, const char *prefix)
{
	struct git_attr_check *check;
	int cnt, i, doubledash;

	doubledash = -1;
	for (i = 1; doubledash < 0 && i < argc; i++) {
		if (!strcmp(argv[i], "--"))
			doubledash = i;
	}

	/* If there is no double dash, we handle only one attribute */
	if (doubledash < 0) {
		cnt = 1;
		doubledash = 1;
	} else
		cnt = doubledash - 1;
	doubledash++;

	if (cnt <= 0 || argc < doubledash)
		usage(check_attr_usage);
	check = xcalloc(cnt, sizeof(*check));
	for (i = 0; i < cnt; i++) {
		const char *name;
		name = argv[i + 1];
		check[i].attr = git_attr(name, strlen(name));
	}

	for (i = doubledash; i < argc; i++) {
		int j;
		if (git_checkattr(argv[i], cnt, check))
			die("git_checkattr died");
		for (j = 0; j < cnt; j++) {
			write_name_quoted("", 0, argv[i], 1, stdout);
			printf(": %s: %s\n", argv[j+1],
			       (check[j].isset < 0) ? "unspecified" :
			       (check[j].isset == 0) ? "unset" :
			       "set");
		}
	}
	return 0;
}
