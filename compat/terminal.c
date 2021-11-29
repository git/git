#include "git-compat-util.h"
#include "compat/terminal.h"
#include "sigchain.h"
#include "strbuf.h"
#include "run-command.h"
#include "string-list.h"
#include "hashmap.h"

#if defined(HAVE_DEV_TTY) || defined(GIT_WINDOWS_NATIVE)

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

void restore_term(void)
{
	if (term_fd < 0)
		return;

	tcsetattr(term_fd, TCSAFLUSH, &old_term);
	close(term_fd);
	term_fd = -1;
}

int save_term(int full_duplex)
{
	if (term_fd < 0)
		term_fd = open("/dev/tty", O_RDWR);

	return (term_fd < 0) ? -1 : tcgetattr(term_fd, &old_term);
}

static int disable_bits(tcflag_t bits)
{
	struct termios t;

	if (save_term(0) < 0)
		goto error;

	t = old_term;
	sigchain_push_common(restore_term_on_signal);

	t.c_lflag &= ~bits;
	if (!tcsetattr(term_fd, TCSAFLUSH, &t))
		return 0;

error:
	close(term_fd);
	term_fd = -1;
	return -1;
}

static int disable_echo(void)
{
	return disable_bits(ECHO);
}

static int enable_non_canonical(void)
{
	return disable_bits(ICANON | ECHO);
}

#elif defined(GIT_WINDOWS_NATIVE)

#define INPUT_PATH "CONIN$"
#define OUTPUT_PATH "CONOUT$"
#define FORCE_TEXT "t"

static int use_stty = 1;
static struct string_list stty_restore = STRING_LIST_INIT_DUP;
static HANDLE hconin = INVALID_HANDLE_VALUE;
static HANDLE hconout = INVALID_HANDLE_VALUE;
static DWORD cmode_in, cmode_out;

void restore_term(void)
{
	if (use_stty) {
		int i;
		struct child_process cp = CHILD_PROCESS_INIT;

		if (stty_restore.nr == 0)
			return;

		strvec_push(&cp.args, "stty");
		for (i = 0; i < stty_restore.nr; i++)
			strvec_push(&cp.args, stty_restore.items[i].string);
		run_command(&cp);
		string_list_clear(&stty_restore, 0);
		return;
	}

	if (hconin == INVALID_HANDLE_VALUE)
		return;

	SetConsoleMode(hconin, cmode_in);
	CloseHandle(hconin);
	if (cmode_out) {
		assert(hconout != INVALID_HANDLE_VALUE);
		SetConsoleMode(hconout, cmode_out);
		CloseHandle(hconout);
	}

	hconin = hconout = INVALID_HANDLE_VALUE;
}

int save_term(int full_duplex)
{
	hconin = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
	    FILE_SHARE_READ, NULL, OPEN_EXISTING,
	    FILE_ATTRIBUTE_NORMAL, NULL);
	if (hconin == INVALID_HANDLE_VALUE)
		return -1;

	if (full_duplex) {
		hconout = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
		if (hconout == INVALID_HANDLE_VALUE)
			goto error;

		GetConsoleMode(hconout, &cmode_out);
	}

	GetConsoleMode(hconin, &cmode_in);
	use_stty = 0;
	return 0;
error:
	CloseHandle(hconin);
	hconin = INVALID_HANDLE_VALUE;
	return -1;
}

static int disable_bits(DWORD bits)
{
	if (use_stty) {
		struct child_process cp = CHILD_PROCESS_INIT;

		strvec_push(&cp.args, "stty");

		if (bits & ENABLE_LINE_INPUT) {
			string_list_append(&stty_restore, "icanon");
			strvec_push(&cp.args, "-icanon");
		}

		if (bits & ENABLE_ECHO_INPUT) {
			string_list_append(&stty_restore, "echo");
			strvec_push(&cp.args, "-echo");
		}

		if (bits & ENABLE_PROCESSED_INPUT) {
			string_list_append(&stty_restore, "-ignbrk");
			string_list_append(&stty_restore, "intr");
			string_list_append(&stty_restore, "^c");
			strvec_push(&cp.args, "ignbrk");
			strvec_push(&cp.args, "intr");
			strvec_push(&cp.args, "");
		}

		cp.silent_exec_failure = 1;
		if (run_command(&cp) == 0)
			return 0;

		/* `stty` could not be executed; access the Console directly */
		use_stty = 0;
	}

	if (save_term(0) < 0)
		return -1;

	sigchain_push_common(restore_term_on_signal);
	if (!SetConsoleMode(hconin, cmode_in & ~bits)) {
		CloseHandle(hconin);
		hconin = INVALID_HANDLE_VALUE;
		return -1;
	}

	return 0;
}

static int disable_echo(void)
{
	return disable_bits(ENABLE_ECHO_INPUT);
}

static int enable_non_canonical(void)
{
	return disable_bits(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
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

	r = strbuf_getline_lf(&buf, input_fh);
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

int read_key_without_echo(struct strbuf *buf)
{
	static int warning_displayed;
	char input[8];
	ssize_t i, len;

	if (warning_displayed || enable_non_canonical() < 0) {
		if (!warning_displayed) {
			warning("reading single keystrokes not supported on "
				"this platform; reading line instead");
			warning_displayed = 1;
		}

		return strbuf_getline(buf, stdin);
	}

	strbuf_reset(buf);
	len = read(0, input, sizeof input);

	for (i = 0; i < len; i++) {
		if (input[i] == '\033')
			strbuf_addstr(buf, "^[");
		else
			strbuf_addch(buf, input[i]);
	}

	restore_term();
	return len ? 0 : EOF;
}

#else

int save_term(int full_duplex)
{
	/* full_duplex == 1, but no support available */
	return -full_duplex;
}

void restore_term(void)
{
}

char *git_terminal_prompt(const char *prompt, int echo)
{
	return getpass(prompt);
}

int read_key_without_echo(struct strbuf *buf)
{
	static int warning_displayed;
	const char *res;

	if (!warning_displayed) {
		warning("reading single keystrokes not supported on this "
			"platform; reading line instead");
		warning_displayed = 1;
	}

	res = getpass("");
	strbuf_reset(buf);
	if (!res)
		return EOF;
	strbuf_addstr(buf, res);
	return 0;
}

#endif
