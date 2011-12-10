#include "cache.h"
#include "run-command.h"
#include "strbuf.h"
#include "prompt.h"

char *git_getpass(const char *prompt)
{
	const char *askpass;
	struct child_process pass;
	const char *args[3];
	static struct strbuf buffer = STRBUF_INIT;

	askpass = getenv("GIT_ASKPASS");
	if (!askpass)
		askpass = askpass_program;
	if (!askpass)
		askpass = getenv("SSH_ASKPASS");
	if (!askpass || !(*askpass)) {
		char *result = getpass(prompt);
		if (!result)
			die_errno("Could not read password");
		return result;
	}

	args[0] = askpass;
	args[1]	= prompt;
	args[2] = NULL;

	memset(&pass, 0, sizeof(pass));
	pass.argv = args;
	pass.out = -1;

	if (start_command(&pass))
		exit(1);

	strbuf_reset(&buffer);
	if (strbuf_read(&buffer, pass.out, 20) < 0)
		die("failed to read password from %s\n", askpass);

	close(pass.out);

	if (finish_command(&pass))
		exit(1);

	strbuf_setlen(&buffer, strcspn(buffer.buf, "\r\n"));

	return buffer.buf;
}
