#include "cache.h"
#include "refs.h"
#include "pkt-line.h"

static const char upload_pack_usage[] = "git-upload-pack <dir>";

#define MAX_HAS (16)
#define MAX_NEEDS (256)
static int nr_has = 0, nr_needs = 0;
static unsigned char has_sha1[MAX_HAS][20];
static unsigned char needs_sha1[MAX_NEEDS][20];

static int strip(char *line, int len)
{
	if (len && line[len-1] == '\n')
		line[--len] = 0;
	return len;
}

static void create_pack_file(void)
{
	int fd[2];
	pid_t pid;

	if (pipe(fd) < 0)
		die("git-upload-pack: unable to create pipe");
	pid = fork();
	if (pid < 0)
		die("git-upload-pack: unable to fork git-rev-list");

	if (!pid) {
		int i;
		int args = nr_has + nr_needs + 5;
		char **argv = xmalloc(args * sizeof(char *));
		char *buf = xmalloc(args * 45);
		char **p = argv;

		dup2(fd[1], 1);
		close(0);
		close(fd[0]);
		close(fd[1]);
		*p++ = "git-rev-list";
		*p++ = "--objects";
		for (i = 0; i < nr_needs; i++) {
			*p++ = buf;
			memcpy(buf, sha1_to_hex(needs_sha1[i]), 41);
			buf += 41;
		}
		for (i = 0; i < nr_has; i++) {
			*p++ = buf;
			*buf++ = '^';
			memcpy(buf, sha1_to_hex(has_sha1[i]), 41);
			buf += 41;
		}
		*p++ = NULL;
		execvp("git-rev-list", argv);
		die("git-upload-pack: unable to exec git-rev-list");
	}
	dup2(fd[0], 0);
	close(fd[0]);
	close(fd[1]);
	execlp("git-pack-objects", "git-pack-objects", "--stdout", NULL);
	die("git-upload-pack: unable to exec git-pack-objects");
}

static int got_sha1(char *hex, unsigned char *sha1)
{
	int nr;
	if (get_sha1_hex(hex, sha1))
		die("git-upload-pack: expected SHA1 object, got '%s'", hex);
	if (!has_sha1_file(sha1))
		return 0;
	nr = nr_has;
	if (nr < MAX_HAS) {
		memcpy(has_sha1[nr], sha1, 20);
		nr_has = nr+1;
	}
	return 1;
}

static int get_common_commits(void)
{
	static char line[1000];
	unsigned char sha1[20];
	int len;

	for(;;) {
		len = packet_read_line(0, line, sizeof(line));

		if (!len) {
			packet_write(1, "NAK\n");
			continue;
		}
		len = strip(line, len);
		if (!strncmp(line, "have ", 5)) {
			if (got_sha1(line+5, sha1)) {
				packet_write(1, "ACK %s\n", sha1_to_hex(sha1));
				break;
			}
			continue;
		}
		if (!strcmp(line, "done")) {
			packet_write(1, "NAK\n");
			return -1;
		}
		die("git-upload-pack: expected SHA1 list, got '%s'", line);
	}

	for (;;) {
		len = packet_read_line(0, line, sizeof(line));
		if (!len)
			continue;
		len = strip(line, len);
		if (!strncmp(line, "have ", 5)) {
			got_sha1(line+5, sha1);
			continue;
		}
		if (!strcmp(line, "done"))
			break;
		die("git-upload-pack: expected SHA1 list, got '%s'", line);
	}
	return 0;
}

static int receive_needs(void)
{
	static char line[1000];
	int len, needs;

	needs = 0;
	for (;;) {
		len = packet_read_line(0, line, sizeof(line));
		if (!len)
			return needs;

		/*
		 * This is purely theoretical right now: git-fetch-pack only
		 * ever asks for a single HEAD
		 */
		if (needs >= MAX_NEEDS)
			die("I'm only doing a max of %d requests", MAX_NEEDS);
		if (strncmp("want ", line, 5) || get_sha1_hex(line+5, needs_sha1[needs]))
			die("git-upload-pack: protocol error, expected to get sha, not '%s'", line);
		needs++;
	}
}

static int send_ref(const char *refname, const unsigned char *sha1)
{
	packet_write(1, "%s %s\n", sha1_to_hex(sha1), refname);
	return 0;
}

static int upload_pack(void)
{
	head_ref(send_ref);
	for_each_ref(send_ref);
	packet_flush(1);
	nr_needs = receive_needs();
	if (!nr_needs)
		return 0;
	get_common_commits();
	create_pack_file();
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir;
	if (argc != 2)
		usage(upload_pack_usage);
	dir = argv[1];

	/* chdir to the directory. If that fails, try appending ".git" */
	if (chdir(dir) < 0) {
		if (chdir(mkpath("%s.git", dir)) < 0)
			die("git-upload-pack unable to chdir to %s", dir);
	}
	chdir(".git");
	if (access("objects", X_OK) || access("refs", X_OK))
		die("git-upload-pack: %s doesn't seem to be a git archive", dir);
	setenv("GIT_DIR", ".", 1);
	upload_pack();
	return 0;
}
