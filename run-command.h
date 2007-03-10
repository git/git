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
	unsigned close_in:1;
	unsigned no_stdin:1;
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

#endif
