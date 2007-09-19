#include "cache.h"
#include "rsh.h"
#include "quote.h"

#define COMMAND_SIZE 4096

int setup_connection(int *fd_in, int *fd_out, const char *remote_prog,
		     char *url, int rmt_argc, char **rmt_argv)
{
	char *host;
	char *path;
	int sv[2];
	int i;
	pid_t pid;
	struct strbuf cmd;

	if (!strcmp(url, "-")) {
		*fd_in = 0;
		*fd_out = 1;
		return 0;
	}

	host = strstr(url, "//");
	if (host) {
		host += 2;
		path = strchr(host, '/');
	} else {
		host = url;
		path = strchr(host, ':');
		if (path)
			*(path++) = '\0';
	}
	if (!path) {
		return error("Bad URL: %s", url);
	}

	/* $GIT_RSH <host> "env GIT_DIR=<path> <remote_prog> <args...>" */
	strbuf_init(&cmd, COMMAND_SIZE);
	strbuf_addstr(&cmd, "env ");
	strbuf_addstr(&cmd, GIT_DIR_ENVIRONMENT "=");
	sq_quote_buf(&cmd, path);
	strbuf_addch(&cmd, ' ');
	sq_quote_buf(&cmd, remote_prog);

	for (i = 0 ; i < rmt_argc ; i++) {
		strbuf_addch(&cmd, ' ');
		sq_quote_buf(&cmd, rmt_argv[i]);
	}

	strbuf_addstr(&cmd, " -");

	if (cmd.len >= COMMAND_SIZE)
		return error("Command line too long");

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
		return error("Couldn't create socket");

	pid = fork();
	if (pid < 0)
		return error("Couldn't fork");
	if (!pid) {
		const char *ssh, *ssh_basename;
		ssh = getenv("GIT_SSH");
		if (!ssh) ssh = "ssh";
		ssh_basename = strrchr(ssh, '/');
		if (!ssh_basename)
			ssh_basename = ssh;
		else
			ssh_basename++;
		close(sv[1]);
		dup2(sv[0], 0);
		dup2(sv[0], 1);
		execlp(ssh, ssh_basename, host, cmd.buf, NULL);
	}
	close(sv[0]);
	*fd_in = sv[1];
	*fd_out = sv[1];
	return 0;
}
