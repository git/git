#include "cache.h"
#include "exec_cmd.h"
#include "pkt-line.h"
#include "sideband.h"
#include <sys/wait.h>

static pid_t setup_sideband(int sideband, const char *me, int fd[2], int xd[2])
{
	pid_t side_pid;

	if (!sideband) {
		fd[0] = xd[0];
		fd[1] = xd[1];
		return 0;
	}
	/* xd[] is talking with upload-pack; subprocess reads from
	 * xd[0], spits out band#2 to stderr, and feeds us band#1
	 * through our fd[0].
	 */
	if (pipe(fd) < 0)
		die("%s: unable to set up pipe", me);
	side_pid = fork();
	if (side_pid < 0)
		die("%s: unable to fork off sideband demultiplexer", me);
	if (!side_pid) {
		/* subprocess */
		close(fd[0]);
		if (xd[0] != xd[1])
			close(xd[1]);
		if (recv_sideband(me, xd[0], fd[1], 2))
			exit(1);
		exit(0);
	}
	close(xd[0]);
	close(fd[1]);
	fd[1] = xd[1];
	return side_pid;
}

static int get_pack(int xd[2], const char *me, int sideband, const char **argv)
{
	int status;
	pid_t pid, side_pid;
	int fd[2];

	side_pid = setup_sideband(sideband, me, fd, xd);
	pid = fork();
	if (pid < 0)
		die("%s: unable to fork off %s", me, argv[0]);
	if (!pid) {
		dup2(fd[0], 0);
		close(fd[0]);
		close(fd[1]);
		execv_git_cmd(argv);
		die("%s exec failed", argv[0]);
	}
	close(fd[0]);
	close(fd[1]);
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			die("waiting for %s: %s", argv[0], strerror(errno));
	}
	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);
		if (code)
			die("%s died with error code %d", argv[0], code);
		return 0;
	}
	if (WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		die("%s died of signal %d", argv[0], sig);
	}
	die("%s died of unnatural causes %d", argv[0], status);
}

int receive_unpack_pack(int xd[2], const char *me, int quiet, int sideband)
{
	const char *argv[3] = { "unpack-objects", quiet ? "-q" : NULL, NULL };
	return get_pack(xd, me, sideband, argv);
}

int receive_keep_pack(int xd[2], const char *me, int quiet, int sideband)
{
	const char *argv[5] = { "index-pack", "--stdin", "--fix-thin",
				quiet ? NULL : "-v", NULL };
	return get_pack(xd, me, sideband, argv);
}
