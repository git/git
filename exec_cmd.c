#include "cache.h"
#include "exec_cmd.h"
#include "quote.h"
#include "spawn-pipe.h"
#define MAX_ARGS	32

extern char **environ;
static const char *builtin_exec_path = GIT_EXEC_PATH;
static const char *current_exec_path;

void git_set_exec_path(const char *exec_path)
{
	current_exec_path = exec_path;
}


/* Returns the highest-priority, location to look for git programs. */
const char *git_exec_path(void)
{
	const char *env;

	if (current_exec_path)
		return current_exec_path;

	env = getenv(EXEC_PATH_ENVIRONMENT);
	if (env && *env) {
		return env;
	}

	return builtin_exec_path;
}


int execv_git_cmd(const char **argv)
{
	char git_command[PATH_MAX + 1];
	int i;
	const char *paths[] = { current_exec_path,
				getenv(EXEC_PATH_ENVIRONMENT),
				builtin_exec_path };

	for (i = 0; i < ARRAY_SIZE(paths); ++i) {
		size_t len;
		int rc;
		const char *exec_dir = paths[i];
		const char *tmp;

		if (!exec_dir || !*exec_dir) continue;

#ifdef __MINGW32__
		if (*exec_dir != '/' && exec_dir[1] != ':') {
#else
		if (*exec_dir != '/') {
#endif
			if (!getcwd(git_command, sizeof(git_command))) {
				fprintf(stderr, "git: cannot determine "
					"current directory: %s\n",
					strerror(errno));
				break;
			}
			len = strlen(git_command);

			/* Trivial cleanup */
			while (!strncmp(exec_dir, "./", 2)) {
				exec_dir += 2;
				while (*exec_dir == '/')
					exec_dir++;
			}

			rc = snprintf(git_command + len,
				      sizeof(git_command) - len, "/%s",
				      exec_dir);
			if (rc < 0 || rc >= sizeof(git_command) - len) {
				fprintf(stderr, "git: command name given "
					"is too long.\n");
				break;
			}
		} else {
			if (strlen(exec_dir) + 1 > sizeof(git_command)) {
				fprintf(stderr, "git: command name given "
					"is too long.\n");
				break;
			}
			strcpy(git_command, exec_dir);
		}

		len = strlen(git_command);
		rc = snprintf(git_command + len, sizeof(git_command) - len,
			      "/git-%s", argv[0]);
		if (rc < 0 || rc >= sizeof(git_command) - len) {
			fprintf(stderr,
				"git: command name given is too long.\n");
			break;
		}

		/* argv[0] must be the git command, but the argv array
		 * belongs to the caller, and my be reused in
		 * subsequent loop iterations. Save argv[0] and
		 * restore it on error.
		 */

		tmp = argv[0];
		argv[0] = git_command;

		trace_argv_printf(argv, -1, "trace: exec:");

		/* execve() can only ever return if it fails */
		execve(git_command, (char **)argv, environ);

		trace_printf("trace: exec failed: %s\n", strerror(errno));

		argv[0] = tmp;
	}
	return -1;

}


int execl_git_cmd(const char *cmd,...)
{
	int argc;
	const char *argv[MAX_ARGS + 1];
	const char *arg;
	va_list param;

	va_start(param, cmd);
	argv[0] = cmd;
	argc = 1;
	while (argc < MAX_ARGS) {
		arg = argv[argc++] = va_arg(param, char *);
		if (!arg)
			break;
	}
	va_end(param);
	if (MAX_ARGS <= argc)
		return error("too many args to run %s", cmd);

	argv[argc] = NULL;
	return execv_git_cmd(argv);
}

int spawnv_git_cmd(const char **argv, int pin[2], int pout[2])
{
	char cmd[100];
	int i, rc;
	pid_t pid;
	const char *paths[] = { current_exec_path,
				getenv(EXEC_PATH_ENVIRONMENT),
				builtin_exec_path };
	char p[3][PATH_MAX + 1];
	char *usedpaths[4], **up = usedpaths;
	const char *tmp;

	for (i = 0; i < ARRAY_SIZE(paths); ++i) {
		size_t len;
		const char *exec_dir = paths[i];

		if (!exec_dir || !*exec_dir) continue;

#ifdef __MINGW32__
		if (*exec_dir != '/' && exec_dir[1] != ':') {
#else
		if (*exec_dir != '/') {
#endif
			if (!getcwd(p[i], sizeof(p[i]))) {
				fprintf(stderr, "git: cannot determine "
					"current directory: %s\n",
					strerror(errno));
				return -1;
			}
			len = strlen(p[i]);

			/* Trivial cleanup */
			while (!strncmp(exec_dir, "./", 2)) {
				exec_dir += 2;
				while (*exec_dir == '/')
					exec_dir++;
			}

			rc = snprintf(p[i] + len,
				      sizeof(p[i]) - len, "/%s",
				      exec_dir);
			if (rc < 0 || rc >= sizeof(p[i]) - len) {
				fprintf(stderr, "git: command name given "
					"is too long.\n");
				return -1;
			}
		} else {
			if (strlen(exec_dir) + 1 > sizeof(p[i])) {
				fprintf(stderr, "git: command name given "
					"is too long.\n");
				return -1;
			}
			strcpy(p[i], exec_dir);
		}
		*up++ = p[i];
	}
	*up = NULL;

	rc = snprintf(cmd, sizeof(cmd), "git-%s", argv[0]);
	if (rc < 0 || rc >= sizeof(cmd)) {
		fprintf(stderr,
			"git: command name given is too long.\n");
		return -1;
	}

	/* argv[0] must be the git command, but the argv array
	 * belongs to the caller.  Save argv[0] and
	 * restore it later.
	 */

	tmp = argv[0];
	argv[0] = cmd;

	trace_argv_printf(argv, -1, "trace: exec:");

	pid = spawnvppe_pipe(cmd, argv, environ, usedpaths,
		pin, pout);

	argv[0] = tmp;
	return pid;

}
