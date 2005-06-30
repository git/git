#include "cache.h"
#include "pkt-line.h"

static const char send_pack_usage[] = "git-send-pack [--exec=other] destination [heads]*";

static const char *exec = "git-receive-pack";

static int send_pack(int in, int out)
{
	for (;;) {
		static char buffer[1000];
		int len;

		len = packet_read_line(in, buffer, sizeof(buffer));
		if (len > 0) {
			write(2, buffer, len);
			continue;
		}
		break;
	}
	packet_flush(out);
	close(out);
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
		[':'] = 1
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
static int setup_connection(int fd[2], char *url, char **heads)
{
	char command[1024];
	const char *host, *path;
	char *colon;
	int pipefd[2][2];

	url = shell_safe(url);
	host = NULL;
	path = url;
	colon = strchr(url, ':');
	if (colon) {
		*colon = 0;
		host = url;
		path = colon+1;
	}
	snprintf(command, sizeof(command), "%s %s", exec, path);
	if (pipe(pipefd[0]) < 0 || pipe(pipefd[1]) < 0)
		die("unable to create pipe pair for communication");
	if (!fork()) {
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
	return 0;
}

int main(int argc, char **argv)
{
	int i, nr_heads = 0;
	char *dest = NULL;
	char **heads = NULL;
	int fd[2];

	argv++;
	for (i = 1; i < argc; i++) {
		char *arg = *argv++;

		if (*arg == '-') {
			if (!strncmp(arg, "--exec=", 7)) {
				exec = arg + 7;
				continue;
			}
			usage(send_pack_usage);
		}
		dest = arg;
		heads = argv;
		nr_heads = argc - i -1;
		break;
	}
	if (!dest)
		usage(send_pack_usage);
	if (setup_connection(fd, dest, heads))
		return 1;
	return send_pack(fd[0], fd[1]);
}
