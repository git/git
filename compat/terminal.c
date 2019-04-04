#include "git-compat-util.h"
#include "compat/terminal.h"
#include "sigchain.h"
#include "strbuf.h"
#include "run-command.h"
#include "string-list.h"

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

#else

char *git_terminal_prompt(const char *prompt, int echo)
{
	return getpass(prompt);
}

#endif
