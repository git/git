#include "git-compat-util.h"
#include "compat/terminal.h"
#include "sigchain.h"
#include "strbuf.h"

#if defined(HAVE_DEV_TTY) || defined(WIN32)

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

#elif defined(WIN32)

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

#endif

#ifndef FORCE_TEXT
#define FORCE_TEXT
#endif

char *git_terminal_prompt(const char *prompt, int echo)
{
	static struct strbuf buf = STRBUF_INIT;
	int r;
	FILE *input_fh, *output_fh;

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
