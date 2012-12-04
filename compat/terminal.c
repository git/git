#include "git-compat-util.h"
#include "compat/terminal.h"
#include "sigchain.h"
#include "strbuf.h"

#ifdef HAVE_DEV_TTY

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

static void restore_term_on_signal(int sig)
{
	restore_term();
	sigchain_pop(sig);
	raise(sig);
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

char *git_terminal_prompt(const char *prompt, int echo)
{
	static struct strbuf buf = STRBUF_INIT;
	int r;
	FILE *fh;

	fh = fopen("/dev/tty", "w+");
	if (!fh)
		return NULL;

	if (!echo && disable_echo()) {
		fclose(fh);
		return NULL;
	}

	fputs(prompt, fh);
	fflush(fh);

	r = strbuf_getline(&buf, fh, '\n');
	if (!echo) {
		fseek(fh, SEEK_CUR, 0);
		putc('\n', fh);
		fflush(fh);
	}

	restore_term();
	fclose(fh);

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
