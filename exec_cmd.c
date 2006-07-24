#include "cache.h"
#include "exec_cmd.h"
#include "quote.h"
#define MAX_ARGS	32

extern char **environ;
static const char *builtin_exec_path = GIT_EXEC_PATH;
static const char *current_exec_path = NULL;

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

	env = getenv("GIT_EXEC_PATH");
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
				getenv("GIT_EXEC_PATH"),
				builtin_exec_path };

	for (i = 0; i < ARRAY_SIZE(paths); ++i) {
		size_t len;
		int rc;
		const char *exec_dir = paths[i];
		const char *tmp;

		if (!exec_dir || !*exec_dir) continue;

		if (*exec_dir != '/') {
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

		if (getenv("GIT_TRACE")) {
			const char **p = argv;
			fputs("trace: exec:", stderr);
			while (*p) {
				fputc(' ', stderr);
				sq_quote_print(stderr, *p);
				++p;
			}
			putc('\n', stderr);
			fflush(stderr);
		}

		/* execve() can only ever return if it fails */
		execve(git_command, (char **)argv, environ);

		if (getenv("GIT_TRACE")) {
			fprintf(stderr, "trace: exec failed: %s\n",
				strerror(errno));
			fflush(stderr);
		}

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
