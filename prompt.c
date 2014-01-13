#include "cache.h"
#include "run-command.h"
#include "strbuf.h"
#include "prompt.h"
#include "compat/terminal.h"

static char *do_askpass(const char *cmd, const char *prompt)
{
	struct child_process pass;
	const char *args[3];
	static struct strbuf buffer = STRBUF_INIT;
	int err = 0;

	args[0] = cmd;
	args[1]	= prompt;
	args[2] = NULL;

	memset(&pass, 0, sizeof(pass));
	pass.argv = args;
	pass.out = -1;

	if (start_command(&pass))
		return NULL;

	strbuf_reset(&buffer);
	if (strbuf_read(&buffer, pass.out, 20) < 0)
		err = 1;

	close(pass.out);

	if (finish_command(&pass))
		err = 1;

	if (err) {
		error("unable to read askpass response from '%s'", cmd);
		strbuf_release(&buffer);
		return NULL;
	}

	strbuf_setlen(&buffer, strcspn(buffer.buf, "\r\n"));

	return buffer.buf;
}

char *git_prompt(const char *prompt, int flags)
{
	char *r = NULL;

	if (flags & PROMPT_ASKPASS) {
		const char *askpass;

		askpass = getenv("GIT_ASKPASS");
		if (!askpass)
			askpass = askpass_program;
		if (!askpass)
			askpass = getenv("SSH_ASKPASS");
		if (askpass && *askpass)
			r = do_askpass(askpass, prompt);
	}

	if (!r)
		r = git_terminal_prompt(prompt, flags & PROMPT_ECHO);
	if (!r) {
		/* prompts already contain ": " at the end */
		die("could not read %s%s", prompt, strerror(errno));
	}
	return r;
}

char *git_getpass(const char *prompt)
{
	return git_prompt(prompt, PROMPT_ASKPASS);
}
