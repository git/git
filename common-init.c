#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "common-init.h"
#include "exec-cmd.h"
#include "gettext.h"
#include "attr.h"
#include "odb.h"
#include "parse.h"
#include "repository.h"
#include "replace-object.h"
#include "setup.h"
#include "strbuf.h"
#include "trace2.h"

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

static void setup_environment(void)
{
	char *git_replace_ref_base;
	const char *replace_ref_base;

	if (getenv(NO_REPLACE_OBJECTS_ENVIRONMENT))
		disable_replace_refs();
	replace_ref_base = getenv(GIT_REPLACE_REF_BASE_ENVIRONMENT);
	git_replace_ref_base = xstrdup(replace_ref_base ? replace_ref_base
							  : "refs/replace/");
	update_ref_namespace(NAMESPACE_REPLACE, git_replace_ref_base);

	if (git_env_bool(NO_LAZY_FETCH_ENVIRONMENT, 0))
		fetch_if_missing = 0;
}

void init_git(const char **argv)
{
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

	initialize_repository(the_repository);
	setup_environment();

	attr_start();

	trace2_initialize();
	trace2_cmd_start(argv);
	trace2_collect_process_info(TRACE2_PROCESS_INFO_STARTUP);

	if (!strbuf_getcwd(&tmp))
		tmp_original_cwd = strbuf_detach(&tmp, NULL);
}
