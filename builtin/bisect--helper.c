#include "builtin.h"
#include "cache.h"
#include "parse-options.h"
#include "bisect.h"
#include "refs.h"
#include "dir.h"
#include "argv-array.h"
#include "run-command.h"
#include "prompt.h"
#include "quote.h"
#include "revision.h"

static GIT_PATH_FUNC(git_path_bisect_terms, "BISECT_TERMS")
static GIT_PATH_FUNC(git_path_bisect_expected_rev, "BISECT_EXPECTED_REV")
static GIT_PATH_FUNC(git_path_bisect_ancestors_ok, "BISECT_ANCESTORS_OK")
static GIT_PATH_FUNC(git_path_bisect_log, "BISECT_LOG")
static GIT_PATH_FUNC(git_path_bisect_start, "BISECT_START")
static GIT_PATH_FUNC(git_path_bisect_head, "BISECT_HEAD")
static GIT_PATH_FUNC(git_path_head_name, "head-name")
static GIT_PATH_FUNC(git_path_bisect_names, "BISECT_NAMES")

static const char * const git_bisect_helper_usage[] = {
	N_("git bisect--helper --bisect-reset [<commit>]"),
	N_("git bisect--helper --bisect-check-and-set-terms <command> <TERM_GOOD> <TERM_BAD>"),
	N_("git bisect--helper --bisect-next-check [<term>] <TERM_GOOD> <TERM_BAD"),
	N_("git bisect--helper --bisect-terms [--term-good | --term-old | --term-bad | --term-new]"),
	N_("git bisect--helper --bisect start [--term-{old,good}=<term> --term-{new,bad}=<term>]"
					      "[--no-checkout] [<bad> [<good>...]] [--] [<paths>...]"),
	N_("git bisect--helper --bisect-next"),
	N_("git bisect--helper --bisect-state (bad|new) [<rev>]"),
	N_("git bisect--helper --bisect-state (good|old) [<rev>...]"),
	N_("git bisect--helper --bisect-replay <filename>"),
	NULL
};

struct bisect_terms {
	const char *term_good;
	const char *term_bad;
};

static int bisect_autostart(struct bisect_terms *terms);

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
		unsigned char sha1[20];
		if (get_sha1_committish(commit, sha1))
			return error(_("'%s' is not a valid commit"), commit);
		strbuf_addstr(&branch, commit);
	}

	if (!file_exists(git_path_bisect_head())) {
		struct argv_array argv = ARGV_ARRAY_INIT;
		argv_array_pushl(&argv, "checkout", branch.buf, "--", NULL);
		if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
			error(_("Could not check out original HEAD '%s'. Try "
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
	int res = 0;
	if (strbuf_read_file(&actual_hex, git_path_bisect_expected_rev(), 0) >= 40) {
		strbuf_trim(&actual_hex);
		res = !strcmp(actual_hex.buf, expected_hex);
	}
	strbuf_release(&actual_hex);
	return res;
}

static int check_expected_revs(const char **revs, int rev_nr)
{
	int i;

	for (i = 0; i < rev_nr; i++) {
		if (!is_expected_rev(revs[i])) {
			unlink_or_warn(git_path_bisect_ancestors_ok());
			unlink_or_warn(git_path_bisect_expected_rev());
			return 0;
		}
	}
	return 0;
}

static int bisect_write(const char *state, const char *rev,
			const struct bisect_terms *terms, int nolog)
{
	struct strbuf tag = STRBUF_INIT;
	struct strbuf commit_name = STRBUF_INIT;
	struct object_id oid;
	struct commit *commit;
	struct pretty_print_context pp = {0};
	FILE *fp = NULL;
	int retval = 0;

	if (!strcmp(state, terms->term_bad))
		strbuf_addf(&tag, "refs/bisect/%s", state);
	else if (one_of(state, terms->term_good, "skip", NULL))
		strbuf_addf(&tag, "refs/bisect/%s-%s", state, rev);
	else {
		error(_("Bad bisect_write argument: %s"), state);
		retval = -1;
		goto finish;
	}

	if (get_oid(rev, &oid)) {
		error(_("couldn't get the oid of the rev '%s'"), rev);
		retval = -1;
		goto finish;
	}

	if (update_ref(NULL, tag.buf, oid.hash, NULL, 0,
		       UPDATE_REFS_MSG_ON_ERR)) {
		retval = -1;
		goto finish;
	}

	fp = fopen(git_path_bisect_log(), "a");
	if (!fp) {
		error_errno(_("couldn't open the file '%s'"), git_path_bisect_log());
		retval = -1;
		goto finish;
	}

	commit = lookup_commit_reference(oid.hash);
	format_commit_message(commit, "%s", &commit_name, &pp);
	fprintf(fp, "# %s: [%s] %s\n", state, sha1_to_hex(oid.hash),
		commit_name.buf);

	if (!nolog)
		fprintf(fp, "git bisect %s %s\n", state, rev);

	goto finish;
finish:
	if (fp)
		fclose(fp);
	strbuf_release(&tag);
	strbuf_release(&commit_name);
	return retval;
}

static int set_terms(struct bisect_terms *terms, const char *bad,
		     const char *good)
{
	terms->term_good = xstrdup(good);
	terms->term_bad = xstrdup(bad);
	return write_terms(terms->term_bad, terms->term_good);
}

static int check_and_set_terms(struct bisect_terms *terms, const char *cmd)
{
	int has_term_file = !is_empty_or_missing_file(git_path_bisect_terms());

	if (one_of(cmd, "skip", "start", "terms", NULL))
		return 0;

	if (has_term_file &&
	    strcmp(cmd, terms->term_bad) &&
	    strcmp(cmd, terms->term_good))
		return error(_("Invalid command: you're currently in a "
				"%s/%s bisect"), terms->term_bad,
				terms->term_good);

	if (!has_term_file) {
		if (one_of(cmd, "bad", "good", NULL))
			return set_terms(terms, "bad", "good");
		if (one_of(cmd, "new", "old", NULL))
			return set_terms(terms, "new", "old");
	}

	return 0;
}

static int mark_good(const char *refname, const struct object_id *oid,
		     int flag, void *cb_data)
{
	int *m_good = (int *)cb_data;
	*m_good = 0;
	return 1;
}

static char *bisect_voc(char *revision_type)
{
	if (!strcmp(revision_type, "bad"))
		return "bad|new";
	if (!strcmp(revision_type, "good"))
		return "good|old";

	return NULL;
}

static int bisect_next_check(const struct bisect_terms *terms,
			     const char *current_term)
{
	int missing_good = 1, missing_bad = 1, retval = 0;
	char *bad_ref = xstrfmt("refs/bisect/%s", terms->term_bad);
	char *good_glob = xstrfmt("%s-*", terms->term_good);
	char *bad_syn = NULL, *good_syn = NULL;

	if (ref_exists(bad_ref))
		missing_bad = 0;

	for_each_glob_ref_in(mark_good, good_glob, "refs/bisect/",
			     (void *) &missing_good);

	if (!missing_good && !missing_bad)
		goto finish;

	if (!current_term) {
		retval = -1;
		goto finish;
	}

	if (missing_good && !missing_bad && current_term &&
	    !strcmp(current_term, terms->term_good)) {
		char *yesno;
		/*
		 * have bad (or new) but not good (or old). We could bisect
		 * although this is less optimum.
		 */
		fprintf(stderr, _("Warning: bisecting only with a %s commit\n"),
			terms->term_bad);
		if (!isatty(0))
			goto finish;
		/*
		 * TRANSLATORS: Make sure to include [Y] and [n] in your
		 * translation. The program will only accept English input
		 * at this point.
		 */
		yesno = git_prompt(_("Are you sure [Y/n]? "), PROMPT_ECHO);
		if (starts_with(yesno, "N") || starts_with(yesno, "n")) {
			retval = -1;
			goto finish;
		}

		goto finish;
	}
	bad_syn = xstrdup(bisect_voc("bad"));
	good_syn = xstrdup(bisect_voc("good"));
	if (!is_empty_or_missing_file(git_path_bisect_start())) {
		error(_("You need to give me at least one %s and "
			"%s revision. You can use \"git bisect %s\" "
			"and \"git bisect %s\" for that. \n"),
			bad_syn, good_syn, bad_syn, good_syn);
		retval = -1;
		goto finish;
	}
	else {
		error(_("You need to start by \"git bisect start\". You "
			"then need to give me at least one %s and %s "
			"revision. You can use \"git bisect %s\" and "
			"\"git bisect %s\" for that.\n"),
			good_syn, bad_syn, bad_syn, good_syn);
		retval = -1;
		goto finish;
	}
	goto finish;
finish:
	free(bad_ref);
	free(good_glob);
	free(bad_syn);
	free(good_syn);
	return retval;
}

static int get_terms(struct bisect_terms *terms)
{
	struct strbuf str = STRBUF_INIT;
	FILE *fp = NULL;
	int res = 0;
	fp = fopen(git_path_bisect_terms(), "r");
	if (!fp) {
		res = -1;
		goto finish;
	}
	strbuf_getline_lf(&str, fp);
	terms->term_bad = strbuf_detach(&str, NULL);
	strbuf_getline_lf(&str, fp);
	terms->term_good = strbuf_detach(&str, NULL);
	goto finish;
finish:
	if (fp)
		fclose(fp);
	strbuf_release(&str);
	return res;
}

static int bisect_terms(struct bisect_terms *terms, const char **argv, int argc)
{
	int i;
	const char bisect_term_usage[] =
"git bisect--helper --bisect-terms [--term-good | --term-bad | ]"
"--term-old | --term-new";

	if (get_terms(terms))
		return error(_("no terms defined"));

	if (argc > 1) {
		usage(bisect_term_usage);
		return -1;
	}

	if (argc == 0) {
		printf(_("Your current terms are %s for the old state\nand "
		       "%s for the new state.\n"), terms->term_good,
		       terms->term_bad);
		return 0;
	}

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--term-good"))
			printf("%s\n", terms->term_good);
		else if (!strcmp(argv[i], "--term-bad"))
			printf("%s\n", terms->term_bad);
		else
			die(_("invalid argument %s for 'git bisect "
				  "terms'.\nSupported options are: "
				  "--term-good|--term-old and "
				  "--term-bad|--term-new."), argv[i]);
	}

	return 0;
}

static int register_good_ref(const char *refname,
			     const struct object_id *oid, int flags,
			     void *cb_data)
{
	struct string_list *good_refs = cb_data;
	string_list_append(good_refs, oid_to_hex(oid));
	return 0;
}

static int bisect_next(struct bisect_terms *terms, const char *prefix)
{
	int res, no_checkout;

	bisect_autostart(terms);
	/*
	 * In case of mistaken revs or checkout error, or signals received,
	 * "bisect_auto_next" below may exit or misbehave.
	 * We have to trap this to be able to clean up using
	 * "bisect_clean_state".
	 */
	if (bisect_next_check(terms, terms->term_good))
		return -1;

	no_checkout = !is_empty_or_missing_file(git_path_bisect_head());

	/* Perform all bisection computation, display and checkout */
	res = bisect_next_all(prefix , no_checkout);

	if (res == 10) {
		FILE *fp = NULL;
		unsigned char sha1[20];
		struct commit *commit;
		struct pretty_print_context pp = {0};
		struct strbuf commit_name = STRBUF_INIT;
		char *bad_ref = xstrfmt("refs/bisect/%s",
					      terms->term_bad);
		int retval = 0;

		read_ref(bad_ref, sha1);
		commit = lookup_commit_reference(sha1);
		format_commit_message(commit, "%s", &commit_name, &pp);
		fp = fopen(git_path_bisect_log(), "a");
		if (!fp) {
			retval = -1;
			goto finish_10;
		}
		if (fprintf(fp, "# first %s commit: [%s] %s\n",
			    terms->term_bad, sha1_to_hex(sha1),
			    commit_name.buf) < 1){
			retval = -1;
			goto finish_10;
		}
		goto finish_10;
	finish_10:
		if (fp)
			fclose(fp);
		strbuf_release(&commit_name);
		free(bad_ref);
		return retval;
	}
	else if (res == 2) {
		FILE *fp = NULL;
		struct rev_info revs;
		struct argv_array rev_argv = ARGV_ARRAY_INIT;
		struct string_list good_revs = STRING_LIST_INIT_DUP;
		struct pretty_print_context pp = {0};
		struct commit *commit;
		char *term_good = xstrfmt("%s-*", terms->term_good);
		int i, retval = 0;

		fp = fopen(git_path_bisect_log(), "a");
		if (!fp) {
			retval = -1;
			goto finish_2;
		}
		if (fprintf(fp, "# only skipped commits left to test\n") < 1) {
			retval = -1;
			goto finish_2;
		}
		for_each_glob_ref_in(register_good_ref, term_good,
				     "refs/bisect/", (void *) &good_revs);

		argv_array_pushl(&rev_argv, "skipped_commits", "refs/bisect/bad", "--not", NULL);
		for (i = 0; i < good_revs.nr; i++)
			argv_array_push(&rev_argv, good_revs.items[i].string);

		/* It is important to reset the flags used by revision walks
		 * as the previous call to bisect_next_all() in turn
		 * setups a revision walk.
		 */
		reset_revision_walk();
		init_revisions(&revs, NULL);
		rev_argv.argc = setup_revisions(rev_argv.argc, rev_argv.argv, &revs, NULL);
		argv_array_clear(&rev_argv);
		string_list_clear(&good_revs, 0);
		if (prepare_revision_walk(&revs))
			die(_("revision walk setup failed\n"));

		while ((commit = get_revision(&revs)) != NULL) {
			struct strbuf commit_name = STRBUF_INIT;
			format_commit_message(commit, "%s",
					      &commit_name, &pp);
			fprintf(fp, "# possible first %s commit: "
				    "[%s] %s\n", terms->term_bad,
				    oid_to_hex(&commit->object.oid),
				    commit_name.buf);
			strbuf_release(&commit_name);
		}
		goto finish_2;
	finish_2:
		if (fp)
			fclose(fp);
		string_list_clear(&good_revs, 0);
		argv_array_clear(&rev_argv);
		free(term_good);
		if (retval)
			return retval;
		else
			return res;
	}
	return res;
}

static int bisect_auto_next(struct bisect_terms *terms, const char *prefix)
{
	if (!bisect_next_check(terms, NULL))
		return bisect_next(terms, prefix);

	return 0;
}

static int bisect_start(struct bisect_terms *terms, int no_checkout,
			const char **argv, int argc)
{
	int i, has_double_dash = 0, must_write_terms = 0, bad_seen = 0;
	int flags, pathspec_pos, retval = 0;
	struct string_list revs = STRING_LIST_INIT_DUP;
	struct string_list states = STRING_LIST_INIT_DUP;
	struct strbuf start_head = STRBUF_INIT;
	struct strbuf bisect_names = STRBUF_INIT;
	struct strbuf orig_args = STRBUF_INIT;
	const char *head;
	unsigned char sha1[20];
	FILE *fp = NULL;
	struct object_id oid;

	if (is_bare_repository())
		no_checkout = 1;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--")) {
			has_double_dash = 1;
			break;
		}
	}

	for (i = 0; i < argc; i++) {
		const char  *commit_id;
		const char *arg = argv[i];
		commit_id = xstrfmt("%s^{commit}", argv[i]);
		if (!strcmp(argv[i], "--")) {
			has_double_dash = 1;
			break;
		} else if (!strcmp(arg, "--no-checkout")) {
			no_checkout = 1;
		} else if (!strcmp(arg, "--term-good") ||
			 !strcmp(arg, "--term-old")) {
			must_write_terms = 1;
			terms->term_good = xstrdup(argv[++i]);
		} else if (skip_prefix(arg, "--term-good=", &arg)) {
			must_write_terms = 1;
			terms->term_good = arg;
		} else if (skip_prefix(arg, "--term-old=", &arg)) {
			must_write_terms = 1;
			terms->term_good = arg;
		} else if (!strcmp(arg, "--term-bad") ||
			 !strcmp(arg, "--term-new")) {
			terms->term_bad = xstrdup(argv[++i]);
			must_write_terms = 1;
		} else if (skip_prefix(arg, "--term-bad=", &arg)) {
			must_write_terms = 1;
			terms->term_bad = arg;
		} else if (skip_prefix(arg, "--term-new=", &arg)) {
			must_write_terms = 1;
			terms->term_good = arg;
		} else if (starts_with(arg, "--") &&
			 !one_of(arg, "--term-good", "--term-bad", NULL)) {
			die(_("unrecognised option: '%s'"), arg);
		} else if (get_oid(commit_id, &oid) && has_double_dash) {
			die(_("'%s' does not appear to be a valid revision"), argv[i]);
		} else {
			string_list_append(&revs, oid_to_hex(&oid));
		}
	}
	pathspec_pos = i;

	/*
	 * The user ran "git bisect start <sha1> <sha1>", hence did not
	 * explicitly specify the terms, but we are already starting to
	 * set references named with the default terms, and won't be able
	 * to change afterwards.
	 */
	must_write_terms |= !!revs.nr;
	for (i = 0; i < revs.nr; i++) {
		if (bad_seen) {
			string_list_append(&states, terms->term_good);
		} else {
			bad_seen = 1;
			string_list_append(&states, terms->term_bad);
		}
	}

	/*
	 * Verify HEAD
	 */
	head = resolve_ref_unsafe("HEAD", 0, sha1, &flags);
	if (!head)
		if (get_sha1("HEAD", sha1))
			die(_("Bad HEAD - I need a HEAD"));

	if (!is_empty_or_missing_file(git_path_bisect_start())) {
		/* Reset to the rev from where we started */
		strbuf_read_file(&start_head, git_path_bisect_start(), 0);
		strbuf_trim(&start_head);
		if (!no_checkout) {
			struct argv_array argv = ARGV_ARRAY_INIT;
			argv_array_pushl(&argv, "checkout", start_head.buf,
					 "--", NULL);
			if (run_command_v_opt(argv.argv, RUN_GIT_CMD)) {
				error(_("checking out '%s' failed. Try 'git "
					"bisect start <valid-branch>'."),
				      start_head.buf);
				retval = -1;
				goto finish;
			}
		}
	} else {
		if (!get_sha1(head, sha1) &&
		    !starts_with(head, "refs/heads/")) {
			strbuf_reset(&start_head);
			strbuf_addstr(&start_head, sha1_to_hex(sha1));
		} else if (!get_sha1(head, sha1) &&
			   skip_prefix(head, "refs/heads/", &head)) {
			/*
			 * This error message should only be triggered by
			 * cogito usage, and cogito users should understand
			 * it relates to cg-seek.
			 */
			if (!is_empty_or_missing_file(git_path_head_name()))
				die(_("won't bisect on cg-seek'ed tree"));
			strbuf_addstr(&start_head, head);
		} else {
			die(_("Bad HEAD - strange symbolic ref"));
		}
	}

	/*
	 * Get rid of any old bisect state.
	 */
	if (bisect_clean_state()) {
		return -1;
	}
	/*
	 * In case of mistaken revs or checkout error, or signals received,
	 * "bisect_auto_next" below may exit or misbehave.
	 * We have to trap this to be able to clean up using
	 * "bisect_clean_state".
	 */

	/*
	 * Write new start state
	 */
	write_file(git_path_bisect_start(), "%s\n", start_head.buf);

	if (no_checkout) {
		get_oid(start_head.buf, &oid);
		if (update_ref(NULL, "BISECT_HEAD", oid.hash, NULL, 0,
			       UPDATE_REFS_MSG_ON_ERR)) {
			retval = -1;
			goto finish;
		}
	}

	if (pathspec_pos < argc - 1)
		sq_quote_argv(&bisect_names, argv + pathspec_pos, 0);
	write_file(git_path_bisect_names(), "%s\n", bisect_names.buf);

	for (i = 0; i < states.nr; i++) {
		if (bisect_write(states.items[i].string,
				 revs.items[i].string, terms, 1)) {
			retval = -1;
			goto finish;
		}
	}

	if (must_write_terms)
		if (write_terms(terms->term_bad, terms->term_good)) {
			retval = -1;
			goto finish;
		}

	fp = fopen(git_path_bisect_log(), "a");
	if (!fp)
		return -1;

	if (fprintf(fp, "git bisect start") < 1) {
		retval = -1;
		goto finish;
	}

	sq_quote_argv(&orig_args, argv, 0);
	if (fprintf(fp, "%s", orig_args.buf) < 0) {
		retval = -1;
		goto finish;
	}
	if (fprintf(fp, "\n") < 1) {
		retval = -1;
		goto finish;
	}
	goto finish;
finish:
	if (fp)
		fclose(fp);
	string_list_clear(&revs, 0);
	string_list_clear(&states, 0);
	strbuf_release(&start_head);
	strbuf_release(&bisect_names);
	strbuf_release(&orig_args);
	return retval || bisect_auto_next(terms, NULL);
}

static int bisect_autostart(struct bisect_terms *terms)
{
	if (is_empty_or_missing_file(git_path_bisect_start())) {
		const char *yesno;
		const char *argv[] = {NULL};
		fprintf(stderr, _("You need to start by \"git bisect "
				  "start\"\n"));

		if (!isatty(0))
			return 1;

		/*
		 * TRANSLATORS: Make sure to include [Y] and [n] in your
		 * translation. THe program will only accept English input
		 * at this point.
		 */
		yesno = git_prompt(_("Do you want me to do it for you "
				     "[Y/n]? "), PROMPT_ECHO);
		if (starts_with(yesno, "n") || starts_with(yesno, "N"))
			exit(0);

		return bisect_start(terms, 0, argv, 0);
	}
	return 0;
}

static char *bisect_head(void)
{
	if (is_empty_or_missing_file(git_path_bisect_head()))
		return "HEAD";
	else
		return "BISECT_HEAD";
}

static int bisect_state(struct bisect_terms *terms, const char **argv,
			int argc)
{
	const char *state = argv[0];

	get_terms(terms);
	if (check_and_set_terms(terms, state))
		return -1;

	if (!argc)
		die(_("Please call `--bisect-state` with at least one argument"));

	if (argc == 1 && one_of(state, terms->term_good,
	    terms->term_bad, "skip", NULL)) {
		const char *bisected_head = xstrdup(bisect_head());
		const char *hex[1];
		unsigned char sha1[20];

		if (get_sha1(bisected_head, sha1))
			die(_("Bad rev input: %s"), bisected_head);
		if (bisect_write(state, sha1_to_hex(sha1), terms, 0))
			return -1;

		*hex = xstrdup(sha1_to_hex(sha1));
		if (check_expected_revs(hex, 1))
			return -1;
		return bisect_auto_next(terms, NULL);
	}

	if ((argc == 2 && !strcmp(state, terms->term_bad)) ||
			one_of(state, terms->term_good, "skip", NULL)) {
		int i;
		struct string_list hex = STRING_LIST_INIT_DUP;

		for (i = 1; i < argc; i++) {
			unsigned char sha1[20];

			if (get_sha1(argv[i], sha1)) {
				string_list_clear(&hex, 0);
				die(_("Bad rev input: %s"), argv[i]);
			}
			string_list_append(&hex, sha1_to_hex(sha1));
		}
		for (i = 0; i < hex.nr; i++) {
			const char **hex_string = (const char **) &hex.items[i].string;
			if(bisect_write(state, *hex_string, terms, 0)) {
				string_list_clear(&hex, 0);
				return -1;
			}
			if (check_expected_revs(hex_string, 1)) {
				string_list_clear(&hex, 0);
				return -1;
			}
		}
		string_list_clear(&hex, 0);
		return bisect_auto_next(terms, NULL);
	}

	if (!strcmp(state, terms->term_bad))
		die(_("'git bisect %s' can take only one argument."),
		      terms->term_bad);

	return -1;
}

static int bisect_log(void)
{
	int fd, status;
	fd = open(git_path_bisect_log(), O_RDONLY);
	if (fd < 0)
		return -1;

	status = copy_fd(fd, 1);
	if (status) {
		close(fd);
		return -1;
	}

	close(fd);
	return status;
}

static int get_next_word(const char *line, int pos, struct strbuf *word)
{
	int i, len = strlen(line), begin = 0;
	strbuf_reset(word);
	for (i = pos; i < len; i++) {
		if (line[i] == ' ' && begin)
			return i + 1;

		if (!begin)
			begin = 1;
		strbuf_addch(word, line[i]);
	}

	return i;
}

static int bisect_replay(struct bisect_terms *terms, const char *filename)
{
	struct strbuf line = STRBUF_INIT;
	struct strbuf word = STRBUF_INIT;
	FILE *fp = NULL;
	int res = 0;

	if (is_empty_or_missing_file(filename)) {
		error(_("no such file with name '%s' exists"), filename);
		res = -1;
		goto finish;
	}

	if (bisect_reset(NULL)) {
		res = -1;
		goto finish;
	}

	fp = fopen(filename, "r");
	if (!fp) {
		res = -1;
		goto finish;
	}

	while (strbuf_getline(&line, fp) != EOF) {
		int pos = 0;
		while (pos < line.len) {
			pos = get_next_word(line.buf, pos, &word);

			if (!strcmp(word.buf, "git")) {
				continue;
			} else if (!strcmp(word.buf, "git-bisect")) {
				continue;
			} else if (!strcmp(word.buf, "bisect")) {
				continue;
			} else if (!strcmp(word.buf, "#")) {
				break;
			}

			get_terms(terms);
			if (check_and_set_terms(terms, word.buf)) {
				res = -1;
				goto finish;
			}

			if (!strcmp(word.buf, "start")) {
				struct argv_array argv = ARGV_ARRAY_INIT;
				sq_dequote_to_argv_array(line.buf+pos, &argv);
				if (bisect_start(terms, 0, argv.argv, argv.argc)) {
					argv_array_clear(&argv);
					res = -1;
					goto finish;
				}
				argv_array_clear(&argv);
				break;
			}

			if (one_of(word.buf, terms->term_good,
			    terms->term_bad, "skip", NULL)) {
				if (bisect_write(word.buf, line.buf+pos, terms, 0)) {
					res = -1;
					goto finish;
				}
				break;
			}

			if (!strcmp(word.buf, "terms")) {
				struct argv_array argv = ARGV_ARRAY_INIT;
				sq_dequote_to_argv_array(line.buf+pos, &argv);
				if (bisect_terms(terms, argv.argv, argv.argc)) {
					argv_array_clear(&argv);
					res = -1;
					goto finish;
				}
				argv_array_clear(&argv);
				break;
			}

			error(_("?? what are you talking about?"));
			res = -1;
			goto finish;
		}
	}
	goto finish;
finish:
	if (fp)
		fclose(fp);
	strbuf_release(&line);
	strbuf_release(&word);
	if (res)
		return -1;

	return bisect_auto_next(terms, NULL);
}

int cmd_bisect__helper(int argc, const char **argv, const char *prefix)
{
	enum {
		BISECT_RESET = 1,
		CHECK_AND_SET_TERMS,
		BISECT_NEXT_CHECK,
		BISECT_TERMS,
		BISECT_START,
		BISECT_NEXT,
		BISECT_STATE,
		BISECT_LOG,
		BISECT_REPLAY
	} cmdmode = 0;
	int no_checkout = 0, res = 0;
	struct option options[] = {
		OPT_CMDMODE(0, "bisect-reset", &cmdmode,
			 N_("reset the bisection state"), BISECT_RESET),
		OPT_CMDMODE(0, "check-and-set-terms", &cmdmode,
			 N_("check and set terms in a bisection state"), CHECK_AND_SET_TERMS),
		OPT_CMDMODE(0, "bisect-next-check", &cmdmode,
			 N_("check whether bad or good terms exist"), BISECT_NEXT_CHECK),
		OPT_CMDMODE(0, "bisect-terms", &cmdmode,
			 N_("print out the bisect terms"), BISECT_TERMS),
		OPT_CMDMODE(0, "bisect-start", &cmdmode,
			 N_("start the bisect session"), BISECT_START),
		OPT_CMDMODE(0, "bisect-next", &cmdmode,
			 N_("find the next bisection commit"), BISECT_NEXT),
		OPT_CMDMODE(0, "bisect-state", &cmdmode,
			 N_("mark the state of ref (or refs)"), BISECT_STATE),
		OPT_CMDMODE(0, "bisect-log", &cmdmode,
			 N_("output the contents of BISECT_LOG"), BISECT_LOG),
		OPT_CMDMODE(0, "bisect-replay", &cmdmode,
			 N_("replay the bisection process from the given file"), BISECT_REPLAY),
		OPT_BOOL(0, "no-checkout", &no_checkout,
			 N_("update BISECT_HEAD instead of checking out the current commit")),
		OPT_END()
	};
	struct bisect_terms terms;

	argc = parse_options(argc, argv, prefix, options,
			     git_bisect_helper_usage,
			     PARSE_OPT_KEEP_DASHDASH | PARSE_OPT_KEEP_UNKNOWN);

	if (!cmdmode)
		usage_with_options(git_bisect_helper_usage, options);

	switch (cmdmode) {
	case BISECT_RESET:
		if (argc > 1)
			die(_("--bisect-reset requires either zero or one arguments"));
		res = bisect_reset(argc ? argv[0] : NULL);
		break;
	case CHECK_AND_SET_TERMS:
		if (argc != 3)
			die(_("--check-and-set-terms requires 3 arguments"));
		terms.term_good = xstrdup(argv[1]);
		terms.term_bad = xstrdup(argv[2]);
		res = check_and_set_terms(&terms, argv[0]);
		break;
	case BISECT_NEXT_CHECK:
		if (argc != 2 && argc != 3)
			die(_("--bisect-next-check requires 2 or 3 arguments"));
		terms.term_good = xstrdup(argv[0]);
		terms.term_bad = xstrdup(argv[1]);
		res = bisect_next_check(&terms, argc == 3 ? argv[2] : NULL);
		break;
	case BISECT_TERMS:
		if (argc > 1)
			die(_("--bisect-terms requires 0 or 1 argument"));
		res = bisect_terms(&terms, argv, argc);
		break;
	case BISECT_START:
		terms.term_good = "good";
		terms.term_bad = "bad";
		res = bisect_start(&terms, no_checkout, argv, argc);
		break;
	case BISECT_NEXT:
		if (argc)
			die(_("--bisect-next requires 0 arguments"));
		get_terms(&terms);
		res = bisect_next(&terms, prefix);
		break;
	case BISECT_STATE:
		if (argc == 0)
			die(_("--bisect-state requires at least 1 argument"));
		terms.term_good = "good";
		terms.term_bad = "bad";
		get_terms(&terms);
		res = bisect_state(&terms, argv, argc);
		break;
	case BISECT_LOG:
		if (argc > 1)
			die(_("--bisect-log requires 0 arguments"));
		res = bisect_log();
		break;
	case BISECT_REPLAY:
		if (argc != 1)
			die(_("--bisect-replay requires 1 argument"));
		terms.term_good = "good";
		terms.term_bad = "bad";
		res = bisect_replay(&terms, argv[0]);
		break;
	default:
		die("BUG: unknown subcommand '%d'", cmdmode);
	}
	return res;
}
