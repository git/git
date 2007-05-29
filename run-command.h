#ifndef RUN_COMMAND_H
#define RUN_COMMAND_H

enum {
	ERR_RUN_COMMAND_FORK = 10000,
	ERR_RUN_COMMAND_EXEC,
	ERR_RUN_COMMAND_PIPE,
	ERR_RUN_COMMAND_WAITPID,
	ERR_RUN_COMMAND_WAITPID_WRONG_PID,
	ERR_RUN_COMMAND_WAITPID_SIGNAL,
	ERR_RUN_COMMAND_WAITPID_NOEXIT,
};

struct child_process {
	const char **argv;
	pid_t pid;
	int in;
	int out;
	const char *dir;
	const char *const *env;
	unsigned close_in:1;
	unsigned close_out:1;
	unsigned no_stdin:1;
	unsigned no_stdout:1;
	unsigned git_cmd:1; /* if this is to be git sub-command */
	unsigned stdout_to_stderr:1;
};

int start_command(struct child_process *);
int finish_command(struct child_process *);
int run_command(struct child_process *);

#define RUN_COMMAND_NO_STDIN 1
#define RUN_GIT_CMD	     2	/*If this is to be git sub-command */
#define RUN_COMMAND_STDOUT_TO_STDERR 4
int run_command_v_opt(const char **argv, int opt);
int run_command_v_opt_cd(const char **argv, int opt, const char *dir);

/*
 * env (the environment) is to be formatted like environ: "VAR=VALUE".
 * To unset an environment variable use just "VAR".
 */
int run_command_v_opt_cd_env(const char **argv, int opt, const char *dir, const char *const *env);

#endif
