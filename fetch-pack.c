#include "cache.h"
#include "pkt-line.h"

static const char fetch_pack_usage[] = "git-fetch-pack [host:]directory [heads]* < mycommitlist";
static const char *exec = "git-upload-pack";

static int get_ack(int fd, unsigned char *result_sha1)
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

static int find_common(int fd[2], unsigned char *result_sha1)
{
	static char line[1000];
	int count = 0, flushes = 0;

	while (fgets(line, sizeof(line), stdin) != NULL) {
		unsigned char sha1[20];
		if (get_sha1_hex(line, sha1))
			die("git-fetch-pack: expected object name, got crud");
		packet_write(fd[1], "have %s\n", sha1_to_hex(sha1));
		if (!(31 & ++count)) {
			packet_flush(fd[1]);
			flushes++;

			/*
			 * We keep one window "ahead" of the other side, and
			 * will wait for an ACK only on the next one
			 */
			if (count == 32)
				continue;
			if (get_ack(fd[0], result_sha1))
				return 0;
			flushes--;
		}
	}
	flushes++;
	packet_flush(fd[1]);
	while (flushes) {
		flushes--;
		if (get_ack(fd[0], result_sha1))
			return 0;
	}
	return -1;
}

static int get_remote_heads(int fd, int nr_match, char **match)
{
	for (;;) {
		static char line[1000];
		unsigned char sha1[20];
		char *refname;
		int len;

		len = packet_read_line(fd, line, sizeof(line));
		if (!len)
			break;
		if (line[len-1] == '\n')
			line[--len] = 0;
		if (len < 42 || get_sha1_hex(line, sha1))
			die("git-fetch-pack: protocol error - expected ref descriptor, got '%sä'", line);
		refname = line+41;
		if (nr_match && !path_match(refname, nr_match, match))
			continue;
		printf("%s %s\n", sha1_to_hex(sha1), refname);
	}
	return 0;
}

static int fetch_pack(int fd[2], int nr_match, char **match)
{
	unsigned char sha1[20];

	get_remote_heads(fd[0], nr_match, match);
	if (find_common(fd, sha1) < 0)
		die("git-fetch-pack: no common commits");
	printf("common commit: %s\n", sha1_to_hex(sha1));
	return 0;
}

int main(int argc, char **argv)
{
	int i, ret, nr_heads;
	char *dest = NULL, **heads;
	int fd[2];
	pid_t pid;

	nr_heads = 0;
	heads = NULL;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (*arg == '-') {
			/* Arguments go here */
			usage(fetch_pack_usage);
		}
		dest = arg;
		heads = argv + i + 1;
		nr_heads = argc - i - 1;
		break;
	}
	if (!dest)
		usage(fetch_pack_usage);
	pid = git_connect(fd, dest, exec);
	if (pid < 0)
		return 1;
	ret = fetch_pack(fd, nr_heads, heads);
	close(fd[0]);
	close(fd[1]);
	finish_connect(pid);
	return ret;
}
