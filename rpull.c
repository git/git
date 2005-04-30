#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "cache.h"
#include "commit.h"
#include <errno.h>
#include <stdio.h>
#include "rsh.h"
#include "pull.h"

static int fd_in;
static int fd_out;

int fetch(unsigned char *sha1)
{
	if (has_sha1_file(sha1))
		return 0;
	write(fd_out, sha1, 20);
	return write_sha1_from_fd(sha1, fd_in);
}

int main(int argc, char **argv)
{
	char *commit_id;
	char *url;
	int arg = 1;

	while (arg < argc && argv[arg][0] == '-') {
		if (argv[arg][1] == 't') {
			get_tree = 1;
		} else if (argv[arg][1] == 'c') {
			get_history = 1;
		} else if (argv[arg][1] == 'a') {
			get_all = 1;
			get_tree = 1;
			get_history = 1;
		}
		arg++;
	}
	if (argc < arg + 2) {
		usage("rpull [-c] [-t] [-a] commit-id url");
		return 1;
	}
	commit_id = argv[arg];
	url = argv[arg + 1];

	if (setup_connection(&fd_in, &fd_out, "rpush", url, arg, argv + 1))
		return 1;

	if (pull(commit_id))
		return 1;

	return 0;
}
