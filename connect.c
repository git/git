#include "cache.h"
#include "pkt-line.h"
#include <sys/wait.h>

int get_ack(int fd, unsigned char *result_sha1)
{
	static char line[1000];
	int len = packet_read_line(fd, line, sizeof(line));

	if (!len)
		die("git-fetch-pack: expected ACK/NAK, got EOF");
	if (line[len-1] == '\n')
		line[--len] = 0;
	if (!strcmp(line, "NAK"))
		return 0;
	if (!strncmp(line, "ACK ", 3)) {
		if (!get_sha1_hex(line+4, result_sha1))
			return 1;
	}
	die("git-fetch_pack: expected ACK/NAK, got '%s'", line);
}

int path_match(const char *path, int nr, char **match)
{
	int i;
	int pathlen = strlen(path);

	for (i = 0; i < nr; i++) {
		char *s = match[i];
		int len = strlen(s);

		if (!len || len > pathlen)
			continue;
		if (memcmp(path + pathlen - len, s, len))
			continue;
		if (pathlen > len && path[pathlen - len - 1] != '/')
			continue;
		*s = 0;
		return 1;
	}
	return 0;
}

/*
 * First, make it shell-safe.  We do this by just disallowing any
 * special characters. Somebody who cares can do escaping and let
 * through the rest. But since we're doing to feed this to ssh as
 * a command line, we're going to be pretty damn anal for now.
 */
static char *shell_safe(char *url)
{
	char *n = url;
	unsigned char c;
	static const char flags[256] = {
		['0'...'9'] = 1,
		['a'...'z'] = 1,
		['A'...'Z'] = 1,
		['.'] = 1, ['/'] = 1,
		['-'] = 1, ['+'] = 1,
		[':'] = 1, ['_'] = 1,
		['@'] = 1, [','] = 1,
		['~'] = 1, ['^'] = 1,
	};

	while ((c = *n++) != 0) {
		if (flags[c] != 1)
			die("I don't like '%c'. Sue me.", c);
	}
	return url;
}

/*
 * Yeah, yeah, fixme. Need to pass in the heads etc.
 */
int git_connect(int fd[2], char *url, const char *prog)
{
	char command[1024];
	const char *host, *path;
	char *colon;
	int pipefd[2][2];
	pid_t pid;

	url = shell_safe(url);
	host = NULL;
	path = url;
	colon = strchr(url, ':');
	if (colon) {
		*colon = 0;
		host = url;
		path = colon+1;
	}
	snprintf(command, sizeof(command), "%s %s", prog, path);
	if (pipe(pipefd[0]) < 0 || pipe(pipefd[1]) < 0)
		die("unable to create pipe pair for communication");
	pid = fork();
	if (!pid) {
		dup2(pipefd[1][0], 0);
		dup2(pipefd[0][1], 1);
		close(pipefd[0][0]);
		close(pipefd[0][1]);
		close(pipefd[1][0]);
		close(pipefd[1][1]);
		if (host)
			execlp("ssh", "ssh", host, command, NULL);
		else
			execlp("sh", "sh", "-c", command, NULL);
		die("exec failed");
	}		
	fd[0] = pipefd[0][0];
	fd[1] = pipefd[1][1];
	close(pipefd[0][1]);
	close(pipefd[1][0]);
	return pid;
}

int finish_connect(pid_t pid)
{
	int ret;

	for (;;) {
		ret = waitpid(pid, NULL, 0);
		if (!ret)
			break;
		if (errno != EINTR)
			break;
	}
	return ret;
}
