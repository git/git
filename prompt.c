#include "cache.h"
#include "config.h"
#include "run-command.h"
#include "strbuf.h"
#include "prompt.h"
#include "compat/terminal.h"

static char *do_askpass(const char *cmd, const char *prompt)
{
	struct child_process pass = CHILD_PROCESS_INIT;
	const char *args[3];
	static struct strbuf buffer = STRBUF_INIT;
	int err = 0;

	args[0] = cmd;
	args[1]	= prompt;
	args[2] = NULL;

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

	if (!r) {
		const char *err;

		if (git_env_bool("GIT_TERMINAL_PROMPT", 1)) {
			r = git_terminal_prompt(prompt, flags & PROMPT_ECHO);
			err = strerror(errno);
		} else {
			err = "terminal prompts disabled";
		}
		if (!r) {
			/* prompts already contain ": " at the end */
			die("could not read %s%s", prompt, err);
		}
	}
	return r;
}
