/*
 * GIT - The information manager from hell
 */

#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "strbuf.h"

int cmd_check_ref_format(int argc, const char **argv, const char *prefix)
{
	if (argc == 3 && !strcmp(argv[1], "--branch")) {
		struct strbuf sb = STRBUF_INIT;
		strbuf_branchname(&sb, argv[2]);
		strbuf_splice(&sb, 0, 0, "refs/heads/", 11);
		if (check_ref_format(sb.buf))
			die("'%s' is not a valid branch name", argv[2]);
		printf("%s\n", sb.buf + 11);
		exit(0);
	}
	if (argc != 2)
		usage("git check-ref-format refname");
	return !!check_ref_format(argv[1]);
}
