#include "git-compat-util.h"
#include "compat/terminal.h"
#include "sigchain.h"
#include "strbuf.h"
#include "run-command.h"
#include "string-list.h"
#include "hashmap.h"

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

/*
 * The `is_known_escape_sequence()` function returns 1 if the passed string
 * corresponds to an Escape sequence that the terminal capabilities contains.
 *
 * To avoid depending on ncurses or other platform-specific libraries, we rely
 * on the presence of the `infocmp` executable to do the job for us (failing
 * silently if the program is not available or refused to run).
 */
struct escape_sequence_entry {
	struct hashmap_entry entry;
	char sequence[FLEX_ARRAY];
};

static int sequence_entry_cmp(const void *hashmap_cmp_fn_data,
			      const struct escape_sequence_entry *e1,
			      const struct escape_sequence_entry *e2,
			      const void *keydata)
{
	return strcmp(e1->sequence, keydata ? keydata : e2->sequence);
}

static int is_known_escape_sequence(const char *sequence)
{
	static struct hashmap sequences;
	static int initialized;

	if (!initialized) {
		struct child_process cp = CHILD_PROCESS_INIT;
		struct strbuf buf = STRBUF_INIT;
		char *p, *eol;

		hashmap_init(&sequences, (hashmap_cmp_fn)sequence_entry_cmp,
			     NULL, 0);

		argv_array_pushl(&cp.args, "infocmp", "-L", "-1", NULL);
		if (pipe_command(&cp, NULL, 0, &buf, 0, NULL, 0))
			strbuf_setlen(&buf, 0);

		for (eol = p = buf.buf; *p; p = eol + 1) {
			p = strchr(p, '=');
			if (!p)
				break;
			p++;
			eol = strchrnul(p, '\n');

			if (starts_with(p, "\\E")) {
				char *comma = memchr(p, ',', eol - p);
				struct escape_sequence_entry *e;

				p[0] = '^';
				p[1] = '[';
				FLEX_ALLOC_MEM(e, sequence, p, comma - p);
				hashmap_entry_init(&e->entry,
						   strhash(e->sequence));
				hashmap_add(&sequences, &e->entry);
			}
			if (!*eol)
				break;
		}
		initialized = 1;
	}

	return !!hashmap_get_from_hash(&sequences, strhash(sequence), sequence);
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

		/*
		 * Query the terminal capabilities once about all the Escape
		 * sequences it knows about, so that we can avoid waiting for
		 * half a second when we know that the sequence is complete.
		 */
		while (!is_known_escape_sequence(buf->buf)) {
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
