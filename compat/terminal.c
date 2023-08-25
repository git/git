#include "git-compat-util.h"
#include "compat/terminal.h"
#include "gettext.h"
#include "sigchain.h"
#include "strbuf.h"
#include "run-command.h"
#include "string-list.h"
#include "hashmap.h"

#if defined(HAVE_DEV_TTY) || defined(GIT_WINDOWS_NATIVE)

static void restore_term_on_signal(int sig)
{
	restore_term();
	/* restore_term calls sigchain_pop_common */
	raise(sig);
}

#ifdef HAVE_DEV_TTY

#define INPUT_PATH "/dev/tty"
#define OUTPUT_PATH "/dev/tty"

static volatile sig_atomic_t term_fd_needs_closing;
static int term_fd = -1;
static struct termios old_term;

static const char *background_resume_msg;
static const char *restore_error_msg;
static volatile sig_atomic_t ttou_received;

/* async safe error function for use by signal handlers. */
static void write_err(const char *msg)
{
	write_in_full(2, "error: ", strlen("error: "));
	write_in_full(2, msg, strlen(msg));
	write_in_full(2, "\n", 1);
}

static void print_background_resume_msg(int signo)
{
	int saved_errno = errno;
	sigset_t mask;
	struct sigaction old_sa;
	struct sigaction sa = { .sa_handler = SIG_DFL };

	ttou_received = 1;
	write_err(background_resume_msg);
	sigaction(signo, &sa, &old_sa);
	raise(signo);
	sigemptyset(&mask);
	sigaddset(&mask, signo);
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
	/* Stopped here */
	sigprocmask(SIG_BLOCK, &mask, NULL);
	sigaction(signo, &old_sa, NULL);
	errno = saved_errno;
}

static void restore_terminal_on_suspend(int signo)
{
	int saved_errno = errno;
	int res;
	struct termios t;
	sigset_t mask;
	struct sigaction old_sa;
	struct sigaction sa = { .sa_handler = SIG_DFL };
	int can_restore = 1;

	if (tcgetattr(term_fd, &t) < 0)
		can_restore = 0;

	if (tcsetattr(term_fd, TCSAFLUSH, &old_term) < 0)
		write_err(restore_error_msg);

	sigaction(signo, &sa, &old_sa);
	raise(signo);
	sigemptyset(&mask);
	sigaddset(&mask, signo);
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
	/* Stopped here */
	sigprocmask(SIG_BLOCK, &mask, NULL);
	sigaction(signo, &old_sa, NULL);
	if (!can_restore) {
		write_err(restore_error_msg);
		goto out;
	}
	/*
	 * If we resume in the background then we receive SIGTTOU when calling
	 * tcsetattr() below. Set up a handler to print an error message in that
	 * case.
	 */
	sigemptyset(&mask);
	sigaddset(&mask, SIGTTOU);
	sa.sa_mask = old_sa.sa_mask;
	sa.sa_handler = print_background_resume_msg;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGTTOU, &sa, &old_sa);
 again:
	ttou_received = 0;
	sigprocmask(SIG_UNBLOCK, &mask, NULL);
	res = tcsetattr(term_fd, TCSAFLUSH, &t);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	if (ttou_received)
		goto again;
	else if (res < 0)
		write_err(restore_error_msg);
	sigaction(SIGTTOU, &old_sa, NULL);
 out:
	errno = saved_errno;
}

static void reset_job_signals(void)
{
	if (restore_error_msg) {
		signal(SIGTTIN, SIG_DFL);
		signal(SIGTTOU, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		restore_error_msg = NULL;
		background_resume_msg = NULL;
	}
}

static void close_term_fd(void)
{
	if (term_fd_needs_closing)
		close(term_fd);
	term_fd_needs_closing = 0;
	term_fd = -1;
}

void restore_term(void)
{
	if (term_fd < 0)
		return;

	tcsetattr(term_fd, TCSAFLUSH, &old_term);
	close_term_fd();
	sigchain_pop_common();
	reset_job_signals();
}

int save_term(enum save_term_flags flags)
{
	struct sigaction sa;

	if (term_fd < 0)
		term_fd = ((flags & SAVE_TERM_STDIN)
			   ? 0
			   : open("/dev/tty", O_RDWR));
	if (term_fd < 0)
		return -1;
	term_fd_needs_closing = !(flags & SAVE_TERM_STDIN);
	if (tcgetattr(term_fd, &old_term) < 0) {
		close_term_fd();
		return -1;
	}
	sigchain_push_common(restore_term_on_signal);
	/*
	 * If job control is disabled then the shell will have set the
	 * disposition of SIGTSTP to SIG_IGN.
	 */
	sigaction(SIGTSTP, NULL, &sa);
	if (sa.sa_handler == SIG_IGN)
		return 0;

	/* avoid calling gettext() from signal handler */
	background_resume_msg = _("cannot resume in the background, please use 'fg' to resume");
	restore_error_msg = _("cannot restore terminal settings");
	sa.sa_handler = restore_terminal_on_suspend;
	sa.sa_flags = SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGTSTP);
	sigaddset(&sa.sa_mask, SIGTTIN);
	sigaddset(&sa.sa_mask, SIGTTOU);
	sigaction(SIGTSTP, &sa, NULL);
	sigaction(SIGTTIN, &sa, NULL);
	sigaction(SIGTTOU, &sa, NULL);

	return 0;
}

static int disable_bits(enum save_term_flags flags, tcflag_t bits)
{
	struct termios t;

	if (save_term(flags) < 0)
		return -1;

	t = old_term;

	t.c_lflag &= ~bits;
	if (bits & ICANON) {
		t.c_cc[VMIN] = 1;
		t.c_cc[VTIME] = 0;
	}
	if (!tcsetattr(term_fd, TCSAFLUSH, &t))
		return 0;

	sigchain_pop_common();
	reset_job_signals();
	close_term_fd();
	return -1;
}

static int disable_echo(enum save_term_flags flags)
{
	return disable_bits(flags, ECHO);
}

static int enable_non_canonical(enum save_term_flags flags)
{
	return disable_bits(flags, ICANON | ECHO);
}

/*
 * On macos it is not possible to use poll() with a terminal so use select
 * instead.
 */
static int getchar_with_timeout(int timeout)
{
	struct timeval tv, *tvp = NULL;
	fd_set readfds;
	int res;

 again:
	if (timeout >= 0) {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		tvp = &tv;
	}

	FD_ZERO(&readfds);
	FD_SET(0, &readfds);
	res = select(1, &readfds, NULL, NULL, tvp);
	if (!res)
		return EOF;
	if (res < 0) {
		if (errno == EINTR)
			goto again;
		else
			return EOF;
	}
	return getchar();
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

	sigchain_pop_common();

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

int save_term(enum save_term_flags flags)
{
	hconin = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
	    FILE_SHARE_READ, NULL, OPEN_EXISTING,
	    FILE_ATTRIBUTE_NORMAL, NULL);
	if (hconin == INVALID_HANDLE_VALUE)
		return -1;

	if (flags & SAVE_TERM_DUPLEX) {
		hconout = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
		if (hconout == INVALID_HANDLE_VALUE)
			goto error;

		GetConsoleMode(hconout, &cmode_out);
	}

	GetConsoleMode(hconin, &cmode_in);
	use_stty = 0;
	sigchain_push_common(restore_term_on_signal);
	return 0;
error:
	CloseHandle(hconin);
	hconin = INVALID_HANDLE_VALUE;
	return -1;
}

static int disable_bits(enum save_term_flags flags, DWORD bits)
{
	if (use_stty) {
		struct child_process cp = CHILD_PROCESS_INIT;

		strvec_push(&cp.args, "stty");

		if (bits & ENABLE_LINE_INPUT) {
			string_list_append(&stty_restore, "icanon");
			/*
			 * POSIX allows VMIN and VTIME to overlap with VEOF and
			 * VEOL - let's hope that is not the case on windows.
			 */
			strvec_pushl(&cp.args, "-icanon", "min", "1", "time", "0", NULL);
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

		if (run_command(&cp) == 0)
			return 0;

		/* `stty` could not be executed; access the Console directly */
		use_stty = 0;
	}

	if (save_term(flags) < 0)
		return -1;

	if (!SetConsoleMode(hconin, cmode_in & ~bits)) {
		CloseHandle(hconin);
		hconin = INVALID_HANDLE_VALUE;
		sigchain_pop_common();
		return -1;
	}

	return 0;
}

static int disable_echo(enum save_term_flags flags)
{
	return disable_bits(flags, ENABLE_ECHO_INPUT);
}

static int enable_non_canonical(enum save_term_flags flags)
{
	return disable_bits(flags,
			    ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
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

static int getchar_with_timeout(int timeout)
{
	struct pollfd pfd = { .fd = 0, .events = POLLIN };

	if (poll(&pfd, 1, timeout) < 1)
		return EOF;

	return getchar();
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

	if (!echo && disable_echo(0)) {
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

static int sequence_entry_cmp(const void *hashmap_cmp_fn_data UNUSED,
			      const struct hashmap_entry *he1,
			      const struct hashmap_entry *he2,
			      const void *keydata)
{
	const struct escape_sequence_entry
		*e1 = container_of(he1, const struct escape_sequence_entry, entry),
		*e2 = container_of(he2, const struct escape_sequence_entry, entry);
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

		hashmap_init(&sequences, sequence_entry_cmp, NULL, 0);

		strvec_pushl(&cp.args, "infocmp", "-L", "-1", NULL);
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

	if (warning_displayed || enable_non_canonical(SAVE_TERM_STDIN) < 0) {
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
			ch = getchar_with_timeout(500);
			if (ch == EOF)
				break;
			strbuf_addch(buf, ch);
		}
	}

	restore_term();
	return 0;
}

#else

int save_term(enum save_term_flags flags)
{
	/* no duplex support available */
	return -!!(flags & SAVE_TERM_DUPLEX);
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
