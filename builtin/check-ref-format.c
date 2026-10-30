/*
 * GIT - The information manager from hell
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "builtin.h"
#include "parse-options.h"
#include "refs.h"
#include "setup.h"
#include "strbuf.h"

static const char * const builtin_check_ref_format_usage[] = {
	N_("git check-ref-format [--normalize] [<options>] <refname>"),
	N_("git check-ref-format --branch <branchname-shorthand>"),
	NULL
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

	setup_git_directory_gently(the_repository, &nongit);
	if (check_branch_ref(the_repository, &sb, arg) ||
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
	int normalize = 0;
	int flags = 0;
	const char *refname;
	char *to_free = NULL;
	int ret = 1;
	struct option options[] = {
		OPT_BOOL_F(0, "normalize", &normalize,
			   N_("normalize the refname and print it"),
			   PARSE_OPT_NONEG),
		OPT_BOOL_F(0, "print", &normalize,
			   N_("deprecated synonym for --normalize"),
			   PARSE_OPT_NONEG | PARSE_OPT_HIDDEN),
		OPT_BIT(0, "allow-onelevel", &flags,
			N_("accept a refname with a single level"),
			REFNAME_ALLOW_ONELEVEL),
		OPT_BIT_F(0, "refspec-pattern", &flags,
			  N_("interpret <refname> as a reference pattern for a refspec"),
			  REFNAME_REFSPEC_PATTERN, PARSE_OPT_NONEG),
		OPT_END()
	};

	BUG_ON_NON_EMPTY_PREFIX(prefix);

	if (argc == 3 && !strcmp(argv[1], "--branch"))
		return check_ref_format_branch(argv[2]);

	argc = parse_options(argc, argv, prefix, options,
			     builtin_check_ref_format_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc != 1)
		usage_with_options(builtin_check_ref_format_usage, options);

	refname = argv[0];
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
