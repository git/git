/*
 * Copyright (c) 2006 Franck Bui-Huu
 */
#include "cache.h"
#include "builtin.h"
#include "archive.h"
#include "pkt-line.h"
#include "sideband.h"
#include "run-command.h"
#include "strvec.h"
#include "config.h"

static const char upload_archive_usage[] =
	"git upload-archive <repo>";

static const char deadchild[] =
"git upload-archive: archiver died with error";

#define MAX_ARGS (64)

int cmd_upload_archive_writer(int argc, const char **argv, const char *prefix)
{
	struct strvec sent_argv = STRVEC_INIT;
	const char *arg_cmd = "argument ";

	if (argc != 2 || !strcmp(argv[1], "-h"))
		usage(upload_archive_usage);

	if (!enter_repo(argv[1], 0))
		die("'%s' does not appear to be a git repository", argv[1]);

	git_config(git_default_config, NULL);
	init_archivers();

	/* put received options in sent_argv[] */
	strvec_push(&sent_argv, "git-upload-archive");
	for (;;) {
		char *buf = packet_read_line(0, NULL);
		if (!buf)
			break;	/* got a flush */
		if (sent_argv.nr > MAX_ARGS)
			die("Too many options (>%d)", MAX_ARGS - 1);

		if (!starts_with(buf, arg_cmd))
			die("'argument' token or flush expected");
		strvec_push(&sent_argv, buf + strlen(arg_cmd));
	}

	/* parse all options sent by the client */
	return write_archive(sent_argv.nr, sent_argv.v, prefix,
			     the_repository, NULL, 1);
}

__attribute__((format (printf, 1, 2)))
static void error_clnt(const char *fmt, ...)
{
	struct strbuf buf = STRBUF_INIT;
	va_list params;

	va_start(params, fmt);
	strbuf_vaddf(&buf, fmt, params);
	va_end(params);
	send_sideband(1, 3, buf.buf, buf.len, LARGE_PACKET_MAX);
	die("sent error to the client: %s", buf.buf);
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
	struct child_process writer = { argv };

	git_config(git_default_config, NULL);
	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage(upload_archive_usage);

	/*
	 * Set up sideband subprocess.
	 *
	 * We (parent) monitor and read from child, sending its fd#1 and fd#2
	 * multiplexed out to our fd#1.  If the child dies, we tell the other
	 * end over channel #3.
	 */
	argv[0] = "upload-archive--writer";
	writer.out = writer.err = -1;
	writer.git_cmd = 1;
	if (start_command(&writer)) {
		int err = errno;
		packet_write_fmt(1, "NACK unable to spawn subprocess\n");
		die("upload-archive: %s", strerror(err));
	}

	packet_write_fmt(1, "ACK\n");
	packet_flush(1);

	while (1) {
		struct pollfd pfd[2];

		pfd[0].fd = writer.out;
		pfd[0].events = POLLIN;
		pfd[1].fd = writer.err;
		pfd[1].events = POLLIN;
		if (poll(pfd, 2, -1) < 0) {
			if (errno != EINTR) {
				error_errno("poll failed resuming");
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

		if (finish_command(&writer))
			error_clnt("%s", deadchild);
		packet_flush(1);
		break;
	}
	return 0;
}
