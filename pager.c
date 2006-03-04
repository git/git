#include "cache.h"

/*
 * This is split up from the rest of git so that we might do
 * something different on Windows, for example.
 */

static void run_pager(void)
{
	const char *prog = getenv("PAGER");
	if (!prog)
		prog = "less";
	setenv("LESS", "-S", 0);
	execlp(prog, prog, NULL);
}

void setup_pager(void)
{
	pid_t pid;
	int fd[2];

	if (!isatty(1))
		return;
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
		close(fd[0]);
		close(fd[1]);
		return;
	}

	/* The original process turns into the PAGER */
	dup2(fd[0], 0);
	close(fd[0]);
	close(fd[1]);

	run_pager();
	exit(255);
}
