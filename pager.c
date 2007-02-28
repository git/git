#include "cache.h"
#include "spawn-pipe.h"

/*
 * This is split up from the rest of git so that we might do
 * something different on Windows, for example.
 */

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
static pid_t pager_pid;
static void collect_pager(void)
{
	fflush(stdout);
	close(1);	/* signals EOF to pager */
	cwait(NULL, pager_pid, 0);
}
#endif

void setup_pager(void)
{
#ifndef __MINGW32__
	pid_t pid;
#else
	const char *pager_argv[] = { "sh", "-c", NULL, NULL };
#endif
	int fd[2];
	const char *pager = getenv("GIT_PAGER");

	if (!isatty(1))
		return;
	if (!pager)
		pager = getenv("PAGER");
	if (!pager)
		pager = "less";
	else if (!*pager || !strcmp(pager, "cat"))
		return;

	pager_in_use = 1; /* means we are emitting to terminal */

	if (pipe(fd) < 0)
		return;
#ifndef __MINGW32__
	pid = fork();
	if (pid < 0) {
		close(fd[0]);
		close(fd[1]);
		return;
	}

	/* return in the child */
	if (!pid) {
		dup2(fd[1], 1);
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
	pager_pid = spawnvpe_pipe(pager_argv[0], pager_argv, environ, fd, NULL);
	if (pager_pid < 0)
		return;

	/* original process continues, but writes to the pipe */
	dup2(fd[1], 1);
	close(fd[1]);

	/* this makes sure that the parent terminates after the pager */
	atexit(collect_pager);
#endif
}
