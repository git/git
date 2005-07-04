#include "cache.h"
#include "refs.h"
#include "pkt-line.h"

static const char upload_pack_usage[] = "git-upload-pack <dir>";

static int got_sha1(char *hex, unsigned char *sha1)
{
	if (get_sha1_hex(hex, sha1))
		die("git-upload-pack: expected SHA1 object, got '%s'", hex);
	return has_sha1_file(sha1);
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
		if (line[len-1] == '\n')
			line[--len] = 0;
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
			break;
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

static int send_ref(const char *refname, const unsigned char *sha1)
{
	packet_write(1, "%s %s\n", sha1_to_hex(sha1), refname);
	return 0;
}

static int upload_pack(void)
{
	for_each_ref(send_ref);
	packet_flush(1);
	get_common_commits();
	return 0;
}

int main(int argc, char **argv)
{
	const char *dir;
	if (argc != 2)
		usage(upload_pack_usage);
	dir = argv[1];
	if (chdir(dir))
		die("git-upload-pack unable to chdir to %s", dir);
	chdir(".git");
	if (access("objects", X_OK) || access("refs", X_OK))
		die("git-upload-pack: %s doesn't seem to be a git archive", dir);
	setenv("GIT_DIR", ".", 1);
	upload_pack();
	return 0;
}
