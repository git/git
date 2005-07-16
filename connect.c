#include "cache.h"
#include "pkt-line.h"
#include "quote.h"
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/*
 * Read all the refs from the other end
 */
struct ref **get_remote_heads(int in, struct ref **list, int nr_match, char **match)
{
	*list = NULL;
	for (;;) {
		struct ref *ref;
		unsigned char old_sha1[20];
		static char buffer[1000];
		char *name;
		int len;

		len = packet_read_line(in, buffer, sizeof(buffer));
		if (!len)
			break;
		if (buffer[len-1] == '\n')
			buffer[--len] = 0;

		if (len < 42 || get_sha1_hex(buffer, old_sha1) || buffer[40] != ' ')
			die("protocol error: expected sha/ref, got '%s'", buffer);
		name = buffer + 41;
		if (nr_match && !path_match(name, nr_match, match))
			continue;
		ref = xmalloc(sizeof(*ref) + len - 40);
		memcpy(ref->old_sha1, old_sha1, 20);
		memset(ref->new_sha1, 0, 20);
		memcpy(ref->name, buffer + 41, len - 40);
		ref->next = NULL;
		*list = ref;
		list = &ref->next;
	}
	return list;
}

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

enum protocol {
	PROTO_LOCAL = 1,
	PROTO_SSH,
	PROTO_GIT,
};

static enum protocol get_protocol(const char *name)
{
	if (!strcmp(name, "ssh"))
		return PROTO_SSH;
	if (!strcmp(name, "git"))
		return PROTO_GIT;
	die("I don't handle protocol '%s'", name);
}

static void lookup_host(const char *host, struct sockaddr *in)
{
	struct addrinfo *res;
	int ret;

	ret = getaddrinfo(host, NULL, NULL, &res);
	if (ret)
		die("Unable to look up %s (%s)", host, gai_strerror(ret));
	*in = *res->ai_addr;
	freeaddrinfo(res);
}

static int git_tcp_connect(int fd[2], const char *prog, char *host, char *path)
{
	struct sockaddr addr;
	int port = DEFAULT_GIT_PORT, sockfd;
	char *colon;

	colon = strchr(host, ':');
	if (colon) {
		char *end;
		unsigned long n = strtoul(colon+1, &end, 0);
		if (colon[1] && !*end) {
			*colon = 0;
			port = n;
		}
	}

	lookup_host(host, &addr);
	((struct sockaddr_in *)&addr)->sin_port = htons(port);

	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	if (sockfd < 0)
		die("unable to create socket (%s)", strerror(errno));
	if (connect(sockfd, (void *)&addr, sizeof(addr)) < 0)
		die("unable to connect (%s)", strerror(errno));
	fd[0] = sockfd;
	fd[1] = sockfd;
	packet_write(sockfd, "%s %s\n", prog, path);
	return 0;
}

/*
 * Yeah, yeah, fixme. Need to pass in the heads etc.
 */
int git_connect(int fd[2], char *url, const char *prog)
{
	char command[1024];
	char *host, *path;
	char *colon;
	int pipefd[2][2];
	pid_t pid;
	enum protocol protocol;

	host = NULL;
	path = url;
	colon = strchr(url, ':');
	protocol = PROTO_LOCAL;
	if (colon) {
		*colon = 0;
		host = url;
		path = colon+1;
		protocol = PROTO_SSH;
		if (!memcmp(path, "//", 2)) {
			char *slash = strchr(path + 2, '/');
			if (slash) {
				int nr = slash - path - 2;
				memmove(path, path+2, nr);
				path[nr] = 0;
				protocol = get_protocol(url);
				host = path;
				path = slash;
			}
		}
	}

	if (protocol == PROTO_GIT)
		return git_tcp_connect(fd, prog, host, path);

	if (pipe(pipefd[0]) < 0 || pipe(pipefd[1]) < 0)
		die("unable to create pipe pair for communication");
	pid = fork();
	if (!pid) {
		snprintf(command, sizeof(command), "%s %s", prog,
			 sq_quote(path));
		dup2(pipefd[1][0], 0);
		dup2(pipefd[0][1], 1);
		close(pipefd[0][0]);
		close(pipefd[0][1]);
		close(pipefd[1][0]);
		close(pipefd[1][1]);
		if (protocol == PROTO_SSH)
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
