#include "cache.h"
#include "pkt-line.h"
#include <sys/socket.h>
#include <netinet/in.h>

static const char daemon_usage[] = "git-daemon [--port=n]";

static int upload(char *dir, int dirlen)
{
	if (chdir(dir) < 0)
		return -1;
	chdir(".git");

	/*
	 * Security on the cheap.
	 *
	 * We want a readable HEAD, usable "objects" directory, and 
	 * a "git-daemon-export-ok" flag that says that the other side
	 * is ok with us doing this.
	 */
	if (access("git-daemon-export-ok", F_OK) ||
	    access("objects/00", X_OK) ||
	    access("HEAD", R_OK))
		return -1;

	/* git-upload-pack only ever reads stuff, so this is safe */
	execlp("git-upload-pack", "git-upload-pack", ".", NULL);
	return -1;
}

static int execute(void)
{
	static char line[1000];
	int len;

	len = packet_read_line(0, line, sizeof(line));

	if (len && line[len-1] == '\n')
		line[--len] = 0;

	if (!strncmp("git-upload-pack /", line, 17))
		return upload(line + 16, len - 16);

	fprintf(stderr, "got bad connection '%s'\n", line);
	return -1;
}

static void handle(int incoming, struct sockaddr_in *addr, int addrlen)
{
	if (fork()) {
		close(incoming);
		return;
	}

	dup2(incoming, 0);
	dup2(incoming, 1);
	close(incoming);
	exit(execute());
}

static int serve(int port)
{
	int sockfd;
	struct sockaddr_in addr;

	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	if (sockfd < 0)
		die("unable to open socket (%s)", strerror(errno));
	memset(&addr, 0, sizeof(addr));
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;
	if (bind(sockfd, (void *)&addr, sizeof(addr)) < 0)
		die("unable to bind to port %d (%s)", port, strerror(errno));
	if (listen(sockfd, 5) < 0)
		die("unable to listen to port %d (%s)", port, strerror(errno));

	for (;;) {
		struct sockaddr_in in;
		socklen_t addrlen = sizeof(in);
		int incoming = accept(sockfd, (void *)&in, &addrlen);

		if (incoming < 0) {
			switch (errno) {
			case EAGAIN:
			case EINTR:
			case ECONNABORTED:
				continue;
			default:
				die("accept returned %s", strerror(errno));
			}
		}
		handle(incoming, &in, addrlen);
	}
}

int main(int argc, char **argv)
{
	int port = DEFAULT_GIT_PORT;
	int i;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (!strncmp(arg, "--port=", 7)) {
			char *end;
			unsigned long n;
			n = strtoul(arg+7, &end, 0);
			if (arg[7] && !*end) {
				port = n;
				continue;
			}
		}
		usage(daemon_usage);
	}

	return serve(port);
}
