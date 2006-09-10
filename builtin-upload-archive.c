/*
 * Copyright (c) 2006 Franck Bui-Huu
 */
#include <time.h>
#include "cache.h"
#include "builtin.h"
#include "archive.h"
#include "pkt-line.h"
#include "sideband.h"
#include <sys/wait.h>
#include <sys/poll.h>

static const char upload_archive_usage[] =
	"git-upload-archive <repo>";

static const char deadchild[] =
"git-upload-archive: archiver died with error";


static int run_upload_archive(int argc, const char **argv, const char *prefix)
{
	struct archiver ar;
	const char *sent_argv[MAX_ARGS];
	const char *arg_cmd = "argument ";
	char *p, buf[4096];
	int treeish_idx;
	int sent_argc;
	int len;

	if (argc != 2)
		usage(upload_archive_usage);

	if (strlen(argv[1]) > sizeof(buf))
		die("insanely long repository name");

	strcpy(buf, argv[1]); /* enter-repo smudges its argument */

	if (!enter_repo(buf, 0))
		die("not a git archive");

	/* put received options in sent_argv[] */
	sent_argc = 1;
	sent_argv[0] = "git-upload-archive";
	for (p = buf;;) {
		/* This will die if not enough free space in buf */
		len = packet_read_line(0, p, (buf + sizeof buf) - p);
		if (len == 0)
			break;	/* got a flush */
		if (sent_argc > MAX_ARGS - 2)
			die("Too many options (>29)");

		if (p[len-1] == '\n') {
			p[--len] = 0;
		}
		if (len < strlen(arg_cmd) ||
		    strncmp(arg_cmd, p, strlen(arg_cmd)))
			die("'argument' token or flush expected");

		len -= strlen(arg_cmd);
		memmove(p, p + strlen(arg_cmd), len);
		sent_argv[sent_argc++] = p;
		p += len;
		*p++ = 0;
	}
	sent_argv[sent_argc] = NULL;

	/* parse all options sent by the client */
	treeish_idx = parse_archive_args(sent_argc, sent_argv, &ar);

	parse_treeish_arg(sent_argv + treeish_idx, &ar.args, prefix);
	parse_pathspec_arg(sent_argv + treeish_idx + 1, &ar.args);

	return ar.write_archive(&ar.args);
}

int cmd_upload_archive(int argc, const char **argv, const char *prefix)
{
	pid_t writer;
	int fd1[2], fd2[2];
	/*
	 * Set up sideband subprocess.
	 *
	 * We (parent) monitor and read from child, sending its fd#1 and fd#2
	 * multiplexed out to our fd#1.  If the child dies, we tell the other
	 * end over channel #3.
	 */
	if (pipe(fd1) < 0 || pipe(fd2) < 0) {
		int err = errno;
		packet_write(1, "NACK pipe failed on the remote side\n");
		die("upload-archive: %s", strerror(err));
	}
	writer = fork();
	if (writer < 0) {
		int err = errno;
		packet_write(1, "NACK fork failed on the remote side\n");
		die("upload-archive: %s", strerror(err));
	}
	if (!writer) {
		/* child - connect fd#1 and fd#2 to the pipe */
		dup2(fd1[1], 1);
		dup2(fd2[1], 2);
		close(fd1[1]); close(fd2[1]);
		close(fd1[0]); close(fd2[0]); /* we do not read from pipe */

		exit(run_upload_archive(argc, argv, prefix));
	}

	/* parent - read from child, multiplex and send out to fd#1 */
	close(fd1[1]); close(fd2[1]); /* we do not write to pipe */
	packet_write(1, "ACK\n");
	packet_flush(1);

	while (1) {
		struct pollfd pfd[2];
		char buf[16384];
		ssize_t sz;
		pid_t pid;
		int status;

		pfd[0].fd = fd1[0];
		pfd[0].events = POLLIN;
		pfd[1].fd = fd2[0];
		pfd[1].events = POLLIN;
		if (poll(pfd, 2, -1) < 0) {
			if (errno != EINTR) {
				error("poll failed resuming: %s",
				      strerror(errno));
				sleep(1);
			}
			continue;
		}
		if (pfd[0].revents & (POLLIN|POLLHUP)) {
			/* Data stream ready */
			sz = read(pfd[0].fd, buf, sizeof(buf));
			send_sideband(1, 1, buf, sz, LARGE_PACKET_MAX);
		}
		if (pfd[1].revents & (POLLIN|POLLHUP)) {
			/* Status stream ready */
			sz = read(pfd[1].fd, buf, sizeof(buf));
			send_sideband(1, 2, buf, sz, LARGE_PACKET_MAX);
		}

		if (((pfd[0].revents | pfd[1].revents) & POLLHUP) == 0)
			continue;
		/* did it die? */
		pid = waitpid(writer, &status, WNOHANG);
		if (!pid) {
			fprintf(stderr, "Hmph, HUP?\n");
			continue;
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) > 0)
			send_sideband(1, 3, deadchild, strlen(deadchild),
				      LARGE_PACKET_MAX);
		packet_flush(1);
		break;
	}
	return 0;
}
