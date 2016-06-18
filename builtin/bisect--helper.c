#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "bisect.h"
#include "refs.h"
#include "dir.h"
#include "argv-array.h"
#include "run-command.h"

static GIT_PATH_FUNC(git_path_bisect_write_terms, "BISECT_TERMS")
static GIT_PATH_FUNC(git_path_bisect_expected_rev, "BISECT_EXPECTED_REV")
static GIT_PATH_FUNC(git_path_bisect_ancestors_ok, "BISECT_ANCESTORS_OK")
static GIT_PATH_FUNC(git_path_bisect_log, "BISECT_LOG")
static GIT_PATH_FUNC(git_path_bisect_names, "BISECT_NAMES")
static GIT_PATH_FUNC(git_path_bisect_run, "BISECT_RUN")
static GIT_PATH_FUNC(git_path_head_name, "head-name")
static GIT_PATH_FUNC(git_path_bisect_start, "BISECT_START")
static GIT_PATH_FUNC(git_path_bisect_head, "BISECT_HEAD")

static const char * const git_bisect_helper_usage[] = {
	N_("git bisect--helper --next-all [--no-checkout]"),
	N_("git bisect--helper --write-terms <bad_term> <good_term>"),
	N_("git bisect--helper --bisect-clean-state"),
	N_("git bisect--helper --bisect-reset [<commit>]"),
	N_("git bisect--helper --bisect-write <state> <revision> <TERM_GOOD> <TERM_BAD> [<nolog>]"),
	NULL
};

static struct bisect_term {
	char term_good[10];
	char term_bad[10];
} terms;

/*
 * Check whether the string `term` belongs to the set of strings
 * included in the variable arguments.
 */
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
	struct strbuf new_term = STRBUF_INIT;
	strbuf_addf(&new_term, "refs/bisect/%s", term);

	if (check_refname_format(new_term.buf, 0)) {
		strbuf_release(&new_term);
		return error(_("'%s' is not a valid term"), term);
	}
	strbuf_release(&new_term);

	if (one_of(term, "help", "start", "skip", "next", "reset",
			"visualize", "replay", "log", "run", NULL))
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
	FILE *fp;
	int res;

	if (!strcmp(bad, good))
		return error(_("please use two different terms"));

	if (check_term_format(bad, "bad") || check_term_format(good, "good"))
		return -1;

	fp = fopen(git_path_bisect_write_terms(), "w");
	if (!fp)
		return error_errno(_("could not open the file BISECT_TERMS"));

	res = fprintf(fp, "%s\n%s\n", bad, good);
	fclose(fp);
	return (res < 0) ? -1 : 0;
}

static int mark_for_removal(const char *refname, const struct object_id *oid,
			    int flag, void *cb_data)
{
	struct string_list *refs = cb_data;
	char *ref = xstrfmt("refs/bisect/%s", refname);
	string_list_append(refs, ref);
	return 0;
}

static int bisect_clean_state(void)
{
	int result = 0;

	/* There may be some refs packed during bisection */
	struct string_list refs_for_removal = STRING_LIST_INIT_NODUP;
	for_each_ref_in("refs/bisect/", mark_for_removal, (void *) &refs_for_removal);
	string_list_append(&refs_for_removal, xstrdup("BISECT_HEAD"));
	result = delete_refs(&refs_for_removal);
	string_list_clear(&refs_for_removal, 0);
	remove_path(git_path_bisect_expected_rev());
	remove_path(git_path_bisect_ancestors_ok());
	remove_path(git_path_bisect_log());
	remove_path(git_path_bisect_names());
	remove_path(git_path_bisect_run());
	remove_path(git_path_bisect_write_terms());
	/* Cleanup head-name if it got left by an old version of git-bisect */
	remove_path(git_path_head_name());
	/*
	 * Cleanup BISECT_START last to support the --no-checkout option
	 * introduced in the commit 4796e823a.
	 */
	remove_path(git_path_bisect_start());

	return result;
}

static int bisect_reset(const char *commit)
{
	struct strbuf branch = STRBUF_INIT;

	if (!commit) {
		if (strbuf_read_file(&branch, git_path_bisect_start(), 0) < 1) {
			printf("We are not bisecting.\n");
			return 0;
		}
		strbuf_rtrim(&branch);

	} else {
		struct object_id oid;
		if (get_oid(commit, &oid))
			return error(_("'%s' is not a valid commit"), commit);
		strbuf_addf(&branch, "%s", commit);
	}

	if (!file_exists(git_path_bisect_head())) {
		struct argv_array argv = ARGV_ARRAY_INIT;
		argv_array_pushl(&argv, "checkout", branch.buf, "--", NULL);
		if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
			error(_("Could not check out original HEAD '%s'. Try"
					"'git bisect reset <commit>'."), branch.buf);
			strbuf_release(&branch);
			argv_array_clear(&argv);
			return -1;
		}
		argv_array_clear(&argv);
	}

	strbuf_release(&branch);
	return bisect_clean_state();
}

static int is_expected_rev(const char *expected_hex)
{
	struct strbuf actual_hex = STRBUF_INIT;
	int res;

	if (strbuf_read_file(&actual_hex, git_path_bisect_expected_rev(), 0) < 0) {
		strbuf_release(&actual_hex);
		return 0;
	}

	strbuf_trim(&actual_hex);
	res = !strcmp(actual_hex.buf, expected_hex);
	strbuf_release(&actual_hex);
	return res;
}

static int check_expected_revs(const char **revs, int rev_nr)
{
	int i;

	for (i = 0; i < rev_nr; i++) {
		if (!is_expected_rev(revs[i])) {
			remove_path(git_path_bisect_ancestors_ok());
			remove_path(git_path_bisect_expected_rev());
			return 0;
		}
	}
	return 0;
}

static int bisect_write(const char *state, const char *rev,
			const char *term_good, const char *term_bad,
			int nolog)
{
	struct strbuf tag = STRBUF_INIT;
	struct strbuf commit_name = STRBUF_INIT;
	struct object_id oid;
	struct commit *commit;
	struct pretty_print_context pp = {0};
	FILE *fp;

	if (!strcmp(state, term_bad))
		strbuf_addf(&tag, "refs/bisect/%s", state);
	else if(one_of(state, term_good, "skip", NULL))
		strbuf_addf(&tag, "refs/bisect/%s-%s", state, rev);
	else
		return error(_("Bad bisect_write argument: %s"), state);

	if (get_oid(rev, &oid)) {
		strbuf_release(&tag);
		return error(_("couldn't get the oid of the rev '%s'"), rev);
	}

	if (update_ref(NULL, tag.buf, oid.hash, NULL, 0,
		       UPDATE_REFS_MSG_ON_ERR)) {
		strbuf_release(&tag);
		return -1;
	}

	fp = fopen(git_path_bisect_log(), "a");
	if (!fp) {
		strbuf_release(&tag);
		return error_errno(_("couldn't open the file '%s'"), git_path_bisect_log());
	}

	commit = lookup_commit_reference(oid.hash);
	format_commit_message(commit, "%s", &commit_name, &pp);
	fprintf(fp, "# %s: [%s] %s\n", state, sha1_to_hex(oid.hash),
		commit_name.buf);

	if (!nolog)
		fprintf(fp, "git bisect %s %s\n", state, rev);

	strbuf_release(&commit_name);
	strbuf_release(&tag);
	fclose(fp);
	return 0;
}

static int check_and_set_terms(const char *cmd, const char *term_good,
			       const char *term_bad)
{
	if (one_of(cmd, "skip", "start", "terms", NULL))
		return 0;

	if (!is_empty_file(git_path_bisect_write_terms()) &&
	    strcmp(cmd, term_bad) && strcmp(cmd, term_good))
		return error(_("Invalid command: you're currently in a"
				"'%s' '%s' bisect"), term_bad, term_good);

	if (one_of(cmd, "bad", "good", NULL)) {
		if (is_empty_file(git_path_bisect_write_terms())) {
			strcpy(terms.term_bad, "bad");
			strcpy(terms.term_good, "good");
			return write_terms(terms.term_bad, terms.term_good);
		}
	}

	if (one_of(cmd, "new", "old", NULL)) {
		if (is_empty_file(git_path_bisect_write_terms())) {
			strcpy(terms.term_bad, "new");
			strcpy(terms.term_good, "old");
			return write_terms(terms.term_bad, terms.term_good);
		}
	}

	return 0;
}

static int mark_good(const char *refname, const struct object_id *oid,
		     int flag, void *cb_data)
{
	int *missing_good = (int *)cb_data;
	*missing_good = 0;
	printf("missing_good: %d\n", *missing_good);

	return 0;
}

static int bisect_next_check(const char *term, const char *term_good,
			     const char *term_bad)
{
	int missing_good = 1, missing_bad = 1;
	struct strbuf hi = STRBUF_INIT;

	char *bad_ref = xstrfmt("refs/bisect/%s", term_bad);
	if (ref_exists(bad_ref))
		missing_bad = 0;

	free(bad_ref);
	for_each_ref_in("refs/bisect/", mark_good, (void *) &missing_good);

	if (!missing_good && !missing_bad)
		return 0;

	if (missing_good && missing_bad)
		return -1;

	if (missing_good && !missing_bad && !term) {
		struct strbuf yesno = STRBUF_INIT;
		/*
		 * have bad (or new) but not good (or old). We could bisect
		 * although this is less optimum.
		 */
		fprintf(stderr, "Warning: bisecting only with a %s\n", term_bad);
		/*
		 * TRANSLATORS: Make sure to include [Y] and [n] in your
		 * translation. The program will only accept English input
		 * at this point.
		 */
		fprintf(stderr, "Are you sure [Y/n]? ");
		if (strbuf_read(&yesno, 0, 3))
			return error(_("cannot read from standard input"));
		if (starts_with(yesno.buf, "N") || starts_with(yesno.buf, "n"))
			return -1;
	}
	if (strbuf_read_file(&hi, git_path_bisect_start(), 0) > 0) {
		char *bad_syn = xstrdup("bad|new");
		char *good_syn = xstrdup("good|old");
		error(_("You need to give me at least one %s and one %s "
			"revision. You can use \"git bisect %s and %s\" for "
			"that.\n"), bad_syn, good_syn, bad_syn, good_syn);
		free(bad_syn);
		free(good_syn);
		return -1;
	}
	else {
		char *bad_syn = xstrdup("bad|new");
		char *good_syn = xstrdup("good|old");
		error(_("You need to start by \"git bisect start\". You "
			"then need to give me at least one %s and one %s "
			"revision. You can use \"git bisect %s\" and \" "
			"git bisect %s\" for that.\n"), good_syn,
			bad_syn, good_syn, bad_syn);
		free(bad_syn);
		free(good_syn);
		return -1;
	}

	return 0;
}

int cmd_bisect__helper(int argc, const char **argv, const char *prefix)
{
	enum {
		NEXT_ALL = 1,
		WRITE_TERMS,
		BISECT_CLEAN_STATE,
		BISECT_RESET,
		CHECK_EXPECTED_REVS,
		BISECT_WRITE,
		CHECK_AND_SET_TERMS,
		BISECT_NEXT_CHECK
	} cmdmode = 0;
	int no_checkout = 0;
	struct option options[] = {
		OPT_CMDMODE(0, "next-all", &cmdmode,
			 N_("perform 'git bisect next'"), NEXT_ALL),
		OPT_CMDMODE(0, "write-terms", &cmdmode,
			 N_("write the terms to .git/BISECT_TERMS"), WRITE_TERMS),
		OPT_CMDMODE(0, "bisect-clean-state", &cmdmode,
			 N_("cleanup the bisection state"), BISECT_CLEAN_STATE),
		OPT_CMDMODE(0, "bisect-reset", &cmdmode,
			 N_("reset the bisection state"), BISECT_RESET),
		OPT_CMDMODE(0, "check-expected-revs", &cmdmode,
			 N_("check for expected revs"), CHECK_EXPECTED_REVS),
		OPT_CMDMODE(0, "bisect-write", &cmdmode,
			 N_("write out the bisection state in BISECT_LOG"), BISECT_WRITE),
		OPT_CMDMODE(0, "check-and-set-terms", &cmdmode,
			 N_("check and set terms in a bisection state"), CHECK_AND_SET_TERMS),
		OPT_CMDMODE(0, "bisect-next-check", &cmdmode,
			 N_("check whether bad or good terms exist"), BISECT_NEXT_CHECK),
		OPT_BOOL(0, "no-checkout", &no_checkout,
			 N_("update BISECT_HEAD instead of checking out the current commit")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, options,
			     git_bisect_helper_usage, 0);

	if (!cmdmode)
		usage_with_options(git_bisect_helper_usage, options);

	switch (cmdmode) {
	int nolog;
	case NEXT_ALL:
		return bisect_next_all(prefix, no_checkout);
	case WRITE_TERMS:
		if (argc != 2)
			die(_("--write-terms requires two arguments"));
		return write_terms(argv[0], argv[1]);
	case BISECT_CLEAN_STATE:
		if (argc != 0)
			die(_("--bisect-clean-state requires no arguments"));
		return bisect_clean_state();
	case BISECT_RESET:
		if (argc > 1)
			die(_("--bisect-reset requires either zero or one arguments"));
		return bisect_reset(argc ? argv[0] : NULL);
	case CHECK_EXPECTED_REVS:
		return check_expected_revs(argv, argc);
	case BISECT_WRITE:
		if (argc != 4 && argc != 5)
			die(_("--bisect-write requires either 4 or 5 arguments"));
		nolog = (argc == 5) && !strcmp(argv[4], "nolog");
		return bisect_write(argv[0], argv[1], argv[2], argv[3], nolog);
	case CHECK_AND_SET_TERMS:
		if (argc != 3)
			die(_("--check-and-set-terms requires 3 arguments"));
		return check_and_set_terms(argv[0], argv[1], argv[2]);
	case BISECT_NEXT_CHECK:
		if (!(argc == 2 || argc == 3))
			die(_("--bisect-next-check requires 2 or 3 arguments"));
		if (argc == 2)
			return bisect_next_check(NULL, argv[0], argv[1]);
		return bisect_next_check(argv[0], argv[1], argv[2]);
	default:
		die("BUG: unknown subcommand '%d'", cmdmode);
	}
	return 0;
}
