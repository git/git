/*
 * Copyright (c) 2006 Franck Bui-Huu
 */
#include "cache.h"
#include "builtin.h"
#include "archive.h"
#include "pkt-line.h"
#include "sideband.h"

static const char upload_archive_usage[] =
	"git upload-archive <repo>";

static const char deadchild[] =
"git upload-archive: archiver died with error";

static const char lostchild[] =
"git upload-archive: archiver process was lost";

#define MAX_ARGS (64)

static int run_upload_archive(int argc, const char **argv, const char *prefix)
{
	const char *sent_argv[MAX_ARGS];
	const char *arg_cmd = "argument ";
	char *p, buf[4096];
	int sent_argc;
	int len;

	if (argc != 2)
		usage(upload_archive_usage);

	if (strlen(argv[1]) + 1 > sizeof(buf))
		die("insanely long repository name");

	strcpy(buf, argv[1]); /* enter-repo smudges its argument */

	if (!enter_repo(buf, 0))
		die("'%s' does not appear to be a git repository", buf);

	/* put received options in sent_argv[] */
	sent_argc = 1;
	sent_argv[0] = "git-upload-archive";
	for (p = buf;;) {
		/* This will die if not enough free space in buf */
		len = packet_read_line(0, p, (buf + sizeof buf) - p);
		if (len == 0)
			break;	/* got a flush */
		if (sent_argc > MAX_ARGS - 2)
			die("Too many options (>%d)", MAX_ARGS - 2);

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
	return write_archive(sent_argc, sent_argv, prefix, 0);
}

__attribute__((format (printf, 1, 2)))
static void error_clnt(const char *fmt, ...)
{
	char buf[1024];
	va_list params;
	int len;

	va_start(params, fmt);
	len = vsprintf(buf, fmt, params);
	va_end(params);
	send_sideband(1, 3, buf, len, LARGE_PACKET_MAX);
	die("sent error to the client: %s", buf);
}

static ssize_t process_input(int child_fd, int band)
{
	char buf[16384];
	ssize_t sz = read(child_fd, buf, sizeof(buf));
	if (sz < 0) {
		if (errno != EAGAIN && errno != EINTR)
			error_clnt("read error: %s\n", strerror(errno));
		return sz;
	}
	send_sideband(1, band, buf, sz, LARGE_PACKET_MAX);
	return sz;
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
		if (pfd[1].revents & POLLIN)
			/* Status stream ready */
			if (process_input(pfd[1].fd, 2))
				continue;
		if (pfd[0].revents & POLLIN)
			/* Data stream ready */
			if (process_input(pfd[0].fd, 1))
				continue;

		if (waitpid(writer, &status, 0) < 0)
			error_clnt("%s", lostchild);
		else if (!WIFEXITED(status) || WEXITSTATUS(status) > 0)
			error_clnt("%s", deadchild);
		packet_flush(1);
		break;
	}
	return 0;
}
