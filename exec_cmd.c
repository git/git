#include "cache.h"
#include "exec_cmd.h"
#include "quote.h"
#include "argv-array.h"
#define MAX_ARGS	32

static const char *argv_exec_path;
static const char *argv0_path;

char *system_path(const char *path)
{
#ifdef RUNTIME_PREFIX
	static const char *prefix;
#else
	static const char *prefix = FALLBACK_RUNTIME_PREFIX;
#endif
	struct strbuf d = STRBUF_INIT;

	if (is_absolute_path(path))
		return xstrdup(path);

#ifdef RUNTIME_PREFIX
	assert(argv0_path);
	assert(is_absolute_path(argv0_path));

	if (!prefix &&
	    !(prefix = strip_path_suffix(argv0_path, GIT_EXEC_PATH)) &&
	    !(prefix = strip_path_suffix(argv0_path, BINDIR)) &&
	    !(prefix = strip_path_suffix(argv0_path, "git"))) {
		prefix = FALLBACK_RUNTIME_PREFIX;
		trace_printf("RUNTIME_PREFIX requested, "
				"but prefix computation failed.  "
				"Using static fallback '%s'.\n", prefix);
	}
#endif

	strbuf_addf(&d, "%s/%s", prefix, path);
	return strbuf_detach(&d, NULL);
}

void git_extract_argv0_path(const char *argv0)
{
	const char *slash;

	if (!argv0 || !*argv0)
		return;

	slash = find_last_dir_sep(argv0);

	if (slash)
		argv0_path = xstrndup(argv0, slash - argv0);
}

void git_set_argv_exec_path(const char *exec_path)
{
	argv_exec_path = exec_path;
	/*
	 * Propagate this setting to external programs.
	 */
	setenv(EXEC_PATH_ENVIRONMENT, exec_path, 1);
}


/* Returns the highest-priority, location to look for git programs. */
const char *git_exec_path(void)
{
	static char *cached_exec_path;

	if (argv_exec_path)
		return argv_exec_path;

	if (!cached_exec_path) {
		const char *env = getenv(EXEC_PATH_ENVIRONMENT);
		if (env && *env)
			cached_exec_path = xstrdup(env);
		else
			cached_exec_path = system_path(GIT_EXEC_PATH);
	}
	return cached_exec_path;
}

static void add_path(struct strbuf *out, const char *path)
{
	if (path && *path) {
		strbuf_add_absolute_path(out, path);
		strbuf_addch(out, PATH_SEP);
	}
}

void setup_path(void)
{
	const char *old_path = getenv("PATH");
	struct strbuf new_path = STRBUF_INIT;

	add_path(&new_path, git_exec_path());

	if (old_path)
		strbuf_addstr(&new_path, old_path);
	else
		strbuf_addstr(&new_path, _PATH_DEFPATH);

	setenv("PATH", new_path.buf, 1);

	strbuf_release(&new_path);
}

const char **prepare_git_cmd(struct argv_array *out, const char **argv)
{
	argv_array_push(out, "git");
	argv_array_pushv(out, argv);
	return out->argv;
}

int execv_git_cmd(const char **argv) {
	struct argv_array nargv = ARGV_ARRAY_INIT;

	prepare_git_cmd(&nargv, argv);
	trace_argv_printf(nargv.argv, "trace: exec:");

	/* execvp() can only ever return if it fails */
	sane_execvp("git", (char **)nargv.argv);

	trace_printf("trace: exec failed: %s\n", strerror(errno));

	argv_array_clear(&nargv);
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
