/*
 * Copyright (c) 2006 Franck Bui-Huu
 * Copyright (c) 2006 Rene Scharfe
 */
#include "cache.h"
#include "builtin.h"
#include "archive.h"
#include "transport.h"
#include "parse-options.h"
#include "pkt-line.h"
#include "sideband.h"

static void create_output_file(const char *output_file)
{
	int output_fd = open(output_file, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (output_fd < 0)
		die_errno(_("could not create archive file '%s'"), output_file);
	if (output_fd != 1) {
		if (dup2(output_fd, 1) < 0)
			die_errno(_("could not redirect output"));
		else
			close(output_fd);
	}
}

static int run_remote_archiver(int argc, const char **argv,
			       const char *remote, const char *exec,
			       const char *name_hint)
{
	char buf[LARGE_PACKET_MAX];
	int fd[2], i, len, rv;
	struct transport *transport;
	struct remote *_remote;

	_remote = remote_get(remote);
	if (!_remote->url[0])
		die(_("git archive: Remote with no URL"));
	transport = transport_get(_remote, _remote->url[0]);
	transport_connect(transport, "git-upload-archive", exec, fd);

	/*
	 * Inject a fake --format field at the beginning of the
	 * arguments, with the format inferred from our output
	 * filename. This way explicit --format options can override
	 * it.
	 */
	if (name_hint) {
		const char *format = archive_format_from_filename(name_hint);
		if (format)
			packet_write(fd[1], "argument --format=%s\n", format);
	}
	for (i = 1; i < argc; i++)
		packet_write(fd[1], "argument %s\n", argv[i]);
	packet_flush(fd[1]);

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (!len)
		die(_("git archive: expected ACK/NAK, got EOF"));
	if (buf[len-1] == '\n')
		buf[--len] = 0;
	if (strcmp(buf, "ACK")) {
		if (len > 5 && !prefixcmp(buf, "NACK "))
			die(_("git archive: NACK %s"), buf + 5);
		if (len > 4 && !prefixcmp(buf, "ERR "))
			die(_("remote error: %s"), buf + 4);
		die(_("git archive: protocol error"));
	}

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (len)
		die(_("git archive: expected a flush"));

	/* Now, start reading from fd[0] and spit it out to stdout */
	rv = recv_sideband("archive", fd[0], 1);
	rv |= transport_disconnect(transport);

	return !!rv;
}

#define PARSE_OPT_KEEP_ALL ( PARSE_OPT_KEEP_DASHDASH | 	\
			     PARSE_OPT_KEEP_ARGV0 | 	\
			     PARSE_OPT_KEEP_UNKNOWN |	\
			     PARSE_OPT_NO_INTERNAL_HELP	)

int cmd_archive(int argc, const char **argv, const char *prefix)
{
	const char *exec = "git-upload-archive";
	const char *output = NULL;
	const char *remote = NULL;
	struct option local_opts[] = {
		OPT_STRING('o', "output", &output, N_("file"),
			N_("write the archive to this file")),
		OPT_STRING(0, "remote", &remote, N_("repo"),
			N_("retrieve the archive from remote repository <repo>")),
		OPT_STRING(0, "exec", &exec, N_("command"),
			N_("path to the remote git-upload-archive command")),
		OPT_END()
	};

	argc = parse_options(argc, argv, prefix, local_opts, NULL,
			     PARSE_OPT_KEEP_ALL);

	if (output)
		create_output_file(output);

	if (remote)
		return run_remote_archiver(argc, argv, remote, exec, output);

	setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

	return write_archive(argc, argv, prefix, 1, output, 0);
}
