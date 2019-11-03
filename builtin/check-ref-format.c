/*
 * GIT - The information manager from hell
 */

#include "cache.h"
#include "refs.h"
#include "builtin.h"
#include "strbuf.h"
#include "parse-options.h"

static const char * const builtin_check_ref_format_usage[] = {
	N_("git check-ref-format [--normalize] [<options>] <refname>\n"),
	N_("   or: git check-ref-format --branch <branchname-shorthand>"),
	NULL,
};

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
	if (strbuf_check_branch_ref(&sb, arg) ||
	    !skip_prefix(sb.buf, "refs/heads/", &name))
		die("'%s' is not a valid branch name", arg);
	printf("%s\n", name);
	strbuf_release(&sb);
	return 0;
}

int cmd_check_ref_format(int argc, const char **argv, const char *prefix)
{
	enum {
		CHECK_REF_FORMAT_BRANCH,
	};
	int i = 0;
	int verbose;
	int normalize;
	int allow_onelevel;
	int refspec_pattern;
	int flags = 0;
	const char *refname;

	struct option options[] = {
		OPT__VERBOSE(&verbose, N_("be verbose")),
		OPT_GROUP(""),
		OPT_CMDMODE( 0 , "branch", &check_ref_format_branch, N_("branch"), CHECK_REF_FORMAT_BRANCH),
		OPT_BOOL( 0 , "normalize", &normalize, N_("normalize tracked files")),
		OPT_BOOL( 0 , "allow-onelevel", &allow_onelevel, N_("allow one level")),
		OPT_BOOL( 0 , "refspec-pattern", &refspec_pattern, N_("refspec pattern")),
		OPT_END(),
	};

	argc = parse_options(argc, argv, prefix, options, builtin_check_ref_format_usage, PARSE_OPT_KEEP_ARGV0);

	refname = argv[i];
	if (normalize)
		refname = collapse_slashes(refname);
	if (check_refname_format(refname, flags))
		return 1;
	if (normalize)
		printf("%s\n", refname);

	return 0;
}
