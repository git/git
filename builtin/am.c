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

struct am_state {
	/* state directory path */
	char *dir;

	/* current and last patch numbers, 1-indexed */
	int cur;
	int last;
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
 * Setup a new am session for applying patches
 */
static void am_setup(struct am_state *state)
{
	if (mkdir(state->dir, 0777) < 0 && errno != EEXIST)
		die_errno(_("failed to create directory '%s'"), state->dir);

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

int cmd_am(int argc, const char **argv, const char *prefix)
{
	struct am_state state;

	const char * const usage[] = {
		N_("git am [options] [(<mbox>|<Maildir>)...]"),
		NULL
	};

	struct option options[] = {
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
	else
		am_setup(&state);

	am_run(&state);

	am_state_release(&state);

	return 0;
}
