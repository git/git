#include "builtin.h"
#include "cache.h"
#include "hex.h"
#include "parse-options.h"
#include "bisect.h"
#include "refs.h"
#include "dir.h"
#include "strvec.h"
#include "run-command.h"
#include "prompt.h"
#include "quote.h"
#include "revision.h"

static GIT_PATH_FUNC(git_path_bisect_terms, "BISECT_TERMS")
static GIT_PATH_FUNC(git_path_bisect_expected_rev, "BISECT_EXPECTED_REV")
static GIT_PATH_FUNC(git_path_bisect_ancestors_ok, "BISECT_ANCESTORS_OK")
static GIT_PATH_FUNC(git_path_bisect_start, "BISECT_START")
static GIT_PATH_FUNC(git_path_bisect_log, "BISECT_LOG")
static GIT_PATH_FUNC(git_path_bisect_names, "BISECT_NAMES")
static GIT_PATH_FUNC(git_path_bisect_first_parent, "BISECT_FIRST_PARENT")
static GIT_PATH_FUNC(git_path_bisect_run, "BISECT_RUN")

#define BUILTIN_GIT_BISECT_START_USAGE \
	N_("git bisect start [--term-{new,bad}=<term> --term-{old,good}=<term>]" \
	   "    [--no-checkout] [--first-parent] [<bad> [<good>...]] [--]" \
	   "    [<pathspec>...]")
#define BUILTIN_GIT_BISECT_STATE_USAGE \
	N_("git bisect (good|bad) [<rev>...]")
#define BUILTIN_GIT_BISECT_TERMS_USAGE \
	"git bisect terms [--term-good | --term-bad]"
#define BUILTIN_GIT_BISECT_SKIP_USAGE \
	N_("git bisect skip [(<rev>|<range>)...]")
#define BUILTIN_GIT_BISECT_NEXT_USAGE \
	"git bisect next"
#define BUILTIN_GIT_BISECT_RESET_USAGE \
	N_("git bisect reset [<commit>]")
#define BUILTIN_GIT_BISECT_VISUALIZE_USAGE \
	"git bisect visualize"
#define BUILTIN_GIT_BISECT_REPLAY_USAGE \
	N_("git bisect replay <logfile>")
#define BUILTIN_GIT_BISECT_LOG_USAGE \
	"git bisect log"
#define BUILTIN_GIT_BISECT_RUN_USAGE \
	N_("git bisect run <cmd>...")

static const char * const git_bisect_usage[] = {
	BUILTIN_GIT_BISECT_START_USAGE,
	BUILTIN_GIT_BISECT_STATE_USAGE,
	BUILTIN_GIT_BISECT_TERMS_USAGE,
	BUILTIN_GIT_BISECT_SKIP_USAGE,
	BUILTIN_GIT_BISECT_NEXT_USAGE,
	BUILTIN_GIT_BISECT_RESET_USAGE,
	BUILTIN_GIT_BISECT_VISUALIZE_USAGE,
	BUILTIN_GIT_BISECT_REPLAY_USAGE,
	BUILTIN_GIT_BISECT_LOG_USAGE,
	BUILTIN_GIT_BISECT_RUN_USAGE,
	NULL
};

struct add_bisect_ref_data {
	struct rev_info *revs;
	unsigned int object_flags;
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

static const char vocab_bad[] = "bad|new";
static const char vocab_good[] = "good|old";

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

/*
 * return code BISECT_INTERNAL_SUCCESS_MERGE_BASE
 * and BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND are codes
 * that indicate special success.
 */

static int is_bisect_success(enum bisect_error res)
{
	return !res ||
		res == BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND ||
		res == BISECT_INTERNAL_SUCCESS_MERGE_BASE;
}

static int write_in_file(const char *path, const char *mode, const char *format, va_list args)
{
	FILE *fp = NULL;
	int res = 0;

	if (strcmp(mode, "w") && strcmp(mode, "a"))
		BUG("write-in-file does not support '%s' mode", mode);
	fp = fopen(path, mode);
	if (!fp)
		return error_errno(_("cannot open file '%s' in mode '%s'"), path, mode);
	res = vfprintf(fp, format, args);

	if (res < 0) {
		int saved_errno = errno;
		fclose(fp);
		errno = saved_errno;
		return error_errno(_("could not write to file '%s'"), path);
	}

	return fclose(fp);
}

__attribute__((format (printf, 2, 3)))
static int write_to_file(const char *path, const char *format, ...)
{
	int res;
	va_list args;

	va_start(args, format);
	res = write_in_file(path, "w", format, args);
	va_end(args);

	return res;
}

__attribute__((format (printf, 2, 3)))
static int append_to_file(const char *path, const char *format, ...)
{
	int res;
	va_list args;

	va_start(args, format);
	res = write_in_file(path, "a", format, args);
	va_end(args);

	return res;
}

static int print_file_to_stdout(const char *path)
{
	int fd = open(path, O_RDONLY);
	int ret = 0;

	if (fd < 0)
		return error_errno(_("cannot open file '%s' for reading"), path);
	if (copy_fd(fd, 1) < 0)
		ret = error_errno(_("failed to read '%s'"), path);
	close(fd);
	return ret;
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
	int res;

	if (!strcmp(bad, good))
		return error(_("please use two different terms"));

	if (check_term_format(bad, "bad") || check_term_format(good, "good"))
		return -1;

	res = write_to_file(git_path_bisect_terms(), "%s\n%s\n", bad, good);

	return res;
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

		if (repo_get_oid_commit(the_repository, commit, &oid))
			return error(_("'%s' is not a valid commit"), commit);
		strbuf_addstr(&branch, commit);
	}

	if (!ref_exists("BISECT_HEAD")) {
		struct child_process cmd = CHILD_PROCESS_INIT;

		cmd.git_cmd = 1;
		strvec_pushl(&cmd.args, "checkout", "--ignore-other-worktrees",
				branch.buf, "--", NULL);
		if (run_command(&cmd)) {
			error(_("could not check out original"
				" HEAD '%s'. Try 'git bisect"
				" reset <commit>'."), branch.buf);
			strbuf_release(&branch);
			return -1;
		}
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

	repo_format_commit_message(the_repository, commit, "%s", &commit_msg,
				   &pp);

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
	int res = 0;

	if (!strcmp(state, terms->term_bad)) {
		strbuf_addf(&tag, "refs/bisect/%s", state);
	} else if (one_of(state, terms->term_good, "skip", NULL)) {
		strbuf_addf(&tag, "refs/bisect/%s-%s", state, rev);
	} else {
		res = error(_("Bad bisect_write argument: %s"), state);
		goto finish;
	}

	if (repo_get_oid(the_repository, rev, &oid)) {
		res = error(_("couldn't get the oid of the rev '%s'"), rev);
		goto finish;
	}

	if (update_ref(NULL, tag.buf, &oid, NULL, 0,
		       UPDATE_REFS_MSG_ON_ERR)) {
		res = -1;
		goto finish;
	}

	fp = fopen(git_path_bisect_log(), "a");
	if (!fp) {
		res = error_errno(_("couldn't open the file '%s'"), git_path_bisect_log());
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
	return res;
}

static int check_and_set_terms(struct bisect_terms *terms, const char *cmd)
{
	int has_term_file = !is_empty_or_missing_file(git_path_bisect_terms());

	if (one_of(cmd, "skip", "start", "terms", NULL))
		return 0;

	if (has_term_file && strcmp(cmd, terms->term_bad) &&
	    strcmp(cmd, terms->term_good))
		return error(_("Invalid command: you're currently in a "
				"%s/%s bisect"), terms->term_bad,
				terms->term_good);

	if (!has_term_file) {
		if (one_of(cmd, "bad", "good", NULL)) {
			set_terms(terms, "bad", "good");
			return write_terms(terms->term_bad, terms->term_good);
		}
		if (one_of(cmd, "new", "old", NULL)) {
			set_terms(terms, "new", "old");
			return write_terms(terms->term_bad, terms->term_good);
		}
	}

	return 0;
}

static int inc_nr(const char *refname UNUSED,
		  const struct object_id *oid UNUSED,
		  int flag UNUSED, void *cb_data)
{
	unsigned int *nr = (unsigned int *)cb_data;
	(*nr)++;
	return 0;
}

static const char need_bad_and_good_revision_warning[] =
	N_("You need to give me at least one %s and %s revision.\n"
	   "You can use \"git bisect %s\" and \"git bisect %s\" for that.");

static const char need_bisect_start_warning[] =
	N_("You need to start by \"git bisect start\".\n"
	   "You then need to give me at least one %s and %s revision.\n"
	   "You can use \"git bisect %s\" and \"git bisect %s\" for that.");

static int decide_next(const struct bisect_terms *terms,
		       const char *current_term, int missing_good,
		       int missing_bad)
{
	if (!missing_good && !missing_bad)
		return 0;
	if (!current_term)
		return -1;

	if (missing_good && !missing_bad &&
	    !strcmp(current_term, terms->term_good)) {
		char *yesno;
		/*
		 * have bad (or new) but not good (or old). We could bisect
		 * although this is less optimum.
		 */
		warning(_("bisecting only with a %s commit"), terms->term_bad);
		if (!isatty(0))
			return 0;
		/*
		 * TRANSLATORS: Make sure to include [Y] and [n] in your
		 * translation. The program will only accept English input
		 * at this point.
		 */
		yesno = git_prompt(_("Are you sure [Y/n]? "), PROMPT_ECHO);
		if (starts_with(yesno, "N") || starts_with(yesno, "n"))
			return -1;
		return 0;
	}

	if (!is_empty_or_missing_file(git_path_bisect_start()))
		return error(_(need_bad_and_good_revision_warning),
			     vocab_bad, vocab_good, vocab_bad, vocab_good);
	else
		return error(_(need_bisect_start_warning),
			     vocab_good, vocab_bad, vocab_good, vocab_bad);
}

static void bisect_status(struct bisect_state *state,
			  const struct bisect_terms *terms)
{
	char *bad_ref = xstrfmt("refs/bisect/%s", terms->term_bad);
	char *good_glob = xstrfmt("%s-*", terms->term_good);

	if (ref_exists(bad_ref))
		state->nr_bad = 1;

	for_each_glob_ref_in(inc_nr, good_glob, "refs/bisect/",
			     (void *) &state->nr_good);

	free(good_glob);
	free(bad_ref);
}

__attribute__((format (printf, 1, 2)))
static void bisect_log_printf(const char *fmt, ...)
{
	struct strbuf buf = STRBUF_INIT;
	va_list ap;

	va_start(ap, fmt);
	strbuf_vaddf(&buf, fmt, ap);
	va_end(ap);

	printf("%s", buf.buf);
	append_to_file(git_path_bisect_log(), "# %s", buf.buf);

	strbuf_release(&buf);
}

static void bisect_print_status(const struct bisect_terms *terms)
{
	struct bisect_state state = { 0 };

	bisect_status(&state, terms);

	/* If we had both, we'd already be started, and shouldn't get here. */
	if (state.nr_good && state.nr_bad)
		return;

	if (!state.nr_good && !state.nr_bad)
		bisect_log_printf(_("status: waiting for both good and bad commits\n"));
	else if (state.nr_good)
		bisect_log_printf(Q_("status: waiting for bad commit, %d good commit known\n",
				     "status: waiting for bad commit, %d good commits known\n",
				     state.nr_good), state.nr_good);
	else
		bisect_log_printf(_("status: waiting for good commit(s), bad commit known\n"));
}

static int bisect_next_check(const struct bisect_terms *terms,
			     const char *current_term)
{
	struct bisect_state state = { 0 };
	bisect_status(&state, terms);
	return decide_next(terms, current_term, !state.nr_good, !state.nr_bad);
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

	free_terms(terms);
	strbuf_getline_lf(&str, fp);
	terms->term_bad = strbuf_detach(&str, NULL);
	strbuf_getline_lf(&str, fp);
	terms->term_good = strbuf_detach(&str, NULL);

finish:
	if (fp)
		fclose(fp);
	strbuf_release(&str);
	return res;
}

static int bisect_terms(struct bisect_terms *terms, const char *option)
{
	if (get_terms(terms))
		return error(_("no terms defined"));

	if (!option) {
		printf(_("Your current terms are %s for the old state\n"
			 "and %s for the new state.\n"),
		       terms->term_good, terms->term_bad);
		return 0;
	}
	if (one_of(option, "--term-good", "--term-old", NULL))
		printf("%s\n", terms->term_good);
	else if (one_of(option, "--term-bad", "--term-new", NULL))
		printf("%s\n", terms->term_bad);
	else
		return error(_("invalid argument %s for 'git bisect terms'.\n"
			       "Supported options are: "
			       "--term-good|--term-old and "
			       "--term-bad|--term-new."), option);

	return 0;
}

static int bisect_append_log_quoted(const char **argv)
{
	int res = 0;
	FILE *fp = fopen(git_path_bisect_log(), "a");
	struct strbuf orig_args = STRBUF_INIT;

	if (!fp)
		return -1;

	if (fprintf(fp, "git bisect start") < 1) {
		res = -1;
		goto finish;
	}

	sq_quote_argv(&orig_args, argv);
	if (fprintf(fp, "%s\n", orig_args.buf) < 1)
		res = -1;

finish:
	fclose(fp);
	strbuf_release(&orig_args);
	return res;
}

static int add_bisect_ref(const char *refname, const struct object_id *oid,
			  int flags UNUSED, void *cb)
{
	struct add_bisect_ref_data *data = cb;

	add_pending_oid(data->revs, refname, oid, data->object_flags);

	return 0;
}

static int prepare_revs(struct bisect_terms *terms, struct rev_info *revs)
{
	int res = 0;
	struct add_bisect_ref_data cb = { revs };
	char *good = xstrfmt("%s-*", terms->term_good);

	/*
	 * We cannot use terms->term_bad directly in
	 * for_each_glob_ref_in() and we have to append a '*' to it,
	 * otherwise for_each_glob_ref_in() will append '/' and '*'.
	 */
	char *bad = xstrfmt("%s*", terms->term_bad);

	/*
	 * It is important to reset the flags used by revision walks
	 * as the previous call to bisect_next_all() in turn
	 * sets up a revision walk.
	 */
	reset_revision_walk();
	repo_init_revisions(the_repository, revs, NULL);
	setup_revisions(0, NULL, revs, NULL);
	for_each_glob_ref_in(add_bisect_ref, bad, "refs/bisect/", &cb);
	cb.object_flags = UNINTERESTING;
	for_each_glob_ref_in(add_bisect_ref, good, "refs/bisect/", &cb);
	if (prepare_revision_walk(revs))
		res = error(_("revision walk setup failed\n"));

	free(good);
	free(bad);
	return res;
}

static int bisect_skipped_commits(struct bisect_terms *terms)
{
	int res;
	FILE *fp = NULL;
	struct rev_info revs;
	struct commit *commit;
	struct pretty_print_context pp = {0};
	struct strbuf commit_name = STRBUF_INIT;

	res = prepare_revs(terms, &revs);
	if (res)
		return res;

	fp = fopen(git_path_bisect_log(), "a");
	if (!fp)
		return error_errno(_("could not open '%s' for appending"),
				  git_path_bisect_log());

	if (fprintf(fp, "# only skipped commits left to test\n") < 0)
		return error_errno(_("failed to write to '%s'"), git_path_bisect_log());

	while ((commit = get_revision(&revs)) != NULL) {
		strbuf_reset(&commit_name);
		repo_format_commit_message(the_repository, commit, "%s",
					   &commit_name, &pp);
		fprintf(fp, "# possible first %s commit: [%s] %s\n",
			terms->term_bad, oid_to_hex(&commit->object.oid),
			commit_name.buf);
	}

	/*
	 * Reset the flags used by revision walks in case
	 * there is another revision walk after this one.
	 */
	reset_revision_walk();

	strbuf_release(&commit_name);
	release_revisions(&revs);
	fclose(fp);
	return 0;
}

static int bisect_successful(struct bisect_terms *terms)
{
	struct object_id oid;
	struct commit *commit;
	struct pretty_print_context pp = {0};
	struct strbuf commit_name = STRBUF_INIT;
	char *bad_ref = xstrfmt("refs/bisect/%s",terms->term_bad);
	int res;

	read_ref(bad_ref, &oid);
	commit = lookup_commit_reference_by_name(bad_ref);
	repo_format_commit_message(the_repository, commit, "%s", &commit_name,
				   &pp);

	res = append_to_file(git_path_bisect_log(), "# first %s commit: [%s] %s\n",
			    terms->term_bad, oid_to_hex(&commit->object.oid),
			    commit_name.buf);

	strbuf_release(&commit_name);
	free(bad_ref);
	return res;
}

static enum bisect_error bisect_next(struct bisect_terms *terms, const char *prefix)
{
	enum bisect_error res;

	if (bisect_autostart(terms))
		return BISECT_FAILED;

	if (bisect_next_check(terms, terms->term_good))
		return BISECT_FAILED;

	/* Perform all bisection computation */
	res = bisect_next_all(the_repository, prefix);

	if (res == BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND) {
		res = bisect_successful(terms);
		return res ? res : BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND;
	} else if (res == BISECT_ONLY_SKIPPED_LEFT) {
		res = bisect_skipped_commits(terms);
		return res ? res : BISECT_ONLY_SKIPPED_LEFT;
	}
	return res;
}

static enum bisect_error bisect_auto_next(struct bisect_terms *terms, const char *prefix)
{
	if (bisect_next_check(terms, NULL)) {
		bisect_print_status(terms);
		return BISECT_OK;
	}

	return bisect_next(terms, prefix);
}

static enum bisect_error bisect_start(struct bisect_terms *terms, int argc,
				      const char **argv)
{
	int no_checkout = 0;
	int first_parent_only = 0;
	int i, has_double_dash = 0, must_write_terms = 0, bad_seen = 0;
	int flags, pathspec_pos;
	enum bisect_error res = BISECT_OK;
	struct string_list revs = STRING_LIST_INIT_DUP;
	struct string_list states = STRING_LIST_INIT_DUP;
	struct strbuf start_head = STRBUF_INIT;
	struct strbuf bisect_names = STRBUF_INIT;
	struct object_id head_oid;
	struct object_id oid;
	const char *head;

	if (is_bare_repository())
		no_checkout = 1;

	/*
	 * Check for one bad and then some good revisions
	 */
	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--")) {
			has_double_dash = 1;
			break;
		}
	}

	for (i = 0; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(argv[i], "--")) {
			break;
		} else if (!strcmp(arg, "--no-checkout")) {
			no_checkout = 1;
		} else if (!strcmp(arg, "--first-parent")) {
			first_parent_only = 1;
		} else if (!strcmp(arg, "--term-good") ||
			 !strcmp(arg, "--term-old")) {
			i++;
			if (argc <= i)
				return error(_("'' is not a valid term"));
			must_write_terms = 1;
			free((void *) terms->term_good);
			terms->term_good = xstrdup(argv[i]);
		} else if (skip_prefix(arg, "--term-good=", &arg) ||
			   skip_prefix(arg, "--term-old=", &arg)) {
			must_write_terms = 1;
			free((void *) terms->term_good);
			terms->term_good = xstrdup(arg);
		} else if (!strcmp(arg, "--term-bad") ||
			 !strcmp(arg, "--term-new")) {
			i++;
			if (argc <= i)
				return error(_("'' is not a valid term"));
			must_write_terms = 1;
			free((void *) terms->term_bad);
			terms->term_bad = xstrdup(argv[i]);
		} else if (skip_prefix(arg, "--term-bad=", &arg) ||
			   skip_prefix(arg, "--term-new=", &arg)) {
			must_write_terms = 1;
			free((void *) terms->term_bad);
			terms->term_bad = xstrdup(arg);
		} else if (starts_with(arg, "--")) {
			return error(_("unrecognized option: '%s'"), arg);
		} else if (!get_oidf(&oid, "%s^{commit}", arg)) {
			string_list_append(&revs, oid_to_hex(&oid));
		} else if (has_double_dash) {
			die(_("'%s' does not appear to be a valid "
			      "revision"), arg);
		} else {
			break;
		}
	}
	pathspec_pos = i;

	/*
	 * The user ran "git bisect start <sha1> <sha1>", hence did not
	 * explicitly specify the terms, but we are already starting to
	 * set references named with the default terms, and won't be able
	 * to change afterwards.
	 */
	if (revs.nr)
		must_write_terms = 1;
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
	head = resolve_ref_unsafe("HEAD", 0, &head_oid, &flags);
	if (!head)
		if (repo_get_oid(the_repository, "HEAD", &head_oid))
			return error(_("bad HEAD - I need a HEAD"));

	/*
	 * Check if we are bisecting
	 */
	if (!is_empty_or_missing_file(git_path_bisect_start())) {
		/* Reset to the rev from where we started */
		strbuf_read_file(&start_head, git_path_bisect_start(), 0);
		strbuf_trim(&start_head);
		if (!no_checkout) {
			struct child_process cmd = CHILD_PROCESS_INIT;

			cmd.git_cmd = 1;
			strvec_pushl(&cmd.args, "checkout", start_head.buf,
				     "--", NULL);
			if (run_command(&cmd)) {
				res = error(_("checking out '%s' failed."
						 " Try 'git bisect start "
						 "<valid-branch>'."),
					       start_head.buf);
				goto finish;
			}
		}
	} else {
		/* Get the rev from where we start. */
		if (!repo_get_oid(the_repository, head, &head_oid) &&
		    !starts_with(head, "refs/heads/")) {
			strbuf_reset(&start_head);
			strbuf_addstr(&start_head, oid_to_hex(&head_oid));
		} else if (!repo_get_oid(the_repository, head, &head_oid) &&
			   skip_prefix(head, "refs/heads/", &head)) {
			strbuf_addstr(&start_head, head);
		} else {
			return error(_("bad HEAD - strange symbolic ref"));
		}
	}

	/*
	 * Get rid of any old bisect state.
	 */
	if (bisect_clean_state())
		return BISECT_FAILED;

	/*
	 * Write new start state
	 */
	write_file(git_path_bisect_start(), "%s\n", start_head.buf);

	if (first_parent_only)
		write_file(git_path_bisect_first_parent(), "\n");

	if (no_checkout) {
		if (repo_get_oid(the_repository, start_head.buf, &oid) < 0) {
			res = error(_("invalid ref: '%s'"), start_head.buf);
			goto finish;
		}
		if (update_ref(NULL, "BISECT_HEAD", &oid, NULL, 0,
			       UPDATE_REFS_MSG_ON_ERR)) {
			res = BISECT_FAILED;
			goto finish;
		}
	}

	if (pathspec_pos < argc - 1)
		sq_quote_argv(&bisect_names, argv + pathspec_pos);
	write_file(git_path_bisect_names(), "%s\n", bisect_names.buf);

	for (i = 0; i < states.nr; i++)
		if (bisect_write(states.items[i].string,
				 revs.items[i].string, terms, 1)) {
			res = BISECT_FAILED;
			goto finish;
		}

	if (must_write_terms && write_terms(terms->term_bad,
					    terms->term_good)) {
		res = BISECT_FAILED;
		goto finish;
	}

	res = bisect_append_log_quoted(argv);
	if (res)
		res = BISECT_FAILED;

finish:
	string_list_clear(&revs, 0);
	string_list_clear(&states, 0);
	strbuf_release(&start_head);
	strbuf_release(&bisect_names);
	if (res)
		return res;

	res = bisect_auto_next(terms, NULL);
	if (!is_bisect_success(res))
		bisect_clean_state();
	return res;
}

static inline int file_is_not_empty(const char *path)
{
	return !is_empty_or_missing_file(path);
}

static int bisect_autostart(struct bisect_terms *terms)
{
	int res;
	const char *yesno;

	if (file_is_not_empty(git_path_bisect_start()))
		return 0;

	fprintf_ln(stderr, _("You need to start by \"git bisect "
			  "start\"\n"));

	if (!isatty(STDIN_FILENO))
		return -1;

	/*
	 * TRANSLATORS: Make sure to include [Y] and [n] in your
	 * translation. The program will only accept English input
	 * at this point.
	 */
	yesno = git_prompt(_("Do you want me to do it for you "
			     "[Y/n]? "), PROMPT_ECHO);
	res = tolower(*yesno) == 'n' ?
		-1 : bisect_start(terms, 0, empty_strvec);

	return res;
}

static enum bisect_error bisect_state(struct bisect_terms *terms, int argc,
				      const char **argv)
{
	const char *state;
	int i, verify_expected = 1;
	struct object_id oid, expected;
	struct strbuf buf = STRBUF_INIT;
	struct oid_array revs = OID_ARRAY_INIT;

	if (!argc)
		return error(_("Please call `--bisect-state` with at least one argument"));

	if (bisect_autostart(terms))
		return BISECT_FAILED;

	state = argv[0];
	if (check_and_set_terms(terms, state) ||
	    !one_of(state, terms->term_good, terms->term_bad, "skip", NULL))
		return BISECT_FAILED;

	argv++;
	argc--;
	if (argc > 1 && !strcmp(state, terms->term_bad))
		return error(_("'git bisect %s' can take only one argument."), terms->term_bad);

	if (argc == 0) {
		const char *head = "BISECT_HEAD";
		enum get_oid_result res_head = repo_get_oid(the_repository,
							    head, &oid);

		if (res_head == MISSING_OBJECT) {
			head = "HEAD";
			res_head = repo_get_oid(the_repository, head, &oid);
		}

		if (res_head)
			error(_("Bad rev input: %s"), head);
		oid_array_append(&revs, &oid);
	}

	/*
	 * All input revs must be checked before executing bisect_write()
	 * to discard junk revs.
	 */

	for (; argc; argc--, argv++) {
		struct commit *commit;

		if (repo_get_oid(the_repository, *argv, &oid)){
			error(_("Bad rev input: %s"), *argv);
			oid_array_clear(&revs);
			return BISECT_FAILED;
		}

		commit = lookup_commit_reference(the_repository, &oid);
		if (!commit)
			die(_("Bad rev input (not a commit): %s"), *argv);

		oid_array_append(&revs, &commit->object.oid);
	}

	if (strbuf_read_file(&buf, git_path_bisect_expected_rev(), 0) < the_hash_algo->hexsz ||
	    get_oid_hex(buf.buf, &expected) < 0)
		verify_expected = 0; /* Ignore invalid file contents */
	strbuf_release(&buf);

	for (i = 0; i < revs.nr; i++) {
		if (bisect_write(state, oid_to_hex(&revs.oid[i]), terms, 0)) {
			oid_array_clear(&revs);
			return BISECT_FAILED;
		}
		if (verify_expected && !oideq(&revs.oid[i], &expected)) {
			unlink_or_warn(git_path_bisect_ancestors_ok());
			unlink_or_warn(git_path_bisect_expected_rev());
			verify_expected = 0;
		}
	}

	oid_array_clear(&revs);
	return bisect_auto_next(terms, NULL);
}

static enum bisect_error bisect_log(void)
{
	int fd, status;
	const char* filename = git_path_bisect_log();

	if (is_empty_or_missing_file(filename))
		return error(_("We are not bisecting."));

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return BISECT_FAILED;

	status = copy_fd(fd, STDOUT_FILENO);
	close(fd);
	return status ? BISECT_FAILED : BISECT_OK;
}

static int process_replay_line(struct bisect_terms *terms, struct strbuf *line)
{
	const char *p = line->buf + strspn(line->buf, " \t");
	char *word_end, *rev;

	if ((!skip_prefix(p, "git bisect", &p) &&
	!skip_prefix(p, "git-bisect", &p)) || !isspace(*p))
		return 0;
	p += strspn(p, " \t");

	word_end = (char *)p + strcspn(p, " \t");
	rev = word_end + strspn(word_end, " \t");
	*word_end = '\0'; /* NUL-terminate the word */

	get_terms(terms);
	if (check_and_set_terms(terms, p))
		return -1;

	if (!strcmp(p, "start")) {
		struct strvec argv = STRVEC_INIT;
		int res;
		sq_dequote_to_strvec(rev, &argv);
		res = bisect_start(terms, argv.nr, argv.v);
		strvec_clear(&argv);
		return res;
	}

	if (one_of(p, terms->term_good,
	   terms->term_bad, "skip", NULL))
		return bisect_write(p, rev, terms, 0);

	if (!strcmp(p, "terms")) {
		struct strvec argv = STRVEC_INIT;
		int res;
		sq_dequote_to_strvec(rev, &argv);
		res = bisect_terms(terms, argv.nr == 1 ? argv.v[0] : NULL);
		strvec_clear(&argv);
		return res;
	}
	error(_("'%s'?? what are you talking about?"), p);

	return -1;
}

static enum bisect_error bisect_replay(struct bisect_terms *terms, const char *filename)
{
	FILE *fp = NULL;
	enum bisect_error res = BISECT_OK;
	struct strbuf line = STRBUF_INIT;

	if (is_empty_or_missing_file(filename))
		return error(_("cannot read file '%s' for replaying"), filename);

	if (bisect_reset(NULL))
		return BISECT_FAILED;

	fp = fopen(filename, "r");
	if (!fp)
		return BISECT_FAILED;

	while ((strbuf_getline(&line, fp) != EOF) && !res)
		res = process_replay_line(terms, &line);

	strbuf_release(&line);
	fclose(fp);

	if (res)
		return BISECT_FAILED;

	return bisect_auto_next(terms, NULL);
}

static enum bisect_error bisect_skip(struct bisect_terms *terms, int argc,
				     const char **argv)
{
	int i;
	enum bisect_error res;
	struct strvec argv_state = STRVEC_INIT;

	strvec_push(&argv_state, "skip");

	for (i = 0; i < argc; i++) {
		const char *dotdot = strstr(argv[i], "..");

		if (dotdot) {
			struct rev_info revs;
			struct commit *commit;

			repo_init_revisions(the_repository, &revs, NULL);
			setup_revisions(2, argv + i - 1, &revs, NULL);

			if (prepare_revision_walk(&revs))
				die(_("revision walk setup failed\n"));
			while ((commit = get_revision(&revs)) != NULL)
				strvec_push(&argv_state,
						oid_to_hex(&commit->object.oid));

			reset_revision_walk();
			release_revisions(&revs);
		} else {
			strvec_push(&argv_state, argv[i]);
		}
	}
	res = bisect_state(terms, argv_state.nr, argv_state.v);

	strvec_clear(&argv_state);
	return res;
}

static int bisect_visualize(struct bisect_terms *terms, int argc,
			    const char **argv)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	struct strbuf sb = STRBUF_INIT;

	if (bisect_next_check(terms, NULL) != 0)
		return BISECT_FAILED;

	cmd.no_stdin = 1;
	if (!argc) {
		if ((getenv("DISPLAY") || getenv("SESSIONNAME") || getenv("MSYSTEM") ||
		     getenv("SECURITYSESSIONID")) && exists_in_PATH("gitk")) {
			strvec_push(&cmd.args, "gitk");
		} else {
			strvec_push(&cmd.args, "log");
			cmd.git_cmd = 1;
		}
	} else {
		if (argv[0][0] == '-') {
			strvec_push(&cmd.args, "log");
			cmd.git_cmd = 1;
		} else if (strcmp(argv[0], "tig") && !starts_with(argv[0], "git"))
			cmd.git_cmd = 1;

		strvec_pushv(&cmd.args, argv);
	}

	strvec_pushl(&cmd.args, "--bisect", "--", NULL);

	strbuf_read_file(&sb, git_path_bisect_names(), 0);
	sq_dequote_to_strvec(sb.buf, &cmd.args);
	strbuf_release(&sb);

	return run_command(&cmd);
}

static int get_first_good(const char *refname UNUSED,
			  const struct object_id *oid,
			  int flag UNUSED, void *cb_data)
{
	oidcpy(cb_data, oid);
	return 1;
}

static int do_bisect_run(const char *command)
{
	struct child_process cmd = CHILD_PROCESS_INIT;

	printf(_("running %s\n"), command);
	cmd.use_shell = 1;
	strvec_push(&cmd.args, command);
	return run_command(&cmd);
}

static int verify_good(const struct bisect_terms *terms, const char *command)
{
	int rc;
	enum bisect_error res;
	struct object_id good_rev;
	struct object_id current_rev;
	char *good_glob = xstrfmt("%s-*", terms->term_good);
	int no_checkout = ref_exists("BISECT_HEAD");

	for_each_glob_ref_in(get_first_good, good_glob, "refs/bisect/",
			     &good_rev);
	free(good_glob);

	if (read_ref(no_checkout ? "BISECT_HEAD" : "HEAD", &current_rev))
		return -1;

	res = bisect_checkout(&good_rev, no_checkout);
	if (res != BISECT_OK)
		return -1;

	rc = do_bisect_run(command);

	res = bisect_checkout(&current_rev, no_checkout);
	if (res != BISECT_OK)
		return -1;

	return rc;
}

static int bisect_run(struct bisect_terms *terms, int argc, const char **argv)
{
	int res = BISECT_OK;
	struct strbuf command = STRBUF_INIT;
	const char *new_state;
	int temporary_stdout_fd, saved_stdout;
	int is_first_run = 1;

	if (bisect_next_check(terms, NULL))
		return BISECT_FAILED;

	if (!argc) {
		error(_("bisect run failed: no command provided."));
		return BISECT_FAILED;
	}

	sq_quote_argv(&command, argv);
	strbuf_ltrim(&command);
	while (1) {
		res = do_bisect_run(command.buf);

		/*
		 * Exit code 126 and 127 can either come from the shell
		 * if it was unable to execute or even find the script,
		 * or from the script itself.  Check with a known-good
		 * revision to avoid trashing the bisect run due to a
		 * missing or non-executable script.
		 */
		if (is_first_run && (res == 126 || res == 127)) {
			int rc = verify_good(terms, command.buf);
			is_first_run = 0;
			if (rc < 0 || 128 <= rc) {
				error(_("unable to verify %s on good"
					" revision"), command.buf);
				res = BISECT_FAILED;
				break;
			}
			if (rc == res) {
				error(_("bogus exit code %d for good revision"),
				      rc);
				res = BISECT_FAILED;
				break;
			}
		}

		if (res < 0 || 128 <= res) {
			error(_("bisect run failed: exit code %d from"
				" %s is < 0 or >= 128"), res, command.buf);
			break;
		}

		if (res == 125)
			new_state = "skip";
		else if (!res)
			new_state = terms->term_good;
		else
			new_state = terms->term_bad;

		temporary_stdout_fd = open(git_path_bisect_run(), O_CREAT | O_WRONLY | O_TRUNC, 0666);

		if (temporary_stdout_fd < 0) {
			res = error_errno(_("cannot open file '%s' for writing"), git_path_bisect_run());
			break;
		}

		fflush(stdout);
		saved_stdout = dup(1);
		dup2(temporary_stdout_fd, 1);

		res = bisect_state(terms, 1, &new_state);

		fflush(stdout);
		dup2(saved_stdout, 1);
		close(saved_stdout);
		close(temporary_stdout_fd);

		print_file_to_stdout(git_path_bisect_run());

		if (res == BISECT_ONLY_SKIPPED_LEFT)
			error(_("bisect run cannot continue any more"));
		else if (res == BISECT_INTERNAL_SUCCESS_MERGE_BASE) {
			puts(_("bisect run success"));
			res = BISECT_OK;
		} else if (res == BISECT_INTERNAL_SUCCESS_1ST_BAD_FOUND) {
			puts(_("bisect found first bad commit"));
			res = BISECT_OK;
		} else if (res) {
			error(_("bisect run failed: 'git bisect %s'"
				" exited with error code %d"), new_state, res);
		} else {
			continue;
		}
		break;
	}

	strbuf_release(&command);
	return res;
}

static int cmd_bisect__reset(int argc, const char **argv, const char *prefix UNUSED)
{
	if (argc > 1)
		return error(_("'%s' requires either no argument or a commit"),
			     "git bisect reset");
	return bisect_reset(argc ? argv[0] : NULL);
}

static int cmd_bisect__terms(int argc, const char **argv, const char *prefix UNUSED)
{
	int res;
	struct bisect_terms terms = { 0 };

	if (argc > 1)
		return error(_("'%s' requires 0 or 1 argument"),
			     "git bisect terms");
	res = bisect_terms(&terms, argc == 1 ? argv[0] : NULL);
	free_terms(&terms);
	return res;
}

static int cmd_bisect__start(int argc, const char **argv, const char *prefix UNUSED)
{
	int res;
	struct bisect_terms terms = { 0 };

	set_terms(&terms, "bad", "good");
	res = bisect_start(&terms, argc, argv);
	free_terms(&terms);
	return res;
}

static int cmd_bisect__next(int argc, const char **argv UNUSED, const char *prefix)
{
	int res;
	struct bisect_terms terms = { 0 };

	if (argc)
		return error(_("'%s' requires 0 arguments"),
			     "git bisect next");
	get_terms(&terms);
	res = bisect_next(&terms, prefix);
	free_terms(&terms);
	return res;
}

static int cmd_bisect__log(int argc UNUSED, const char **argv UNUSED, const char *prefix UNUSED)
{
	return bisect_log();
}

static int cmd_bisect__replay(int argc, const char **argv, const char *prefix UNUSED)
{
	int res;
	struct bisect_terms terms = { 0 };

	if (argc != 1)
		return error(_("no logfile given"));
	set_terms(&terms, "bad", "good");
	res = bisect_replay(&terms, argv[0]);
	free_terms(&terms);
	return res;
}

static int cmd_bisect__skip(int argc, const char **argv, const char *prefix UNUSED)
{
	int res;
	struct bisect_terms terms = { 0 };

	set_terms(&terms, "bad", "good");
	get_terms(&terms);
	res = bisect_skip(&terms, argc, argv);
	free_terms(&terms);
	return res;
}

static int cmd_bisect__visualize(int argc, const char **argv, const char *prefix UNUSED)
{
	int res;
	struct bisect_terms terms = { 0 };

	get_terms(&terms);
	res = bisect_visualize(&terms, argc, argv);
	free_terms(&terms);
	return res;
}

static int cmd_bisect__run(int argc, const char **argv, const char *prefix UNUSED)
{
	int res;
	struct bisect_terms terms = { 0 };

	if (!argc)
		return error(_("'%s' failed: no command provided."), "git bisect run");
	get_terms(&terms);
	res = bisect_run(&terms, argc, argv);
	free_terms(&terms);
	return res;
}

int cmd_bisect(int argc, const char **argv, const char *prefix)
{
	int res = 0;
	parse_opt_subcommand_fn *fn = NULL;
	struct option options[] = {
		OPT_SUBCOMMAND("reset", &fn, cmd_bisect__reset),
		OPT_SUBCOMMAND("terms", &fn, cmd_bisect__terms),
		OPT_SUBCOMMAND("start", &fn, cmd_bisect__start),
		OPT_SUBCOMMAND("next", &fn, cmd_bisect__next),
		OPT_SUBCOMMAND("log", &fn, cmd_bisect__log),
		OPT_SUBCOMMAND("replay", &fn, cmd_bisect__replay),
		OPT_SUBCOMMAND("skip", &fn, cmd_bisect__skip),
		OPT_SUBCOMMAND("visualize", &fn, cmd_bisect__visualize),
		OPT_SUBCOMMAND("view", &fn, cmd_bisect__visualize),
		OPT_SUBCOMMAND("run", &fn, cmd_bisect__run),
		OPT_END()
	};
	argc = parse_options(argc, argv, prefix, options, git_bisect_usage,
			     PARSE_OPT_SUBCOMMAND_OPTIONAL);

	if (!fn) {
		struct bisect_terms terms = { 0 };

		if (!argc)
			usage_msg_opt(_("need a command"), git_bisect_usage, options);

		set_terms(&terms, "bad", "good");
		get_terms(&terms);
		if (check_and_set_terms(&terms, argv[0]))
			usage_msg_optf(_("unknown command: '%s'"), git_bisect_usage,
				       options, argv[0]);
		res = bisect_state(&terms, argc, argv);
		free_terms(&terms);
	} else {
		argc--;
		argv++;
		res = fn(argc, argv, prefix);
	}

	return is_bisect_success(res) ? 0 : -res;
}
