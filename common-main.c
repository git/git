#include "cache.h"
#include "exec-cmd.h"
#include "gettext.h"
#include "attr.h"
#include "setup.h"

/*
 * Many parts of Git have subprograms communicate via pipe, expect the
 * upstream of a pipe to die with SIGPIPE when the downstream of a
 * pipe does not need to read all that is written.  Some third-party
 * programs that ignore or block SIGPIPE for their own reason forget
 * to restore SIGPIPE handling to the default before spawning Git and
 * break this carefully orchestrated machinery.
 *
 * Restore the way SIGPIPE is handled to default, which is what we
 * expect.
 */
static void restore_sigpipe_to_default(void)
{
	sigset_t unblock;

	sigemptyset(&unblock);
	sigaddset(&unblock, SIGPIPE);
	sigprocmask(SIG_UNBLOCK, &unblock, NULL);
	signal(SIGPIPE, SIG_DFL);
}

int main(int argc, const char **argv)
{
	int result;
	struct strbuf tmp = STRBUF_INIT;

	trace2_initialize_clock();

	/*
	 * Always open file descriptors 0/1/2 to avoid clobbering files
	 * in die().  It also avoids messing up when the pipes are dup'ed
	 * onto stdin/stdout/stderr in the child processes we spawn.
	 */
	sanitize_stdfds();
	restore_sigpipe_to_default();

	git_resolve_executable_dir(argv[0]);

	setlocale(LC_CTYPE, "");
	git_setup_gettext();

	initialize_the_repository();

	attr_start();

	trace2_initialize();
	trace2_cmd_start(argv);
	trace2_collect_process_info(TRACE2_PROCESS_INFO_STARTUP);

	if (!strbuf_getcwd(&tmp))
		tmp_original_cwd = strbuf_detach(&tmp, NULL);

	result = cmd_main(argc, argv);

	/* Not exit(3), but a wrapper calling our common_exit() */
	exit(result);
}

static void check_bug_if_BUG(void)
{
	if (!bug_called_must_BUG)
		return;
	BUG("on exit(): had bug() call(s) in this process without explicit BUG_if_bug()");
}

/* We wrap exit() to call common_exit() in git-compat-util.h */
int common_exit(const char *file, int line, int code)
{
	/*
	 * For non-POSIX systems: Take the lowest 8 bits of the "code"
	 * to e.g. turn -1 into 255. On a POSIX system this is
	 * redundant, see exit(3) and wait(2), but as it doesn't harm
	 * anything there we don't need to guard this with an "ifdef".
	 */
	code &= 0xff;

	check_bug_if_BUG();
	trace2_cmd_exit_fl(file, line, code);

	return code;
}
