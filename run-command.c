#include "cache.h"
#include "run-command.h"
#include "exec_cmd.h"

int run_command(struct child_process *cmd)
{
	pid_t pid = fork();

	if (pid < 0)
		return -ERR_RUN_COMMAND_FORK;
	if (!pid) {
		if (cmd->no_stdin) {
			int fd = open("/dev/null", O_RDWR);
			dup2(fd, 0);
			close(fd);
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
