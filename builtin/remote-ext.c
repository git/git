#include "builtin.h"
#include "transport.h"
#include "run-command.h"

/*
 * URL syntax:
 *	'command [arg1 [arg2 [...]]]'	Invoke command with given arguments.
 *	Special characters:
 *	'% ': Literal space in argument.
 *	'%%': Literal percent sign.
 *	'%S': Name of service (git-upload-pack/git-upload-archive/
 *		git-receive-pack.
 *	'%s': Same as \s, but with possible git- prefix stripped.
 *	'%G': Only allowed as first 'character' of argument. Do not pass this
 *		Argument to command, instead send this as name of repository
 *		in in-line git://-style request (also activates sending this
 *		style of request).
 *	'%V': Only allowed as first 'character' of argument. Used in
 *		conjunction with '%G': Do not pass this argument to command,
 *		instead send this as vhost in git://-style request (note: does
 *		not activate sending git:// style request).
 */

static char *git_req;
static char *git_req_vhost;

static char *strip_escapes(const char *str, const char *service,
	const char **next)
{
	size_t rpos = 0;
	int escape = 0;
	char special = 0;
	const char *service_noprefix = service;
	struct strbuf ret = STRBUF_INIT;

	skip_prefix(service_noprefix, "git-", &service_noprefix);

	/* Pass the service to command. */
	setenv("GIT_EXT_SERVICE", service, 1);
	setenv("GIT_EXT_SERVICE_NOPREFIX", service_noprefix, 1);

	/* Scan the length of argument. */
	while (str[rpos] && (escape || str[rpos] != ' ')) {
		if (escape) {
			switch (str[rpos]) {
			case ' ':
			case '%':
			case 's':
			case 'S':
				break;
			case 'G':
			case 'V':
				special = str[rpos];
				if (rpos == 1)
					break;
				/* Fall-through to error. */
			default:
				die("Bad remote-ext placeholder '%%%c'.",
					str[rpos]);
			}
			escape = 0;
		} else
			escape = (str[rpos] == '%');
		rpos++;
	}
	if (escape && !str[rpos])
		die("remote-ext command has incomplete placeholder");
	*next = str + rpos;
	if (**next == ' ')
		++*next;	/* Skip over space */

	/*
	 * Do the actual placeholder substitution. The string will be short
	 * enough not to overflow integers.
	 */
	rpos = special ? 2 : 0;		/* Skip first 2 bytes in specials. */
	escape = 0;
	while (str[rpos] && (escape || str[rpos] != ' ')) {
		if (escape) {
			switch (str[rpos]) {
			case ' ':
			case '%':
				strbuf_addch(&ret, str[rpos]);
				break;
			case 's':
				strbuf_addstr(&ret, service_noprefix);
				break;
			case 'S':
				strbuf_addstr(&ret, service);
				break;
			}
			escape = 0;
		} else
			switch (str[rpos]) {
			case '%':
				escape = 1;
				break;
			default:
				strbuf_addch(&ret, str[rpos]);
				break;
			}
		rpos++;
	}
	switch (special) {
	case 'G':
		git_req = strbuf_detach(&ret, NULL);
		return NULL;
	case 'V':
		git_req_vhost = strbuf_detach(&ret, NULL);
		return NULL;
	default:
		return strbuf_detach(&ret, NULL);
	}
}

/* Should be enough... */
#define MAXARGUMENTS 256

static const char **parse_argv(const char *arg, const char *service)
{
	int arguments = 0;
	int i;
	const char **ret;
	char *temparray[MAXARGUMENTS + 1];

	while (*arg) {
		char *expanded;
		if (arguments == MAXARGUMENTS)
			die("remote-ext command has too many arguments");
		expanded = strip_escapes(arg, service, &arg);
		if (expanded)
			temparray[arguments++] = expanded;
	}

	ret = xmalloc((arguments + 1) * sizeof(char *));
	for (i = 0; i < arguments; i++)
		ret[i] = temparray[i];
	ret[arguments] = NULL;
	return ret;
}

static void send_git_request(int stdin_fd, const char *serv, const char *repo,
	const char *vhost)
{
	size_t bufferspace;
	size_t wpos = 0;
	char *buffer;

	/*
	 * Request needs 12 bytes extra if there is vhost (xxxx \0host=\0) and
	 * 6 bytes extra (xxxx \0) if there is no vhost.
	 */
	if (vhost)
		bufferspace = strlen(serv) + strlen(repo) + strlen(vhost) + 12;
	else
		bufferspace = strlen(serv) + strlen(repo) + 6;

	if (bufferspace > 0xFFFF)
		die("Request too large to send");
	buffer = xmalloc(bufferspace);

	/* Make the packet. */
	wpos = sprintf(buffer, "%04x%s %s%c", (unsigned)bufferspace,
		serv, repo, 0);

	/* Add vhost if any. */
	if (vhost)
		sprintf(buffer + wpos, "host=%s%c", vhost, 0);

	/* Send the request */
	if (write_in_full(stdin_fd, buffer, bufferspace) < 0)
		die_errno("Failed to send request");

	free(buffer);
}

static int run_child(const char *arg, const char *service)
{
	int r;
	struct child_process child = CHILD_PROCESS_INIT;

	child.in = -1;
	child.out = -1;
	child.err = 0;
	child.argv = parse_argv(arg, service);

	if (start_command(&child) < 0)
		die("Can't run specified command");

	if (git_req)
		send_git_request(child.in, service, git_req, git_req_vhost);

	r = bidirectional_transfer_loop(child.out, child.in);
	if (!r)
		r = finish_command(&child);
	else
		finish_command(&child);
	return r;
}

#define MAXCOMMAND 4096

static int command_loop(const char *child)
{
	char buffer[MAXCOMMAND];

	while (1) {
		size_t i;
		if (!fgets(buffer, MAXCOMMAND - 1, stdin)) {
			if (ferror(stdin))
				die("Comammand input error");
			exit(0);
		}
		/* Strip end of line characters. */
		i = strlen(buffer);
		while (i > 0 && isspace(buffer[i - 1]))
			buffer[--i] = 0;

		if (!strcmp(buffer, "capabilities")) {
			printf("*connect\n\n");
			fflush(stdout);
		} else if (!strncmp(buffer, "connect ", 8)) {
			printf("\n");
			fflush(stdout);
			return run_child(child, buffer + 8);
		} else {
			fprintf(stderr, "Bad command");
			return 1;
		}
	}
}

int cmd_remote_ext(int argc, const char **argv, const char *prefix)
{
	if (argc != 3)
		die("Expected two arguments");

	return command_loop(argv[2]);
}
