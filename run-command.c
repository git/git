#include "cache.h"
#include "run-command.h"
#include "exec_cmd.h"
#include "spawn-pipe.h"

static inline void close_pair(int fd[2])
{
	close(fd[0]);
	close(fd[1]);
}

int start_command(struct child_process *cmd)
{
	int need_in = !cmd->no_stdin && cmd->in < 0;
	int fdin[2] = { -1, -1 };
	int fd_o[2] = { -1, -1 };

	if (need_in) {
		if (pipe(fdin) < 0)
			return -ERR_RUN_COMMAND_PIPE;
		cmd->in = fdin[1];
		cmd->close_in = 1;
	}

	{
		if (cmd->no_stdin) {
			fdin[0] = open("/dev/null", O_RDWR);
		} else if (need_in) {
			/* nothing */
		} else if (cmd->in) {
			fdin[0] = cmd->in;
		}

		if (cmd->stdout_to_stderr)
			fd_o[1] = dup(2);
		if (cmd->git_cmd) {
			cmd->pid = spawnv_git_cmd(cmd->argv, fdin, fd_o);
		} else {
			cmd->pid = spawnvpe_pipe(cmd->argv[0], cmd->argv, environ, fdin, fd_o);
		}
	}
	if (cmd->pid < 0) {
		if (need_in) {
			close_pair(fdin);
		}
		return -ERR_RUN_COMMAND_FORK;
	}

	return 0;
}

int finish_command(struct child_process *cmd)
{
	if (cmd->close_in)
		close(cmd->in);

	for (;;) {
		int status, code;
		pid_t waiting = waitpid(cmd->pid, &status, 0);

		if (waiting < 0) {
			if (errno == EINTR)
				continue;
			error("waitpid failed (%s)", strerror(errno));
			return -ERR_RUN_COMMAND_WAITPID;
		}
		if (waiting != cmd->pid)
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

int run_command(struct child_process *cmd)
{
	int code = start_command(cmd);
	if (code)
		return code;
	return finish_command(cmd);
}

int run_command_v_opt(const char **argv, int opt)
{
	struct child_process cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.argv = argv;
	cmd.no_stdin = opt & RUN_COMMAND_NO_STDIN ? 1 : 0;
	cmd.git_cmd = opt & RUN_GIT_CMD ? 1 : 0;
	cmd.stdout_to_stderr = opt & RUN_COMMAND_STDOUT_TO_STDERR ? 1 : 0;
	return run_command(&cmd);
}
