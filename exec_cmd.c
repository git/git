#include "cache.h"
#include "exec_cmd.h"
#include "quote.h"
#define MAX_ARGS	32

extern char **environ;
static const char *argv_exec_path;
static const char *argv0_path;

const char *system_path(const char *path)
{
	if (!is_absolute_path(path) && argv0_path) {
		struct strbuf d = STRBUF_INIT;
		strbuf_addf(&d, "%s/%s", argv0_path, path);
		path = strbuf_detach(&d, NULL);
	}
	return path;
}

void git_set_argv0_path(const char *path)
{
	argv0_path = path;
}

void git_set_argv_exec_path(const char *exec_path)
{
	argv_exec_path = exec_path;
}


/* Returns the highest-priority, location to look for git programs. */
const char *git_exec_path(void)
{
	const char *env;

	if (argv_exec_path)
		return argv_exec_path;

	env = getenv(EXEC_PATH_ENVIRONMENT);
	if (env && *env) {
		return env;
	}

	return system_path(GIT_EXEC_PATH);
}

static void add_path(struct strbuf *out, const char *path)
{
	if (path && *path) {
		if (is_absolute_path(path))
			strbuf_addstr(out, path);
		else
			strbuf_addstr(out, make_nonrelative_path(path));

		strbuf_addch(out, PATH_SEP);
	}
}

void setup_path(void)
{
	const char *old_path = getenv("PATH");
	struct strbuf new_path;

	strbuf_init(&new_path, 0);

	add_path(&new_path, argv_exec_path);
	add_path(&new_path, getenv(EXEC_PATH_ENVIRONMENT));
	add_path(&new_path, system_path(GIT_EXEC_PATH));
	add_path(&new_path, argv0_path);

	if (old_path)
		strbuf_addstr(&new_path, old_path);
	else
		strbuf_addstr(&new_path, "/usr/local/bin:/usr/bin:/bin");

	setenv("PATH", new_path.buf, 1);

	strbuf_release(&new_path);
}

const char **prepare_git_cmd(const char **argv)
{
	int argc;
	const char **nargv;

	for (argc = 0; argv[argc]; argc++)
		; /* just counting */
	nargv = xmalloc(sizeof(*nargv) * (argc + 2));

	nargv[0] = "git";
	for (argc = 0; argv[argc]; argc++)
		nargv[argc + 1] = argv[argc];
	nargv[argc + 1] = NULL;
	return nargv;
}

int execv_git_cmd(const char **argv) {
	const char **nargv = prepare_git_cmd(argv);
	trace_argv_printf(nargv, "trace: exec:");

	/* execvp() can only ever return if it fails */
	execvp("git", (char **)nargv);

	trace_printf("trace: exec failed: %s\n", strerror(errno));

	free(nargv);
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
