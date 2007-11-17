#include "git-compat-util.h"
#include "spawn-pipe.h"


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
#ifdef __MINGW32__
	char **path = mingw_get_path_split();

	pid_t pid = spawnvppe_pipe(cmd, argv, env, path, pin, pout);

	mingw_free_path_split(path);
#else
	pid_t pid = spawnvppe_pipe(cmd, argv, env, NULL, pin, pout);
#endif
	return pid;
}

int spawnvppe_pipe(const char *cmd, const char **argv, const char **env,
		  char **path,
		  int pin[], int pout[])
{
	const char *cmd_basename = strrchr(cmd, '/');
	const char *argv0 = argv[0];
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

	prog = mingw_path_lookup(cmd, path);
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

	argv[0] = argv0;

	return pid;
}

const char **copy_environ()
{
	return copy_env( (const char**)environ);
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
