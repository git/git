#include "cache.h"
#include "run-command.h"
#include "exec_cmd.h"

int start_command(struct child_process *cmd)
{
	int need_in = !cmd->no_stdin && cmd->in < 0;
	int fdin[2];

	if (need_in) {
		if (pipe(fdin) < 0)
			return -ERR_RUN_COMMAND_PIPE;
		cmd->in = fdin[1];
		cmd->close_in = 1;
	}

	cmd->pid = fork();
	if (cmd->pid < 0) {
		if (need_in) {
			close(fdin[0]);
			close(fdin[1]);
		}
		return -ERR_RUN_COMMAND_FORK;
	}

	if (!cmd->pid) {
		if (cmd->no_stdin) {
			int fd = open("/dev/null", O_RDWR);
			dup2(fd, 0);
			close(fd);
		} else if (need_in) {
			dup2(fdin[0], 0);
			close(fdin[0]);
			close(fdin[1]);
		} else if (cmd->in) {
			dup2(cmd->in, 0);
			close(cmd->in);
		}

		if (cmd->stdout_to_stderr)
			dup2(2, 1);
		if (cmd->git_cmd) {
			execv_git_cmd(cmd->argv);
		} else {
			execvp(cmd->argv[0], (char *const*) cmd->argv);
		}
		die("exec %s failed.", cmd->argv[0]);
	}

	if (need_in)
		close(fdin[0]);
	else if (cmd->in)
		close(cmd->in);

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
