/*
 * GIT - Distributed version control system
 */
#include "builtin.h"
#include "refs.h"
#include "setup.h"
#include "strbuf.h"

static const char builtin_check_ref_format_usage[] =
"git check-ref-format [--normalize] [<options>] <refname>\n"
"   or: git check-ref-format --branch <branchname-shorthand>";

/*
 * Return a copy of refname but with leading slashes removed and runs
 * of adjacent slashes replaced with single slashes.
 *
 * This function is similar to normalize_path_copy(), but stripped down
 * to meet check_ref_format's simpler needs.
 */
static char *collapse_slashes(const char *refname)
{
	char *ret = xmallocz(strlen(refname));
	char ch;
	char prev = '/';
	char *cp = ret;

	while ((ch = *refname++) != '\0') {
		if (prev == '/' && ch == prev)
			continue;

		*cp++ = ch;
		prev = ch;
	}
	*cp = '\0';
	return ret;
}

static int check_ref_format_branch(const char *arg)
{
	struct strbuf sb = STRBUF_INIT;
	const char *name;
	int nongit;

	setup_git_directory_gently(&nongit);
	if (check_branch_ref(&sb, arg) ||
	    !skip_prefix(sb.buf, "refs/heads/", &name))
		die("'%s' is not a valid branch name", arg);
	printf("%s\n", name);
	strbuf_release(&sb);
	return 0;
}

int cmd_check_ref_format(int argc,
			 const char **argv,
			 const char *prefix,
			 struct repository *repo UNUSED)
{
	int i;
	int normalize = 0;
	int flags = 0;
	const char *refname;
	char *to_free = NULL;
	int ret = 1;

	BUG_ON_NON_EMPTY_PREFIX(prefix);

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(builtin_check_ref_format_usage);

	if (argc == 3 && !strcmp(argv[1], "--branch"))
		return check_ref_format_branch(argv[2]);

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "--normalize") || !strcmp(argv[i], "--print"))
			normalize = 1;
		else if (!strcmp(argv[i], "--allow-onelevel"))
			flags |= REFNAME_ALLOW_ONELEVEL;
		else if (!strcmp(argv[i], "--no-allow-onelevel"))
			flags &= ~REFNAME_ALLOW_ONELEVEL;
		else if (!strcmp(argv[i], "--refspec-pattern"))
			flags |= REFNAME_REFSPEC_PATTERN;
		else
			usage(builtin_check_ref_format_usage);
	}
	if (! (i == argc - 1))
		usage(builtin_check_ref_format_usage);

	refname = argv[i];
	if (normalize)
		refname = to_free = collapse_slashes(refname);
	if (check_refname_format(refname, flags))
		goto cleanup;
	if (normalize)
		printf("%s\n", refname);

	ret = 0;
cleanup:
	free(to_free);
	return ret;
}
