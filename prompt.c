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

	args[0] = cmd;
	args[1]	= prompt;
	args[2] = NULL;

	memset(&pass, 0, sizeof(pass));
	pass.argv = args;
	pass.out = -1;

	if (start_command(&pass))
		exit(1);

	if (strbuf_read(&buffer, pass.out, 20) < 0)
		die("failed to get '%s' from %s\n", prompt, cmd);

	close(pass.out);

	if (finish_command(&pass))
		exit(1);

	strbuf_setlen(&buffer, strcspn(buffer.buf, "\r\n"));

	return strbuf_detach(&buffer, NULL);
}

char *git_prompt(const char *prompt, int flags)
{
	char *r;

	if (flags & PROMPT_ASKPASS) {
		const char *askpass;

		askpass = getenv("GIT_ASKPASS");
		if (!askpass)
			askpass = askpass_program;
		if (!askpass)
			askpass = getenv("SSH_ASKPASS");
		if (askpass && *askpass)
			return do_askpass(askpass, prompt);
	}

	r = git_terminal_prompt(prompt, flags & PROMPT_ECHO);
	if (!r)
		die_errno("could not read '%s'", prompt);
	return r;
}

char *git_getpass(const char *prompt)
{
	return git_prompt(prompt, PROMPT_ASKPASS);
}
