/*
 * "git rebase" builtin command
 *
 * Copyright (c) 2018 Pratik Karki
 */

#include "builtin.h"
#include "run-command.h"
#include "exec-cmd.h"
#include "argv-array.h"
#include "dir.h"

static int use_builtin_rebase(void)
{
	struct child_process cp = CHILD_PROCESS_INIT;
	struct strbuf out = STRBUF_INIT;
	int ret;

	argv_array_pushl(&cp.args,
			 "config", "--bool", "rebase.usebuiltin", NULL);
	cp.git_cmd = 1;
	if (capture_command(&cp, &out, 6)) {
		strbuf_release(&out);
		return 0;
	}

	strbuf_trim(&out);
	ret = !strcmp("true", out.buf);
	strbuf_release(&out);
	return ret;
}

int cmd_rebase(int argc, const char **argv, const char *prefix)
{
	/*
	 * NEEDSWORK: Once the builtin rebase has been tested enough
	 * and git-legacy-rebase.sh is retired to contrib/, this preamble
	 * can be removed.
	 */

	if (!use_builtin_rebase()) {
		const char *path = mkpath("%s/git-legacy-rebase",
					  git_exec_path());

		if (sane_execvp(path, (char **)argv) < 0)
			die_errno(_("could not exec %s"), path);
		else
			BUG("sane_execvp() returned???");
	}

	if (argc != 2)
		die(_("Usage: %s <base>"), argv[0]);
	prefix = setup_git_directory();
	trace_repo_setup(prefix);
	setup_work_tree();

	die("TODO");
}
