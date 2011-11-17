/*
 * Copyright (c) 2006 Franck Bui-Huu
 * Copyright (c) 2006 Rene Scharfe
 */
#include "cache.h"
#include "builtin.h"
#include "archive.h"
#include "pkt-line.h"
#include "sideband.h"

static int run_remote_archiver(const char *remote, int argc,
			       const char **argv)
{
	char *url, buf[LARGE_PACKET_MAX];
	int fd[2], i, len, rv;
	struct child_process *conn;
	const char *exec = "git-upload-archive";
	int exec_at = 0, exec_value_at = 0;

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!prefixcmp(arg, "--exec=")) {
			if (exec_at)
				die("multiple --exec specified");
			exec = arg + 7;
			exec_at = i;
		} else if (!strcmp(arg, "--exec")) {
			if (exec_at)
				die("multiple --exec specified");
			if (i + 1 >= argc)
				die("option --exec requires a value");
			exec = argv[i + 1];
			exec_at = i;
			exec_value_at = ++i;
		}
	}

	url = xstrdup(remote);
	conn = git_connect(fd, url, exec, 0);

	for (i = 1; i < argc; i++) {
		if (i == exec_at || i == exec_value_at)
			continue;
		packet_write(fd[1], "argument %s\n", argv[i]);
	}
	packet_flush(fd[1]);

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (!len)
		die("git archive: expected ACK/NAK, got EOF");
	if (buf[len-1] == '\n')
		buf[--len] = 0;
	if (strcmp(buf, "ACK")) {
		if (len > 5 && !prefixcmp(buf, "NACK "))
			die("git archive: NACK %s", buf + 5);
		die("git archive: protocol error");
	}

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (len)
		die("git archive: expected a flush");

	/* Now, start reading from fd[0] and spit it out to stdout */
	rv = recv_sideband("archive", fd[0], 1, 2);
	close(fd[0]);
	close(fd[1]);
	rv |= finish_connect(conn);

	return !!rv;
}

static const char *extract_remote_arg(int *ac, const char **av)
{
	int ix, iy, cnt = *ac;
	int no_more_options = 0;
	const char *remote = NULL;

	for (ix = iy = 1; ix < cnt; ix++) {
		const char *arg = av[ix];
		if (!strcmp(arg, "--"))
			no_more_options = 1;
		if (!no_more_options) {
			if (!prefixcmp(arg, "--remote=")) {
				if (remote)
					die("Multiple --remote specified");
				remote = arg + 9;
				continue;
			} else if (!strcmp(arg, "--remote")) {
				if (remote)
					die("Multiple --remote specified");
				if (++ix >= cnt)
					die("option --remote requires a value");
				remote = av[ix];
				continue;
			}
			if (arg[0] != '-')
				no_more_options = 1;
		}
		if (ix != iy)
			av[iy] = arg;
		iy++;
	}
	if (remote) {
		av[--cnt] = NULL;
		*ac = cnt;
	}
	return remote;
}

int cmd_archive(int argc, const char **argv, const char *prefix)
{
	const char *remote = NULL;

	remote = extract_remote_arg(&argc, argv);
	if (remote)
		return run_remote_archiver(remote, argc, argv);

	setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

	return write_archive(argc, argv, prefix, 1, 0);
}
