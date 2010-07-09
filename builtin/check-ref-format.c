/*
 * GIT - The information manager from hell
 */

#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "strbuf.h"

static const char builtin_check_ref_format_usage[] =
"git check-ref-format [--print] <refname>\n"
"   or: git check-ref-format --branch <branchname-shorthand>";

/*
 * Replace each run of adjacent slashes in src with a single slash,
 * and write the result to dst.
 *
 * This function is similar to normalize_path_copy(), but stripped down
 * to meet check_ref_format's simpler needs.
 */
static void collapse_slashes(char *dst, const char *src)
{
	char ch;
	char prev = '\0';

	while ((ch = *src++) != '\0') {
		if (prev == '/' && ch == prev)
			continue;

		*dst++ = ch;
		prev = ch;
	}
	*dst = '\0';
}

int cmd_check_ref_format(int argc, const char **argv, const char *prefix)
{
	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(builtin_check_ref_format_usage);

	if (argc == 3 && !strcmp(argv[1], "--branch")) {
		struct strbuf sb = STRBUF_INIT;

		if (strbuf_check_branch_ref(&sb, argv[2]))
			die("'%s' is not a valid branch name", argv[2]);
		printf("%s\n", sb.buf + 11);
		exit(0);
	}
	if (argc == 3 && !strcmp(argv[1], "--print")) {
		char *refname = xmalloc(strlen(argv[2]) + 1);

		if (check_ref_format(argv[2]))
			exit(1);
		collapse_slashes(refname, argv[2]);
		printf("%s\n", refname);
		exit(0);
	}
	if (argc != 2)
		usage(builtin_check_ref_format_usage);
	return !!check_ref_format(argv[1]);
}
