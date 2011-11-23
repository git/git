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
 * Remove leading slashes and replace each run of adjacent slashes in
 * src with a single slash, and write the result to dst.
 *
 * This function is similar to normalize_path_copy(), but stripped down
 * to meet check_ref_format's simpler needs.
 */
static void collapse_slashes(char *dst, const char *src)
{
	char ch;
	char prev = '/';

	while ((ch = *src++) != '\0') {
		if (prev == '/' && ch == prev)
			continue;

		*dst++ = ch;
		prev = ch;
	}
	*dst = '\0';
}

static int check_ref_format_branch(const char *arg)
{
	struct strbuf sb = STRBUF_INIT;
	int nongit;

	setup_git_directory_gently(&nongit);
	if (strbuf_check_branch_ref(&sb, arg))
		die("'%s' is not a valid branch name", arg);
	printf("%s\n", sb.buf + 11);
	return 0;
}

static int check_ref_format_print(const char *arg)
{
	char *refname = xmalloc(strlen(arg) + 1);

	if (check_ref_format(arg))
		return 1;
	collapse_slashes(refname, arg);
	printf("%s\n", refname);
	return 0;
}

int cmd_check_ref_format(int argc, const char **argv, const char *prefix)
{
	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(builtin_check_ref_format_usage);

	if (argc == 3 && !strcmp(argv[1], "--branch"))
		return check_ref_format_branch(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "--print"))
		return check_ref_format_print(argv[2]);
	if (argc != 2)
		usage(builtin_check_ref_format_usage);
	return !!check_ref_format(argv[1]);
}
