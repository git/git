#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "bisect.h"
#include "refs.h"

static const char * const git_bisect_helper_usage[] = {
	N_("git bisect--helper --next-all [--no-checkout]"),
	N_("git bisect--helper --check-term-format <term> <orig_term>"),
	NULL
};

enum sub_commands {
	NEXT_ALL = 1,
	CHECK_TERM_FMT
};

static int one_of(const char *term, ...)
{
	va_list matches;
	const char *match;

	va_start(matches, term);
	while((match = va_arg(matches, const char *)) != NULL)
		if(!strcmp(term, match))
			return 1;

	va_end(matches);

	return 0;
}

static int check_term_format(const char *term, const char *orig_term,
                             int flag)
{
	char new_term[1000] = "refs/bisect/";
	strcat(new_term, term);
	int result = 0;

	printf("hi: \n");
	printf("new_term: %s\n", new_term);
	if (check_refname_format(new_term, flag)) {
		printf("inside if\n");
		result = 1;
		printf("'%s' is not a valid term\n", term);
	}

	else if (one_of(term, "help", "start", "skip", "next", "reset",
	           "visualize", "replay", "log", "run", NULL)) {
		result = 1;
		printf("can't use the builtin command '%s' as a term\n", term);
	}

	/*
	 * In theory, nothing prevents swapping
	 * completely good and bad, but this situation
	 * could be confusing and hasn't been tested
	 * enough. Forbid it for now.
	 */

	else if ((one_of(term, "bad", "new", NULL) && !strcmp(orig_term, "bad")) ||
	    (one_of(term, "good", "old", NULL) && !strcmp(orig_term, "good"))) {
		result = 1;
		printf("can't change the meaning of the term '%s'.fuck\n", term);
	}

	return result;
}

int cmd_bisect__helper(int argc, const char **argv, const char *prefix)
{
	int sub_command = 0;
	int no_checkout = 0;
	struct option options[] = {
		OPT_CMDMODE(0, "next-all", &sub_command,
			 N_("perform 'git bisect next'"), NEXT_ALL),
		OPT_CMDMODE(0, "check-term-format", &sub_command,
			 N_("check format of the ref"), CHECK_TERM_FMT),
		OPT_BOOL(0, "no-checkout", &no_checkout,
			 N_("update BISECT_HEAD instead of checking out the current commit")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_bisect_helper_usage, 0);

	if (!sub_command)
		usage_with_options(git_bisect_helper_usage, options);

	switch (sub_command) {
	case NEXT_ALL:
		return bisect_next_all(prefix, no_checkout);
	case CHECK_TERM_FMT:
		if (argc == 2) {
			int c = check_term_format(argv[0], argv[1], 0);
			printf("exit code: %d\n", c);
			return c;
		}
		else
			die("insufficient arguments");
	}
	return 1;
}
