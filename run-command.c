#include "cache.h"
#include "run-command.h"
#include "exec_cmd.h"

static inline void close_pair(int fd[2])
{
	close(fd[0]);
	close(fd[1]);
}

int start_command(struct child_process *cmd)
{
	int need_in = !cmd->no_stdin && cmd->in < 0;
	int need_out = !cmd->stdout_to_stderr && cmd->out < 0;
	int fdin[2], fdout[2];

	if (need_in) {
		if (pipe(fdin) < 0)
			return -ERR_RUN_COMMAND_PIPE;
		cmd->in = fdin[1];
		cmd->close_in = 1;
	}

	if (need_out) {
		if (pipe(fdout) < 0) {
			if (need_in)
				close_pair(fdin);
			return -ERR_RUN_COMMAND_PIPE;
		}
		cmd->out = fdout[0];
		cmd->close_out = 1;
	}

	cmd->pid = fork();
	if (cmd->pid < 0) {
		if (need_in)
			close_pair(fdin);
		if (need_out)
			close_pair(fdout);
		return -ERR_RUN_COMMAND_FORK;
	}

	if (!cmd->pid) {
		if (cmd->no_stdin) {
			int fd = open("/dev/null", O_RDWR);
			dup2(fd, 0);
			close(fd);
		} else if (need_in) {
			dup2(fdin[0], 0);
			close_pair(fdin);
		} else if (cmd->in) {
			dup2(cmd->in, 0);
			close(cmd->in);
		}

		if (cmd->stdout_to_stderr)
			dup2(2, 1);
		else if (need_out) {
			dup2(fdout[1], 1);
			close_pair(fdout);
		} else if (cmd->out > 1) {
			dup2(cmd->out, 1);
			close(cmd->out);
		}

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

	if (need_out)
		close(fdout[1]);
	else if (cmd->out > 1)
		close(cmd->out);

	return 0;
}

int finish_command(struct child_process *cmd)
{
	if (cmd->close_in)
		close(cmd->in);
	if (cmd->close_out)
		close(cmd->out);

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
