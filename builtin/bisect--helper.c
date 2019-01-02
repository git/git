#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "bisect.h"
#include "refs.h"
#include "dir.h"
#include "argv-array.h"
#include "run-command.h"

static GIT_PATH_FUNC(git_path_bisect_terms, "BISECT_TERMS")
static GIT_PATH_FUNC(git_path_bisect_expected_rev, "BISECT_EXPECTED_REV")
static GIT_PATH_FUNC(git_path_bisect_ancestors_ok, "BISECT_ANCESTORS_OK")
static GIT_PATH_FUNC(git_path_bisect_start, "BISECT_START")
static GIT_PATH_FUNC(git_path_bisect_head, "BISECT_HEAD")
static GIT_PATH_FUNC(git_path_bisect_log, "BISECT_LOG")

static const char * const git_bisect_helper_usage[] = {
	N_("git bisect--helper --next-all [--no-checkout]"),
	N_("git bisect--helper --write-terms <bad_term> <good_term>"),
	N_("git bisect--helper --bisect-clean-state"),
	N_("git bisect--helper --bisect-reset [<commit>]"),
	N_("git bisect--helper --bisect-write [--no-log] <state> <revision> <good_term> <bad_term>"),
	NULL
};

struct bisect_terms {
	char *term_good;
	char *term_bad;
};

static void free_terms(struct bisect_terms *terms)
{
	FREE_AND_NULL(terms->term_good);
	FREE_AND_NULL(terms->term_bad);
}

static void set_terms(struct bisect_terms *terms, const char *bad,
		      const char *good)
{
	free((void *)terms->term_good);
	terms->term_good = xstrdup(good);
	free((void *)terms->term_bad);
	terms->term_bad = xstrdup(bad);
}

/*
 * Check whether the string `term` belongs to the set of strings
 * included in the variable arguments.
 */
LAST_ARG_MUST_BE_NULL
static int one_of(const char *term, ...)
{
	int res = 0;
	va_list matches;
	const char *match;

	va_start(matches, term);
	while (!res && (match = va_arg(matches, const char *)))
		res = !strcmp(term, match);
	va_end(matches);

	return res;
}

static int check_term_format(const char *term, const char *orig_term)
{
	int res;
	char *new_term = xstrfmt("refs/bisect/%s", term);

	res = check_refname_format(new_term, 0);
	free(new_term);

	if (res)
		return error(_("'%s' is not a valid term"), term);

	if (one_of(term, "help", "start", "skip", "next", "reset",
			"visualize", "view", "replay", "log", "run", "terms", NULL))
		return error(_("can't use the builtin command '%s' as a term"), term);

	/*
	 * In theory, nothing prevents swapping completely good and bad,
	 * but this situation could be confusing and hasn't been tested
	 * enough. Forbid it for now.
	 */

	if ((strcmp(orig_term, "bad") && one_of(term, "bad", "new", NULL)) ||
		 (strcmp(orig_term, "good") && one_of(term, "good", "old", NULL)))
		return error(_("can't change the meaning of the term '%s'"), term);

	return 0;
}

static int write_terms(const char *bad, const char *good)
{
	FILE *fp = NULL;
	int res;

	if (!strcmp(bad, good))
		return error(_("please use two different terms"));

	if (check_term_format(bad, "bad") || check_term_format(good, "good"))
		return -1;

	fp = fopen(git_path_bisect_terms(), "w");
	if (!fp)
		return error_errno(_("could not open the file BISECT_TERMS"));

	res = fprintf(fp, "%s\n%s\n", bad, good);
	res |= fclose(fp);
	return (res < 0) ? -1 : 0;
}

static int is_expected_rev(const char *expected_hex)
{
	struct strbuf actual_hex = STRBUF_INIT;
	int res = 0;
	if (strbuf_read_file(&actual_hex, git_path_bisect_expected_rev(), 0) >= 40) {
		strbuf_trim(&actual_hex);
		res = !strcmp(actual_hex.buf, expected_hex);
	}
	strbuf_release(&actual_hex);
	return res;
}

static void check_expected_revs(const char **revs, int rev_nr)
{
	int i;

	for (i = 0; i < rev_nr; i++) {
		if (!is_expected_rev(revs[i])) {
			unlink_or_warn(git_path_bisect_ancestors_ok());
			unlink_or_warn(git_path_bisect_expected_rev());
		}
	}
}

static int bisect_reset(const char *commit)
{
	struct strbuf branch = STRBUF_INIT;

	if (!commit) {
		if (strbuf_read_file(&branch, git_path_bisect_start(), 0) < 1) {
			printf(_("We are not bisecting.\n"));
			return 0;
		}
		strbuf_rtrim(&branch);
	} else {
		struct object_id oid;

		if (get_oid_commit(commit, &oid))
			return error(_("'%s' is not a valid commit"), commit);
		strbuf_addstr(&branch, commit);
	}

	if (!file_exists(git_path_bisect_head())) {
		struct argv_array argv = ARGV_ARRAY_INIT;

		argv_array_pushl(&argv, "checkout", branch.buf, "--", NULL);
		if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
			strbuf_release(&branch);
			argv_array_clear(&argv);
			return error(_("could not check out original"
				       " HEAD '%s'. Try 'git bisect"
				       "reset <commit>'."), branch.buf);
		}
		argv_array_clear(&argv);
	}

	strbuf_release(&branch);
	return bisect_clean_state();
}

static void log_commit(FILE *fp, char *fmt, const char *state,
		       struct commit *commit)
{
	struct pretty_print_context pp = {0};
	struct strbuf commit_msg = STRBUF_INIT;
	char *label = xstrfmt(fmt, state);

	format_commit_message(commit, "%s", &commit_msg, &pp);

	fprintf(fp, "# %s: [%s] %s\n", label, oid_to_hex(&commit->object.oid),
		commit_msg.buf);

	strbuf_release(&commit_msg);
	free(label);
}

static int bisect_write(const char *state, const char *rev,
			const struct bisect_terms *terms, int nolog)
{
	struct strbuf tag = STRBUF_INIT;
	struct object_id oid;
	struct commit *commit;
	FILE *fp = NULL;
	int retval = 0;

	if (!strcmp(state, terms->term_bad)) {
		strbuf_addf(&tag, "refs/bisect/%s", state);
	} else if (one_of(state, terms->term_good, "skip", NULL)) {
		strbuf_addf(&tag, "refs/bisect/%s-%s", state, rev);
	} else {
		retval = error(_("Bad bisect_write argument: %s"), state);
		goto finish;
	}

	if (get_oid(rev, &oid)) {
		retval = error(_("couldn't get the oid of the rev '%s'"), rev);
		goto finish;
	}

	if (update_ref(NULL, tag.buf, &oid, NULL, 0,
		       UPDATE_REFS_MSG_ON_ERR)) {
		retval = -1;
		goto finish;
	}

	fp = fopen(git_path_bisect_log(), "a");
	if (!fp) {
		retval = error_errno(_("couldn't open the file '%s'"), git_path_bisect_log());
		goto finish;
	}

	commit = lookup_commit_reference(the_repository, &oid);
	log_commit(fp, "%s", state, commit);

	if (!nolog)
		fprintf(fp, "git bisect %s %s\n", state, rev);

finish:
	if (fp)
		fclose(fp);
	strbuf_release(&tag);
	return retval;
}

int cmd_bisect__helper(int argc, const char **argv, const char *prefix)
{
	enum {
		NEXT_ALL = 1,
		WRITE_TERMS,
		BISECT_CLEAN_STATE,
		CHECK_EXPECTED_REVS,
		BISECT_RESET,
		BISECT_WRITE
	} cmdmode = 0;
	int no_checkout = 0, res = 0, nolog = 0;
	struct option options[] = {
		OPT_CMDMODE(0, "next-all", &cmdmode,
			 N_("perform 'git bisect next'"), NEXT_ALL),
		OPT_CMDMODE(0, "write-terms", &cmdmode,
			 N_("write the terms to .git/BISECT_TERMS"), WRITE_TERMS),
		OPT_CMDMODE(0, "bisect-clean-state", &cmdmode,
			 N_("cleanup the bisection state"), BISECT_CLEAN_STATE),
		OPT_CMDMODE(0, "check-expected-revs", &cmdmode,
			 N_("check for expected revs"), CHECK_EXPECTED_REVS),
		OPT_CMDMODE(0, "bisect-reset", &cmdmode,
			 N_("reset the bisection state"), BISECT_RESET),
		OPT_CMDMODE(0, "bisect-write", &cmdmode,
			 N_("write out the bisection state in BISECT_LOG"), BISECT_WRITE),
		OPT_BOOL(0, "no-checkout", &no_checkout,
			 N_("update BISECT_HEAD instead of checking out the current commit")),
		OPT_BOOL(0, "no-log", &nolog,
			 N_("no log for BISECT_WRITE ")),
		OPT_END()
	};
	struct bisect_terms terms = { .term_good = NULL, .term_bad = NULL };

	argc = parse_options(argc, argv, prefix, options,
			     git_bisect_helper_usage, 0);

	if (!cmdmode)
		usage_with_options(git_bisect_helper_usage, options);

	switch (cmdmode) {
	case NEXT_ALL:
		return bisect_next_all(prefix, no_checkout);
	case WRITE_TERMS:
		if (argc != 2)
			return error(_("--write-terms requires two arguments"));
		return write_terms(argv[0], argv[1]);
	case BISECT_CLEAN_STATE:
		if (argc != 0)
			return error(_("--bisect-clean-state requires no arguments"));
		return bisect_clean_state();
	case CHECK_EXPECTED_REVS:
		check_expected_revs(argv, argc);
		return 0;
	case BISECT_RESET:
		if (argc > 1)
			return error(_("--bisect-reset requires either no argument or a commit"));
		return !!bisect_reset(argc ? argv[0] : NULL);
	case BISECT_WRITE:
		if (argc != 4 && argc != 5)
			return error(_("--bisect-write requires either 4 or 5 arguments"));
		set_terms(&terms, argv[3], argv[2]);
		res = bisect_write(argv[0], argv[1], &terms, nolog);
		break;
	default:
		return error("BUG: unknown subcommand '%d'", cmdmode);
	}
	free_terms(&terms);
	return !!res;
}
