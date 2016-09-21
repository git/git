#include "cache.h"
#include "run-command.h"
#include "sigchain.h"

#ifndef DEFAULT_PAGER
#define DEFAULT_PAGER "less"
#endif

static struct child_process pager_process = CHILD_PROCESS_INIT;
static const char *pager_program;

static void wait_for_pager(int in_signal)
{
	if (!in_signal) {
		fflush(stdout);
		fflush(stderr);
	}
	/* signal EOF to pager */
	close(1);
	close(2);
	if (in_signal)
		finish_command_in_signal(&pager_process);
	else
		finish_command(&pager_process);
}

static void wait_for_pager_atexit(void)
{
	wait_for_pager(0);
}

static void wait_for_pager_signal(int signo)
{
	wait_for_pager(1);
	sigchain_pop(signo);
	raise(signo);
}

static int core_pager_config(const char *var, const char *value, void *data)
{
	if (!strcmp(var, "core.pager"))
		return git_config_string(&pager_program, var, value);
	return 0;
}

static void read_early_config(config_fn_t cb, void *data)
{
	git_config_with_options(cb, data, NULL, 1);

	/*
	 * Note that this is a really dirty hack that does the wrong thing in
	 * many cases. The crux of the problem is that we cannot run
	 * setup_git_directory() early on in git's setup, so we have no idea if
	 * we are in a repository or not, and therefore are not sure whether
	 * and how to read repository-local config.
	 *
	 * So if we _aren't_ in a repository (or we are but we would reject its
	 * core.repositoryformatversion), we'll read whatever is in .git/config
	 * blindly. Similarly, if we _are_ in a repository, but not at the
	 * root, we'll fail to find .git/config (because it's really
	 * ../.git/config, etc). See t7006 for a complete set of failures.
	 *
	 * However, we have historically provided this hack because it does
	 * work some of the time (namely when you are at the top-level of a
	 * valid repository), and would rarely make things worse (i.e., you do
	 * not generally have a .git/config file sitting around).
	 */
	if (!startup_info->have_repository) {
		struct git_config_source repo_config;

		memset(&repo_config, 0, sizeof(repo_config));
		repo_config.file = ".git/config";
		git_config_with_options(cb, data, &repo_config, 1);
	}
}

const char *git_pager(int stdout_is_tty)
{
	const char *pager;

	if (!stdout_is_tty)
		return NULL;

	pager = getenv("GIT_PAGER");
	if (!pager) {
		if (!pager_program)
			read_early_config(core_pager_config, NULL);
		pager = pager_program;
	}
	if (!pager)
		pager = getenv("PAGER");
	if (!pager)
		pager = DEFAULT_PAGER;
	if (!*pager || !strcmp(pager, "cat"))
		pager = NULL;

	return pager;
}

static void setup_pager_env(struct argv_array *env)
{
	const char **argv;
	int i;
	char *pager_env = xstrdup(PAGER_ENV);
	int n = split_cmdline(pager_env, &argv);

	if (n < 0)
		die("malformed build-time PAGER_ENV: %s",
			split_cmdline_strerror(n));

	for (i = 0; i < n; i++) {
		char *cp = strchr(argv[i], '=');

		if (!cp)
			die("malformed build-time PAGER_ENV");

		*cp = '\0';
		if (!getenv(argv[i])) {
			*cp = '=';
			argv_array_push(env, argv[i]);
		}
	}
	free(pager_env);
	free(argv);
}

void prepare_pager_args(struct child_process *pager_process, const char *pager)
{
	argv_array_push(&pager_process->args, pager);
	pager_process->use_shell = 1;
	setup_pager_env(&pager_process->env_array);
}

void setup_pager(void)
{
	const char *pager = git_pager(isatty(1));

	if (!pager)
		return;

	/*
	 * force computing the width of the terminal before we redirect
	 * the standard output to the pager.
	 */
	(void) term_columns();

	setenv("GIT_PAGER_IN_USE", "true", 1);

	/* spawn the pager */
	prepare_pager_args(&pager_process, pager);
	pager_process.in = -1;
	argv_array_push(&pager_process.env_array, "GIT_PAGER_IN_USE");
	if (start_command(&pager_process))
		return;

	/* original process continues, but writes to the pipe */
	dup2(pager_process.in, 1);
	if (isatty(2))
		dup2(pager_process.in, 2);
	close(pager_process.in);

	/* this makes sure that the parent terminates after the pager */
	sigchain_push_common(wait_for_pager_signal);
	atexit(wait_for_pager_atexit);
}

int pager_in_use(void)
{
	const char *env;
	env = getenv("GIT_PAGER_IN_USE");
	return env ? git_config_bool("GIT_PAGER_IN_USE", env) : 0;
}

/*
 * Return cached value (if set) or $COLUMNS environment variable (if
 * set and positive) or ioctl(1, TIOCGWINSZ).ws_col (if positive),
 * and default to 80 if all else fails.
 */
int term_columns(void)
{
	static int term_columns_at_startup;

	char *col_string;
	int n_cols;

	if (term_columns_at_startup)
		return term_columns_at_startup;

	term_columns_at_startup = 80;

	col_string = getenv("COLUMNS");
	if (col_string && (n_cols = atoi(col_string)) > 0)
		term_columns_at_startup = n_cols;
#ifdef TIOCGWINSZ
	else {
		struct winsize ws;
		if (!ioctl(1, TIOCGWINSZ, &ws) && ws.ws_col)
			term_columns_at_startup = ws.ws_col;
	}
#endif

	return term_columns_at_startup;
}

/*
 * How many columns do we need to show this number in decimal?
 */
int decimal_width(uintmax_t number)
{
	int width;

	for (width = 1; number >= 10; width++)
		number /= 10;
	return width;
}

struct pager_command_config_data {
	const char *cmd;
	int want;
	char *value;
};

static int pager_command_config(const char *var, const char *value, void *vdata)
{
	struct pager_command_config_data *data = vdata;
	const char *cmd;

	if (skip_prefix(var, "pager.", &cmd) && !strcmp(cmd, data->cmd)) {
		int b = git_config_maybe_bool(var, value);
		if (b >= 0)
			data->want = b;
		else {
			data->want = 1;
			data->value = xstrdup(value);
		}
	}

	return 0;
}

/* returns 0 for "no pager", 1 for "use pager", and -1 for "not specified" */
int check_pager_config(const char *cmd)
{
	struct pager_command_config_data data;

	data.cmd = cmd;
	data.want = -1;
	data.value = NULL;

	read_early_config(pager_command_config, &data);

	if (data.value)
		pager_program = data.value;
	return data.want;
}
