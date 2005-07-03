#include "cache.h"
#include "commit.h"
#include "rsh.h"
#include "pull.h"
#include "refs.h"

static int fd_in;
static int fd_out;

static unsigned char remote_version = 0;
static unsigned char local_version = 1;

int fetch(unsigned char *sha1)
{
	int ret;
	signed char remote;
	char type = 'o';
	if (has_sha1_file(sha1))
		return 0;
	write(fd_out, &type, 1);
	write(fd_out, sha1, 20);
	if (read(fd_in, &remote, 1) < 1)
		return -1;
	if (remote < 0)
		return remote;
	ret = write_sha1_from_fd(sha1, fd_in);
	if (!ret)
		pull_say("got %s\n", sha1_to_hex(sha1));
	return ret;
}

static int get_version(void)
{
	char type = 'v';
	write(fd_out, &type, 1);
	write(fd_out, &local_version, 1);
	if (read(fd_in, &remote_version, 1) < 1) {
		return error("Couldn't read version from remote end");
	}
	return 0;
}

int fetch_ref(char *ref, unsigned char *sha1)
{
	signed char remote;
	char type = 'r';
	write(fd_out, &type, 1);
	write(fd_out, ref, strlen(ref) + 1);
	read(fd_in, &remote, 1);
	if (remote < 0)
		return remote;
	read(fd_in, sha1, 20);
	return 0;
}

int main(int argc, char **argv)
{
	char *commit_id;
	char *url;
	int arg = 1;
	const char *prog = getenv("GIT_SSH_PUSH") ? : "git-ssh-push";

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't') {
			get_tree = 1;
		} else if (argv[arg][1] == 'c') {
			get_history = 1;
		} else if (argv[arg][1] == 'a') {
			get_all = 1;
			get_tree = 1;
			get_history = 1;
		} else if (argv[arg][1] == 'v') {
			get_verbosely = 1;
		} else if (argv[arg][1] == 'w') {
			write_ref = argv[arg + 1];
			arg++;
		}
		arg++;
	}
	if (argc < arg + 2) {
		usage("git-ssh-pull [-c] [-t] [-a] [-v] [-d] [--recover] [-w ref] commit-id url");
		return 1;
	}
	commit_id = argv[arg];
	url = argv[arg + 1];

	if (setup_connection(&fd_in, &fd_out, prog, url, arg, argv + 1))
		return 1;

	if (get_version())
		return 1;

	if (pull(commit_id))
		return 1;

	return 0;
}
