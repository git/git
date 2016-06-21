#include "rsh.h"

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "cache.h"

#define COMMAND_SIZE 4096

int setup_connection(int *fd_in, int *fd_out, const char *remote_prog, 
		     char *url, int rmt_argc, char **rmt_argv)
{
	char *host;
	char *path;
	int sv[2];
	char command[COMMAND_SIZE];
	char *posn;
	int i;

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
	/* ssh <host> 'cd <path>; stdio-pull <arg...> <commit-id>' */
	snprintf(command, COMMAND_SIZE, 
		 "%s='%s' %s",
		 GIT_DIR_ENVIRONMENT, path, remote_prog);
	*path = '\0';
	posn = command + strlen(command);
	for (i = 0; i < rmt_argc; i++) {
		*(posn++) = ' ';
		strncpy(posn, rmt_argv[i], COMMAND_SIZE - (posn - command));
		posn += strlen(rmt_argv[i]);
		if (posn - command + 4 >= COMMAND_SIZE) {
			return error("Command line too long");
		}
	}
	strcpy(posn, " -");
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) {
		return error("Couldn't create socket");
	}
	if (!fork()) {
		close(sv[1]);
		dup2(sv[0], 0);
		dup2(sv[0], 1);
		execlp("ssh", "ssh", host, command, NULL);
	}
	close(sv[0]);
	*fd_in = sv[1];
	*fd_out = sv[1];
	return 0;
}
