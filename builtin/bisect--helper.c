#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "bisect.h"
#include "refs.h"

static const char * const git_bisect_helper_usage[] = {
	N_("git bisect--helper --next-all [--no-checkout]"),
	N_("git bisect--helper --check-term-format <term> <revision>"),
	NULL
};

static int one_of(const char *term, ...) {
	va_list matches;
	const char *match;

	va_start(matches, term);
	while ((match = va_arg(matches, const char *)) != NULL)
		if (!strcmp(term, match))
			return 1;

	va_end(matches);

	return 0;
}

static int check_term_format(const char *term, const char *revision, int flag) {
	if (check_refname_format(term, flag))
		die("'%s' is not a valid term", term);

	if (one_of(term, "help", "start", "skip", "next", "reset", "visualize",
	    "replay", "log", "run", NULL))
		die("can't use the builtin command '%s' as a term", term);

	/* In theory, nothing prevents swapping
	 * completely good and bad, but this situation
	 * could be confusing and hasn't been tested
	 * enough. Forbid it for now.
	 */

	if (!strcmp(term, "bad") || !strcmp(term, "new"))
		if (strcmp(revision, "bad"))
			die("can't change the meaning of term '%s'", term);

	if(!strcmp(term, "good") || !strcmp(term, "old"))
		if (strcmp(revision, "good"))
			die("can't change the meaning of term '%s'", term);

	return 0;
}

int cmd_bisect__helper(int argc, const char **argv, const char *prefix)
{
	int sub_command = 0;
	int no_checkout = 0;

	enum sub_commands {
		NEXT_ALL,
		CHECK_TERM_FMT
	};

	struct option options[] = {
		OPT_CMDMODE(0, "next-all", &sub_command,
			 N_("perform 'git bisect next'"), NEXT_ALL),
		OPT_BOOL(0, "no-checkout", &no_checkout,
			 N_("update BISECT_HEAD instead of checking out the current commit")),
		OPT_CMDMODE(0, "check-term-format", &sub_command,
			 N_("check the format of the ref"), CHECK_TERM_FMT),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_bisect_helper_usage, 0);

	if (sub_command == CHECK_TERM_FMT) {
		if (argc == 2) {
			if (argv[0] != NULL && argv[1] != NULL)
				return check_term_format(argv[0], argv[1], 0);
			else
				die("no revision or term provided with check_for_term");
		}
		else
			die("--check-term-format expects 2 arguments");
	}

	if (sub_command != NEXT_ALL && sub_command != CHECK_TERM_FMT)
		usage_with_options(git_bisect_helper_usage, options);

	/* next-all */
	if (sub_command == NEXT_ALL)
		return bisect_next_all(prefix, no_checkout);

	return 1;
}
