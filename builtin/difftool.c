/*
 * "git difftool" builtin command
 *
 * This is a wrapper around the GIT_EXTERNAL_DIFF-compatible
 * git-difftool--helper script.
 *
 * This script exports GIT_EXTERNAL_DIFF and GIT_PAGER for use by git.
 * The GIT_DIFF* variables are exported for use by git-difftool--helper.
 *
 * Any arguments that are unknown to this script are forwarded to 'git diff'.
 *
 * Copyright (C) 2016 Johannes Schindelin
 */
#include "builtin.h"
#include "run-command.h"
#include "exec_cmd.h"

/*
 * NEEDSWORK: this function can go once the legacy-difftool Perl script is
 * retired.
 *
 * We intentionally avoid reading the config directly here, to avoid messing up
 * the GIT_* environment variables when we need to fall back to exec()ing the
 * Perl script.
 */
static int use_builtin_difftool(void) {
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT;
	int ret;

	argv_array_pushl(&cp.args,
			 "config", "--bool", "difftool.usebuiltin", NULL);
	cp.git_cmd = 1;
	if (capture_command(&cp, &out, 6))
		return 0;
	strbuf_trim(&out);
	ret = !strcmp("true", out.buf);
	strbuf_release(&out);
	return ret;
}

int cmd_difftool(int argc, const char **argv, const char *prefix)
{
	/*
	 * NEEDSWORK: Once the builtin difftool has been tested enough
	 * and git-legacy-difftool.perl is retired to contrib/, this preamble
	 * can be removed.
	 */
	if (!use_builtin_difftool()) {
		const char *path = mkpath("%s/git-legacy-difftool",
					  git_exec_path());

		if (sane_execvp(path, (char **)argv) < 0)
			die_errno("could not exec %s", path);

		return 0;
	}
	prefix = setup_git_directory();
	trace_repo_setup(prefix);
	setup_work_tree();

	die("TODO");
}
