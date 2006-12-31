#include "cache.h"
#include "run-command.h"
#include "exec_cmd.h"

int run_command_v_opt(const char **argv, int flags)
{
	pid_t pid = fork();

	if (pid < 0)
		return -ERR_RUN_COMMAND_FORK;
	if (!pid) {
		if (flags & RUN_COMMAND_NO_STDIO) {
			int fd = open("/dev/null", O_RDWR);
			dup2(fd, 0);
			dup2(fd, 1);
			close(fd);
		} else if (flags & RUN_COMMAND_STDOUT_TO_STDERR)
			dup2(2, 1);
		if (flags & RUN_GIT_CMD) {
			execv_git_cmd(argv);
		} else {
			execvp(argv[0], (char *const*) argv);
		}
		die("exec %s failed.", argv[0]);
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

int run_command_v(const char **argv)
{
	return run_command_v_opt(argv, 0);
}

static int run_command_va_opt(int opt, const char *cmd, va_list param)
{
	int argc;
	const char *argv[MAX_RUN_COMMAND_ARGS];
	const char *arg;

	argv[0] = (char*) cmd;
	argc = 1;
	while (argc < MAX_RUN_COMMAND_ARGS) {
		arg = argv[argc++] = va_arg(param, char *);
		if (!arg)
			break;
	}
	if (MAX_RUN_COMMAND_ARGS <= argc)
		return error("too many args to run %s", cmd);
	return run_command_v_opt(argv, opt);
}

int run_command_opt(int opt, const char *cmd, ...)
{
	va_list params;
	int r;

	va_start(params, cmd);
	r = run_command_va_opt(opt, cmd, params);
	va_end(params);
	return r;
}

int run_command(const char *cmd, ...)
{
	va_list params;
	int r;

	va_start(params, cmd);
	r = run_command_va_opt(0, cmd, params);
	va_end(params);
	return r;
}
