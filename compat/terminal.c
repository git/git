#include "git-compat-util.h"
#include "compat/terminal.h"
#include "sigchain.h"
#include "strbuf.h"
#include "run-command.h"
#include "string-list.h"
#include "argv-array.h"

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

static int disable_bits(tcflag_t bits)
{
	struct termios t;

	term_fd = open("/dev/tty", O_RDWR);
	if (tcgetattr(term_fd, &t) < 0)
		goto error;

	old_term = t;
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
static DWORD cmode;

static void restore_term(void)
{
	if (use_stty) {
		int i;
		struct child_process cp = CHILD_PROCESS_INIT;

		if (stty_restore.nr == 0)
			return;

		argv_array_push(&cp.args, "stty");
		for (i = 0; i < stty_restore.nr; i++)
			argv_array_push(&cp.args, stty_restore.items[i].string);
		run_command(&cp);
		string_list_clear(&stty_restore, 0);
		return;
	}

	if (hconin == INVALID_HANDLE_VALUE)
		return;

	SetConsoleMode(hconin, cmode);
	CloseHandle(hconin);
	hconin = INVALID_HANDLE_VALUE;
}

static int disable_bits(DWORD bits)
{
	if (use_stty) {
		struct child_process cp = CHILD_PROCESS_INIT;

		argv_array_push(&cp.args, "stty");

		if (bits & ENABLE_LINE_INPUT) {
			string_list_append(&stty_restore, "icanon");
			argv_array_push(&cp.args, "-icanon");
		}

		if (bits & ENABLE_ECHO_INPUT) {
			string_list_append(&stty_restore, "echo");
			argv_array_push(&cp.args, "-echo");
		}

		if (bits & ENABLE_PROCESSED_INPUT) {
			string_list_append(&stty_restore, "-ignbrk");
			string_list_append(&stty_restore, "intr");
			string_list_append(&stty_restore, "^c");
			argv_array_push(&cp.args, "ignbrk");
			argv_array_push(&cp.args, "intr");
			argv_array_push(&cp.args, "");
		}

		if (run_command(&cp) == 0)
			return 0;

		/* `stty` could not be executed; access the Console directly */
		use_stty = 0;
	}

	hconin = CreateFile("CONIN$", GENERIC_READ | GENERIC_WRITE,
	    FILE_SHARE_READ, NULL, OPEN_EXISTING,
	    FILE_ATTRIBUTE_NORMAL, NULL);
	if (hconin == INVALID_HANDLE_VALUE)
		return -1;

	GetConsoleMode(hconin, &cmode);
	sigchain_push_common(restore_term_on_signal);
	if (!SetConsoleMode(hconin, cmode & ~bits)) {
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

/*
 * Override `getchar()`, as the default implementation does not use
 * `ReadFile()`.
 *
 * This poses a problem when we want to see whether the standard
 * input has more characters, as the default of Git for Windows is to start the
 * Bash in a MinTTY, which uses a named pipe to emulate a pty, in which case
 * our `poll()` emulation calls `PeekNamedPipe()`, which seems to require
 * `ReadFile()` to be called first to work properly (it only reports 0
 * available bytes, otherwise).
 *
 * So let's just override `getchar()` with a version backed by `ReadFile()` and
 * go our merry ways from here.
 */
static int mingw_getchar(void)
{
	DWORD read = 0;
	unsigned char ch;

	if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &ch, 1, &read, NULL))
		return EOF;

	if (!read) {
		error("Unexpected 0 read");
		return EOF;
	}

	return ch;
}
#define getchar mingw_getchar

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
	int ch;

	if (warning_displayed || enable_non_canonical() < 0) {
		if (!warning_displayed) {
			warning("reading single keystrokes not supported on "
				"this platform; reading line instead");
			warning_displayed = 1;
		}

		return strbuf_getline(buf, stdin);
	}

	strbuf_reset(buf);
	ch = getchar();
	if (ch == EOF) {
		restore_term();
		return EOF;
	}
	strbuf_addch(buf, ch);

	if (ch == '\033' /* ESC */) {
		/*
		 * We are most likely looking at an Escape sequence. Let's try
		 * to read more bytes, waiting at most half a second, assuming
		 * that the sequence is complete if we did not receive any byte
		 * within that time.
		 *
		 * Start by replacing the Escape byte with ^[ */
		strbuf_splice(buf, buf->len - 1, 1, "^[", 2);

		for (;;) {
			struct pollfd pfd = { .fd = 0, .events = POLLIN };

			if (poll(&pfd, 1, 500) < 1)
				break;

			ch = getchar();
			if (ch == EOF)
				return 0;
			strbuf_addch(buf, ch);
		}
	}

	restore_term();
	return 0;
}

#else

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
