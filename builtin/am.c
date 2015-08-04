/*
 * Builtin "git am"
 *
 * Based on git-am.sh by Junio C Hamano.
 */
#include "cache.h"
#include "builtin.h"
#include "exec_cmd.h"
#include "parse-options.h"
#include "dir.h"
#include "run-command.h"

enum patch_format {
	PATCH_FORMAT_UNKNOWN = 0,
	PATCH_FORMAT_MBOX
};

struct am_state {
	/* state directory path */
	char *dir;

	/* current and last patch numbers, 1-indexed */
	int cur;
	int last;

	/* number of digits in patch filename */
	int prec;
};

/**
 * Initializes am_state with the default values. The state directory is set to
 * dir.
 */
static void am_state_init(struct am_state *state, const char *dir)
{
	memset(state, 0, sizeof(*state));

	assert(dir);
	state->dir = xstrdup(dir);

	state->prec = 4;
}

/**
 * Releases memory allocated by an am_state.
 */
static void am_state_release(struct am_state *state)
{
	free(state->dir);
}

/**
 * Returns path relative to the am_state directory.
 */
static inline const char *am_path(const struct am_state *state, const char *path)
{
	return mkpath("%s/%s", state->dir, path);
}

/**
 * Returns 1 if there is an am session in progress, 0 otherwise.
 */
static int am_in_progress(const struct am_state *state)
{
	struct stat st;

	if (lstat(state->dir, &st) < 0 || !S_ISDIR(st.st_mode))
		return 0;
	if (lstat(am_path(state, "last"), &st) || !S_ISREG(st.st_mode))
		return 0;
	if (lstat(am_path(state, "next"), &st) || !S_ISREG(st.st_mode))
		return 0;
	return 1;
}

/**
 * Reads the contents of `file` in the `state` directory into `sb`. Returns the
 * number of bytes read on success, -1 if the file does not exist. If `trim` is
 * set, trailing whitespace will be removed.
 */
static int read_state_file(struct strbuf *sb, const struct am_state *state,
			const char *file, int trim)
{
	strbuf_reset(sb);

	if (strbuf_read_file(sb, am_path(state, file), 0) >= 0) {
		if (trim)
			strbuf_trim(sb);

		return sb->len;
	}

	if (errno == ENOENT)
		return -1;

	die_errno(_("could not read '%s'"), am_path(state, file));
}

/**
 * Loads state from disk.
 */
static void am_load(struct am_state *state)
{
	struct strbuf sb = STRBUF_INIT;

	if (read_state_file(&sb, state, "next", 1) < 0)
		die("BUG: state file 'next' does not exist");
	state->cur = strtol(sb.buf, NULL, 10);

	if (read_state_file(&sb, state, "last", 1) < 0)
		die("BUG: state file 'last' does not exist");
	state->last = strtol(sb.buf, NULL, 10);

	strbuf_release(&sb);
}

/**
 * Removes the am_state directory, forcefully terminating the current am
 * session.
 */
static void am_destroy(const struct am_state *state)
{
	struct strbuf sb = STRBUF_INIT;

	strbuf_addstr(&sb, state->dir);
	remove_dir_recursively(&sb, 0);
	strbuf_release(&sb);
}

/**
 * Splits out individual email patches from `paths`, where each path is either
 * a mbox file or a Maildir. Returns 0 on success, -1 on failure.
 */
static int split_mail_mbox(struct am_state *state, const char **paths)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf last = STRBUF_INIT;

	cp.git_cmd = 1;
	argv_array_push(&cp.args, "mailsplit");
	argv_array_pushf(&cp.args, "-d%d", state->prec);
	argv_array_pushf(&cp.args, "-o%s", state->dir);
	argv_array_push(&cp.args, "-b");
	argv_array_push(&cp.args, "--");
	argv_array_pushv(&cp.args, paths);

	if (capture_command(&cp, &last, 8))
		return -1;

	state->cur = 1;
	state->last = strtol(last.buf, NULL, 10);

	return 0;
}

/**
 * Splits a list of files/directories into individual email patches. Each path
 * in `paths` must be a file/directory that is formatted according to
 * `patch_format`.
 *
 * Once split out, the individual email patches will be stored in the state
 * directory, with each patch's filename being its index, padded to state->prec
 * digits.
 *
 * state->cur will be set to the index of the first mail, and state->last will
 * be set to the index of the last mail.
 *
 * Returns 0 on success, -1 on failure.
 */
static int split_mail(struct am_state *state, enum patch_format patch_format,
			const char **paths)
{
	switch (patch_format) {
	case PATCH_FORMAT_MBOX:
		return split_mail_mbox(state, paths);
	default:
		die("BUG: invalid patch_format");
	}
	return -1;
}

/**
 * Setup a new am session for applying patches
 */
static void am_setup(struct am_state *state, enum patch_format patch_format,
			const char **paths)
{
	if (mkdir(state->dir, 0777) < 0 && errno != EEXIST)
		die_errno(_("failed to create directory '%s'"), state->dir);

	if (split_mail(state, patch_format, paths) < 0) {
		am_destroy(state);
		die(_("Failed to split patches."));
	}

	/*
	 * NOTE: Since the "next" and "last" files determine if an am_state
	 * session is in progress, they should be written last.
	 */

	write_file(am_path(state, "next"), 1, "%d", state->cur);

	write_file(am_path(state, "last"), 1, "%d", state->last);
}

/**
 * Increments the patch pointer, and cleans am_state for the application of the
 * next patch.
 */
static void am_next(struct am_state *state)
{
	state->cur++;
	write_file(am_path(state, "next"), 1, "%d", state->cur);
}

/**
 * Applies all queued mail.
 */
static void am_run(struct am_state *state)
{
	while (state->cur <= state->last) {

		/* NEEDSWORK: Patch application not implemented yet */

		am_next(state);
	}

	am_destroy(state);
}

/**
 * parse_options() callback that validates and sets opt->value to the
 * PATCH_FORMAT_* enum value corresponding to `arg`.
 */
static int parse_opt_patchformat(const struct option *opt, const char *arg, int unset)
{
	int *opt_value = opt->value;

	if (!strcmp(arg, "mbox"))
		*opt_value = PATCH_FORMAT_MBOX;
	else
		return error(_("Invalid value for --patch-format: %s"), arg);
	return 0;
}

int cmd_am(int argc, const char **argv, const char *prefix)
{
	struct am_state state;
	int patch_format = PATCH_FORMAT_UNKNOWN;

	const char * const usage[] = {
		N_("git am [options] [(<mbox>|<Maildir>)...]"),
		NULL
	};

	struct option options[] = {
		OPT_CALLBACK(0, "patch-format", &patch_format, N_("format"),
			N_("format the patch(es) are in"),
			parse_opt_patchformat),
		OPT_END()
	};

	/*
	 * NEEDSWORK: Once all the features of git-am.sh have been
	 * re-implemented in builtin/am.c, this preamble can be removed.
	 */
	if (!getenv("_GIT_USE_BUILTIN_AM")) {
		const char *path = mkpath("%s/git-am", git_exec_path());

		if (sane_execvp(path, (char **)argv) < 0)
			die_errno("could not exec %s", path);
	} else {
		prefix = setup_git_directory();
		trace_repo_setup(prefix);
		setup_work_tree();
	}

	git_config(git_default_config, NULL);

	am_state_init(&state, git_path("rebase-apply"));

	argc = parse_options(argc, argv, prefix, options, usage, 0);

	if (am_in_progress(&state))
		am_load(&state);
	else {
		struct argv_array paths = ARGV_ARRAY_INIT;
		int i;

		for (i = 0; i < argc; i++) {
			if (is_absolute_path(argv[i]) || !prefix)
				argv_array_push(&paths, argv[i]);
			else
				argv_array_push(&paths, mkpath("%s/%s", prefix, argv[i]));
		}

		am_setup(&state, patch_format, paths.argv);

		argv_array_clear(&paths);
	}

	am_run(&state);

	am_state_release(&state);

	return 0;
}
