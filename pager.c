#include "cache.h"
#include "run-command.h"
#include "sigchain.h"

#ifndef DEFAULT_PAGER
#define DEFAULT_PAGER "less"
#endif

/*
 * This is split up from the rest of git so that we can do
 * something different on Windows.
 */

static const char *pager_argv[] = { NULL, NULL };
static struct child_process pager_process = CHILD_PROCESS_INIT;

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

const char *git_pager(int stdout_is_tty)
{
	const char *pager;

	if (!stdout_is_tty)
		return NULL;

	pager = getenv("GIT_PAGER");
	if (!pager) {
		if (!pager_program)
			git_config(git_default_config, NULL);
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
	pager_argv[0] = pager;
	pager_process.use_shell = 1;
	pager_process.argv = pager_argv;
	pager_process.in = -1;
	if (!getenv("LESS"))
		argv_array_push(&pager_process.env_array, "LESS=FRX");
	if (!getenv("LV"))
		argv_array_push(&pager_process.env_array, "LV=-c");
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

/* returns 0 for "no pager", 1 for "use pager", and -1 for "not specified" */
int check_pager_config(const char *cmd)
{
	int want = -1;
	struct strbuf key = STRBUF_INIT;
	const char *value = NULL;
	strbuf_addf(&key, "pager.%s", cmd);
	if (git_config_key_is_valid(key.buf) &&
	    !git_config_get_value(key.buf, &value)) {
		int b = git_config_maybe_bool(key.buf, value);
		if (b >= 0)
			want = b;
		else {
			want = 1;
			pager_program = xstrdup(value);
		}
	}
	strbuf_release(&key);
	return want;
}
