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
	int need_in, need_out, need_err;
	int fdin[2] = { -1, -1 };
	int fdout[2] = { -1, -1 };
	int fderr[2] = { -1, -1 };
	const char **env = (const char **)environ;

	need_in = !cmd->no_stdin && cmd->in < 0;
	if (need_in) {
		if (pipe(fdin) < 0)
			return -ERR_RUN_COMMAND_PIPE;
		cmd->in = fdin[1];
		cmd->close_in = 1;
	}

	need_out = !cmd->no_stdout
		&& !cmd->stdout_to_stderr
		&& cmd->out < 0;
	if (need_out) {
		if (pipe(fdout) < 0) {
			if (need_in)
				close_pair(fdin);
			return -ERR_RUN_COMMAND_PIPE;
		}
		cmd->out = fdout[0];
		cmd->close_out = 1;
	}

	need_err = !cmd->no_stderr && cmd->err < 0;
	if (need_err) {
		if (pipe(fderr) < 0) {
			if (need_in)
				close_pair(fdin);
			if (need_out)
				close_pair(fdout);
			return -ERR_RUN_COMMAND_PIPE;
		}
		cmd->err = fderr[0];
	}

	{
		if (cmd->no_stdin)
			fdin[0] = open("/dev/null", O_RDWR);
		else if (need_in) {
			/* nothing */
		} else if (cmd->in) {
			fdin[0] = cmd->in;
		}

		if (cmd->no_stdout)
			fdout[1] = open("/dev/null", O_RDWR);
		else if (cmd->stdout_to_stderr)
			fdout[1] = dup(2);
		else if (need_out) {
			/* nothing */
		} else if (cmd->out > 1) {
			fdout[1] = cmd->out;
		}

		if (cmd->no_stderr)
			fderr[1] = open("/dev/null", O_RDWR);
		else if (need_err) {
			/* nothing */
		}

		if (cmd->dir)
			die("chdir in start_command() not implemented");
		if (cmd->dir && chdir(cmd->dir))
			die("exec %s: cd to %s failed (%s)", cmd->argv[0],
			    cmd->dir, strerror(errno));
		if (cmd->env) {
			if (cmd->git_cmd)
				die("modifying environment for git_cmd in start_command() not implemented");
			env = copy_environ();
			for (; *cmd->env; cmd->env++) {
				if (strchr(*cmd->env, '='))
					die("setting environment in start_command() not implemented");
				else
					env_unsetenv(env, *cmd->env);
			}
		}
		if (cmd->git_cmd) {
			cmd->pid = spawnv_git_cmd(cmd->argv, fdin, fdout);
		} else {
			cmd->pid = spawnvpe_pipe(cmd->argv[0], cmd->argv, env, fdin, fdout);
		}
	}
	if (cmd->pid < 0) {
		if (need_in)
			close_pair(fdin);
		if (need_out)
			close_pair(fdout);
		if (need_err)
			close_pair(fderr);
		return -ERR_RUN_COMMAND_FORK;
	}

	return 0;
}

static int wait_or_whine(pid_t pid)
{
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

int finish_command(struct child_process *cmd)
{
	if (cmd->close_in)
		close(cmd->in);
	if (cmd->close_out)
		close(cmd->out);
	return wait_or_whine(cmd->pid);
}

int run_command(struct child_process *cmd)
{
	int code = start_command(cmd);
	if (code)
		return code;
	return finish_command(cmd);
}

static void prepare_run_command_v_opt(struct child_process *cmd,
				      const char **argv,
				      int opt)
{
	memset(cmd, 0, sizeof(*cmd));
	cmd->argv = argv;
	cmd->no_stdin = opt & RUN_COMMAND_NO_STDIN ? 1 : 0;
	cmd->git_cmd = opt & RUN_GIT_CMD ? 1 : 0;
	cmd->stdout_to_stderr = opt & RUN_COMMAND_STDOUT_TO_STDERR ? 1 : 0;
}

int run_command_v_opt(const char **argv, int opt)
{
	struct child_process cmd;
	prepare_run_command_v_opt(&cmd, argv, opt);
	return run_command(&cmd);
}

int run_command_v_opt_cd(const char **argv, int opt, const char *dir)
{
	struct child_process cmd;
	prepare_run_command_v_opt(&cmd, argv, opt);
	cmd.dir = dir;
	return run_command(&cmd);
}

int run_command_v_opt_cd_env(const char **argv, int opt, const char *dir, const char *const *env)
{
	struct child_process cmd;
	prepare_run_command_v_opt(&cmd, argv, opt);
	cmd.dir = dir;
	cmd.env = env;
	return run_command(&cmd);
}

#ifdef __MINGW32__
static __stdcall unsigned run_thread(void *data)
{
	struct async *async = data;
	return async->proc(async->fd_for_proc, async->data);
}
#endif

int start_async(struct async *async)
{
	int pipe_out[2];

	if (pipe(pipe_out) < 0)
		return error("cannot create pipe: %s", strerror(errno));
	async->out = pipe_out[0];

#ifndef __MINGW32__
	async->pid = fork();
	if (async->pid < 0) {
		error("fork (async) failed: %s", strerror(errno));
		close_pair(pipe_out);
		return -1;
	}
	if (!async->pid) {
		close(pipe_out[0]);
		exit(!!async->proc(pipe_out[1], async->data));
	}
	close(pipe_out[1]);
#else
	async->fd_for_proc = pipe_out[1];
	async->tid = (HANDLE) _beginthreadex(NULL, 0, run_thread, async, 0, NULL);
	if (!async->tid) {
		error("cannot create thread: %s", strerror(errno));
		close_pair(pipe_out);
		return -1;
	}
#endif
	return 0;
}

int finish_async(struct async *async)
{
#ifndef __MINGW32__
	int ret = 0;

	if (wait_or_whine(async->pid))
		ret = error("waitpid (async) failed");
#else
	DWORD ret = 0;
	if (WaitForSingleObject(async->tid, INFINITE) != WAIT_OBJECT_0)
		ret = error("waiting for thread failed: %lu", GetLastError());
	else if (!GetExitCodeThread(async->tid, &ret))
		ret = error("cannot get thread exit code: %lu", GetLastError());
	CloseHandle(async->tid);
#endif
	return ret;
}
