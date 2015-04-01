#include <inttypes.h>
#include "git-compat-util.h"
#include "run-command.h"
#include "compat/terminal.h"
#include "sigchain.h"
#include "strbuf.h"

#if defined(HAVE_DEV_TTY) || defined(GIT_WINDOWS_NATIVE)

static void restore_term(void);

static void restore_term_on_signal(int sig)
{
	restore_term();
	sigchain_pop(sig);
	raise(sig);
}

#ifdef HAVE_DEV_TTY

#define INPUT_PATH "/dev/tty"
#define OUTPUT_PATH "/dev/tty"

static int term_fd = -1;
static struct termios old_term;

static void restore_term(void)
{
	if (term_fd < 0)
		return;

	tcsetattr(term_fd, TCSAFLUSH, &old_term);
	close(term_fd);
	term_fd = -1;
}

static int disable_echo(void)
{
	struct termios t;

	term_fd = open("/dev/tty", O_RDWR);
	if (tcgetattr(term_fd, &t) < 0)
		goto error;

	old_term = t;
	sigchain_push_common(restore_term_on_signal);

	t.c_lflag &= ~ECHO;
	if (!tcsetattr(term_fd, TCSAFLUSH, &t))
		return 0;

error:
	close(term_fd);
	term_fd = -1;
	return -1;
}

#elif defined(GIT_WINDOWS_NATIVE)

#define INPUT_PATH "CONIN$"
#define OUTPUT_PATH "CONOUT$"
#define FORCE_TEXT "t"

static HANDLE hconin = INVALID_HANDLE_VALUE;
static DWORD cmode;

static void restore_term(void)
{
	if (hconin == INVALID_HANDLE_VALUE)
		return;

	SetConsoleMode(hconin, cmode);
	CloseHandle(hconin);
	hconin = INVALID_HANDLE_VALUE;
}

static int disable_echo(void)
{
	hconin = CreateFile("CONIN$", GENERIC_READ | GENERIC_WRITE,
	    FILE_SHARE_READ, NULL, OPEN_EXISTING,
	    FILE_ATTRIBUTE_NORMAL, NULL);
	if (hconin == INVALID_HANDLE_VALUE)
		return -1;

	GetConsoleMode(hconin, &cmode);
	sigchain_push_common(restore_term_on_signal);
	if (!SetConsoleMode(hconin, cmode & (~ENABLE_ECHO_INPUT))) {
		CloseHandle(hconin);
		hconin = INVALID_HANDLE_VALUE;
		return -1;
	}

	return 0;
}

static char *xterm_prompt(const char *prompt, int echo)
{
	const char *env = getenv("MSYS_TTY_HANDLES");
	const char *echo_off[] = { "sh", "-c", "stty -echo </dev/tty", NULL };
	const char *echo_on[] = { "sh", "-c", "stty echo </dev/tty", NULL };
	static char buffer[1024];
	DWORD len, dummy;
	size_t tty0, tty1, tty2;
	HANDLE in_handle, out_handle;

	if (!env || 3 != sscanf(env,
	    " %" SCNuPTR " %" SCNuPTR " %" SCNuPTR " ",
	    &tty0, &tty1, &tty2)) {
		warning("Cannot read from xterm");
		return NULL;
	}

	in_handle = (HANDLE)tty0;
	out_handle = (HANDLE)tty1;

	if (!echo && run_command_v_opt(echo_off, 0))
		warning("Could not disable echo on xterm");

	if (!WriteFile(out_handle, prompt, strlen(prompt), &dummy, NULL)) {
		warning("Could not write to xterm");
		return NULL;
	}

	if (!ReadFile(in_handle, buffer, 1024, &len, NULL)) {
		warning("Could not read from xterm");
		return NULL;
	}

	if (len && buffer[len - 1] == '\n')
		buffer[--len] = '\0';
	if (len && buffer[len - 1] == '\r')
		buffer[--len] = '\0';

	if (!echo) {
		if(run_command_v_opt(echo_on, 0))
			warning("Could not re-enable echo on xterm");
		WriteFile(out_handle, "\n", 1, &dummy, NULL);
	}

	return len == 0 ? NULL : buffer;
}

#endif

#ifndef FORCE_TEXT
#define FORCE_TEXT
#endif

char *git_terminal_prompt(const char *prompt, int echo)
{
	static struct strbuf buf = STRBUF_INIT;
	int r;
	FILE *input_fh, *output_fh;
#ifdef GIT_WINDOWS_NATIVE
	const char *term = getenv("TERM");

	if (term && starts_with(term, "xterm"))
		return xterm_prompt(prompt, echo);
#endif

	input_fh = fopen(INPUT_PATH, "r" FORCE_TEXT);
	if (!input_fh)
		return NULL;

	output_fh = fopen(OUTPUT_PATH, "w" FORCE_TEXT);
	if (!output_fh) {
		fclose(input_fh);
		return NULL;
	}

	if (!echo && disable_echo()) {
		fclose(input_fh);
		fclose(output_fh);
		return NULL;
	}

	fputs(prompt, output_fh);
	fflush(output_fh);

	r = strbuf_getline(&buf, input_fh, '\n');
	if (!echo) {
		putc('\n', output_fh);
		fflush(output_fh);
	}

	restore_term();
	fclose(input_fh);
	fclose(output_fh);

	if (r == EOF)
		return NULL;
	return buf.buf;
}

#else

char *git_terminal_prompt(const char *prompt, int echo)
{
	return getpass(prompt);
}

#endif
