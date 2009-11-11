/*
 * GIT - The information manager from hell
 */

#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "strbuf.h"

static const char builtin_check_ref_format_usage[] =
"git check-ref-format <refname>\n"
"   or: git check-ref-format --branch <branchname-shorthand>";

int cmd_check_ref_format(int argc, const char **argv, const char *prefix)
{
	if (argc == 3 && !strcmp(argv[1], "--branch")) {
		struct strbuf sb = STRBUF_INIT;

		if (strbuf_check_branch_ref(&sb, argv[2]))
			die("'%s' is not a valid branch name", argv[2]);
		printf("%s\n", sb.buf + 11);
		exit(0);
	}
	if (argc != 2)
		usage(builtin_check_ref_format_usage);
	return !!check_ref_format(argv[1]);
}
