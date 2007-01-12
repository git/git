#include "git-compat-util.h"
#include "spawn-pipe.h"

extern char **environ;

#ifdef __MINGW32__
static char *lookup_prog(const char *dir, const char *cmd, int tryexe)
{
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s.exe", dir, cmd);

	if (tryexe && access(path, 0) == 0)
		return xstrdup(path);
	path[strlen(path)-4] = '\0';
	if (access(path, 0) == 0)
		return xstrdup(path);
	return NULL;
}

/*
 * Determines the absolute path of cmd based on the PATH environment variable.
 * If cmd contains a slash or backslash, no lookup is performed.
 */
static char *path_lookup(const char *cmd)
{
	char *p, *envpath = getenv("PATH");
	char *prog = NULL;
	int len = strlen(cmd);
	int tryexe = len < 4 || strcasecmp(cmd+len-4, ".exe");

	if (strchr(cmd, '/') || strchr(cmd, '\\') ||
	    !envpath || !*envpath)
		envpath = "";
	envpath = xstrdup(envpath);
	p = envpath;
	while (p && !prog) {
		const char *dir = p;
		p = strchr(p, ';');
		if (p) *p++ = '\0';
		if (*dir)
			prog = lookup_prog(dir, cmd, tryexe);
	}
	free(envpath);
	if (!prog) {
		prog = lookup_prog(".", cmd, tryexe);
		if (!prog)
			prog = xstrdup(cmd);
	}
	return prog;
}
#endif

/* cmd specifies the command to invoke.
 * argv specifies its arguments; argv[0] will be replaced by the basename of cmd.
 * env specifies the environment.
 * pin and pout specify pipes; the read end of pin is made the standard input
 * of the spawned process, and the write end of pout is mad the standard output.
 * The respective unused ends of the pipes are closed both in the parent
 * process as well as in the child process.
 * Anyone of pin or pout can be NULL, or any one of the ends can be -1 to
 * indicate that no processing shall occur.
 */
int spawnvpe_pipe(const char *cmd, const char **argv, const char **env,
		  int pin[], int pout[])
{
	const char *cmd_basename = strrchr(cmd, '/');
	pid_t pid;

#ifdef __MINGW32__
	int s0 = -1, s1 = -1, argc;
	char *prog;
	const char **qargv, *interpr;

	if (!cmd_basename)
		cmd_basename = strrchr(cmd, '\\');
#endif

	if (!cmd_basename)
		cmd_basename = cmd;
	else
		cmd_basename++;
	argv[0] = cmd_basename;

#ifndef __MINGW32__
	pid = fork();
	if (pid < 0)
		die("unable to fork");
	if (!pid) {
		if (pin) {
			if (pin[0] >= 0) {
				dup2(pin[0], 0);
				close(pin[0]);
			}
			if (pin[1] >= 0)
				close(pin[1]);
		}
		if (pout) {
			if (pout[1] >= 0) {
				dup2(pout[1], 1);
				close(pout[1]);
			}
			if (pout[0] >= 0)
				close(pout[0]);
		}
		environ = env;
		execvp(cmd, argv);
		die("exec failed");
	}

	if (pin && pin[0] >= 0)
		close(pin[0]);
	if (pout && pout[1] >= 1)
		close(pout[1]);
#else
	if (pin) {
		if (pin[0] >= 0) {
			s0 = dup(0);
			dup2(pin[0], 0);
			close(pin[0]);
		}
	}
	if (pout) {
		if (pout[1] >= 0) {
			s1 = dup(1);
			dup2(pout[1], 1);
			close(pout[1]);
		}
	}

	prog = path_lookup(cmd);
	interpr = parse_interpreter(prog);

	for (argc = 0; argv[argc];) argc++;
	qargv = xmalloc((argc+2)*sizeof(char*));
	if (!interpr) {
		quote_argv(qargv, argv);
		pid = spawnve(_P_NOWAIT, prog, qargv, env);
	} else {
		qargv[0] = interpr;
		argv[0] = prog;
		quote_argv(&qargv[1], argv);
		pid = spawnvpe(_P_NOWAIT, interpr, qargv, env);
	}

	if (pid < 0)
		die("unable to run %s", cmd);

	free(qargv);		/* TODO: quoted args should be freed, too */
	free(prog);

	if (s0 >= 0) {
		dup2(s0, 0);
		close(s0);
	}
	if (s1 >= 0) {
		dup2(s1, 1);
		close(s1);
	}
#endif

	return pid;
}

const char **copy_environ()
{
	return copy_env(environ);
}

const char **copy_env(const char **env)
{
	const char **s;
	int n = 1;
	for (s = env; *s; s++)
		n++;
	s = xmalloc(n*sizeof(const char *));
	memcpy(s, env, n*sizeof(const char *));
	return s;
}

void env_unsetenv(const char **env, const char *name)
{
	int src, dst;
	size_t nmln;

	nmln = strlen(name);

	for (src = dst = 0; env[src]; ++src) {
		size_t enln;
		enln = strlen(env[src]);
		if (enln > nmln) {
			/* might match, and can test for '=' safely */
			if (0 == strncmp (env[src], name, nmln)
			    && '=' == env[src][nmln])
				/* matches, so skip */
				continue;
		}
		env[dst] = env[src];
		++dst;
	}
	env[dst] = NULL;
}
