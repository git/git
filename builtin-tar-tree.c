/*
 * Copyright (c) 2005, 2006 Rene Scharfe
 */
#include <time.h>
#include "cache.h"
#include "commit.h"
#include "tar.h"
#include "builtin.h"
#include "pkt-line.h"
#include "archive.h"

#define RECORDSIZE	(512)
#define BLOCKSIZE	(RECORDSIZE * 20)

static const char tar_tree_usage[] =
"git-tar-tree [--remote=<repo>] <tree-ish> [basedir]";

static int generate_tar(int argc, const char **argv, const char *prefix)
{
	struct archiver_args args;
	int result;
	char *base = NULL;

	memset(&args, 0, sizeof(args));
	if (argc != 2 && argc != 3)
		usage(tar_tree_usage);
	if (argc == 3) {
		int baselen = strlen(argv[2]);
		base = xmalloc(baselen + 2);
		memcpy(base, argv[2], baselen);
		base[baselen] = '/';
		base[baselen + 1] = '\0';
	}
	args.base = base;
	parse_treeish_arg(argv + 1, &args, NULL);

	result = write_tar_archive(&args);
	free(base);

	return result;
}

static const char *exec = "git-upload-tar";

static int remote_tar(int argc, const char **argv)
{
	int fd[2], ret, len;
	pid_t pid;
	char buf[1024];
	char *url;

	if (argc < 3 || 4 < argc)
		usage(tar_tree_usage);

	/* --remote=<repo> */
	url = xstrdup(argv[1]+9);
	pid = git_connect(fd, url, exec);
	if (pid < 0)
		return 1;

	packet_write(fd[1], "want %s\n", argv[2]);
	if (argv[3])
		packet_write(fd[1], "base %s\n", argv[3]);
	packet_flush(fd[1]);

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (!len)
		die("git-tar-tree: expected ACK/NAK, got EOF");
	if (buf[len-1] == '\n')
		buf[--len] = 0;
	if (strcmp(buf, "ACK")) {
		if (5 < len && !strncmp(buf, "NACK ", 5))
			die("git-tar-tree: NACK %s", buf + 5);
		die("git-tar-tree: protocol error");
	}
	/* expect a flush */
	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (len)
		die("git-tar-tree: expected a flush");

	/* Now, start reading from fd[0] and spit it out to stdout */
	ret = copy_fd(fd[0], 1);
	close(fd[0]);

	ret |= finish_connect(pid);
	return !!ret;
}

int cmd_tar_tree(int argc, const char **argv, const char *prefix)
{
	if (argc < 2)
		usage(tar_tree_usage);
	if (!strncmp("--remote=", argv[1], 9))
		return remote_tar(argc, argv);
	return generate_tar(argc, argv, prefix);
}

/* ustar header + extended global header content */
#define HEADERSIZE (2 * RECORDSIZE)

int cmd_get_tar_commit_id(int argc, const char **argv, const char *prefix)
{
	char buffer[HEADERSIZE];
	struct ustar_header *header = (struct ustar_header *)buffer;
	char *content = buffer + RECORDSIZE;
	ssize_t n;

	n = xread(0, buffer, HEADERSIZE);
	if (n < HEADERSIZE)
		die("git-get-tar-commit-id: read error");
	if (header->typeflag[0] != 'g')
		return 1;
	if (memcmp(content, "52 comment=", 11))
		return 1;

	n = xwrite(1, content + 11, 41);
	if (n < 41)
		die("git-get-tar-commit-id: write error");

	return 0;
}
