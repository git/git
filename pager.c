#include "cache.h"

/*
 * This is split up from the rest of git so that we can do
 * something different on Windows.
 */

static int spawned_pager;

#ifndef __MINGW32__
static void run_pager(const char *pager)
{
	/*
	 * Work around bug in "less" by not starting it until we
	 * have real input
	 */
	fd_set in;

	FD_ZERO(&in);
	FD_SET(0, &in);
	select(1, &in, NULL, &in, NULL);

	execlp(pager, pager, NULL);
	execl("/bin/sh", "sh", "-c", pager, NULL);
}
#else
#include "run-command.h"

static const char *pager_argv[] = { "sh", "-c", NULL, NULL };
static struct child_process pager_process = {
	.argv = pager_argv,
	.in = -1
};
static void wait_for_pager(void)
{
	fflush(stdout);
	fflush(stderr);
	/* signal EOF to pager */
	close(1);
	close(2);
	finish_command(&pager_process);
}
#endif

void setup_pager(void)
{
#ifndef __MINGW32__
	pid_t pid;
	int fd[2];
#endif
	const char *pager = getenv("GIT_PAGER");

	if (!isatty(1))
		return;
	if (!pager) {
		if (!pager_program)
			git_config(git_default_config, NULL);
		pager = pager_program;
	}
	if (!pager)
		pager = getenv("PAGER");
	if (!pager)
		pager = "less";
	else if (!*pager || !strcmp(pager, "cat"))
		return;

	spawned_pager = 1; /* means we are emitting to terminal */

#ifndef __MINGW32__
	if (pipe(fd) < 0)
		return;
	pid = fork();
	if (pid < 0) {
		close(fd[0]);
		close(fd[1]);
		return;
	}

	/* return in the child */
	if (!pid) {
		dup2(fd[1], 1);
		dup2(fd[1], 2);
		close(fd[0]);
		close(fd[1]);
		return;
	}

	/* The original process turns into the PAGER */
	dup2(fd[0], 0);
	close(fd[0]);
	close(fd[1]);

	setenv("LESS", "FRSX", 0);
	run_pager(pager);
	die("unable to execute pager '%s'", pager);
	exit(255);
#else
	/* spawn the pager */
	pager_argv[2] = pager;
	if (start_command(&pager_process))
		return;

	/* original process continues, but writes to the pipe */
	dup2(pager_process.in, 1);
	dup2(pager_process.in, 2);
	close(pager_process.in);

	/* this makes sure that the parent terminates after the pager */
	atexit(wait_for_pager);
#endif
}

int pager_in_use(void)
{
	const char *env;

	if (spawned_pager)
		return 1;

	env = getenv("GIT_PAGER_IN_USE");
	return env ? git_config_bool("GIT_PAGER_IN_USE", env) : 0;
}
