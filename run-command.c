#include "cache.h"
#include "run-command.h"
#include "exec_cmd.h"
#include "spawn-pipe.h"

int run_command_v_opt(const char **argv, int flags)
{
	pid_t pid;
	int fd_i[2] = { -1, -1 };
	int fd_o[2] = { -1, -1 };

	if (flags & RUN_COMMAND_NO_STDIN) {
		fd_i[0] = open("/dev/null", O_RDWR);
	}
	if (flags & RUN_COMMAND_STDOUT_TO_STDERR)
		fd_o[1] = dup(2);

	if (flags & RUN_GIT_CMD) {
		pid = spawnv_git_cmd(argv, fd_i, fd_o);
	} else {
		pid = spawnvpe_pipe(argv[0], argv, environ, fd_i, fd_o);
	}
	if (pid < 0)
		return -ERR_RUN_COMMAND_FORK;
	for (;;) {
		int status, code;
		pid_t waiting = waitpid(pid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			error("waitpid failed (%s)", strerror(errno));
			return -ERR_RUN_COMMAND_WAITPID;
		}
		if (waiting != pid)
			return -ERR_RUN_COMMAND_WAITPID_WRONG_PID;
		if (WIFSIGNALED(status))
			return -ERR_RUN_COMMAND_WAITPID_SIGNAL;

		if (!WIFEXITED(status))
			return -ERR_RUN_COMMAND_WAITPID_NOEXIT;
		code = WEXITSTATUS(status);
		if (code)
			return -code;
		return 0;
	}
}
