/*
 * Copyright (c) 2006 Franck Bui-Huu
 * Copyright (c) 2006 Rene Scharfe
 */
#include "cache.h"
#include "builtin.h"
#include "archive.h"
#include "connect.h"
#include "transport.h"
#include "parse-options.h"
#include "pkt-line.h"
#include "protocol.h"
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

static void do_v2_command_and_cap(int out)
{
	packet_write_fmt(out, "command=archive\n");
	/* Capability list would go here, if we wanted to request any. */
	packet_delim(out);
}

static int run_remote_archiver(int argc, const char **argv,
			       const char *remote, const char *exec,
			       const char *name_hint)
{
	int fd[2], i, rv;
	struct transport *transport;
	struct remote *_remote;
	struct packet_reader reader;
	enum packet_read_status status;
	enum protocol_version version;

	_remote = remote_get(remote);
	if (!_remote->url[0])
		die(_("git archive: Remote with no URL"));
	transport = transport_get(_remote, _remote->url[0]);
	transport_connect(transport, "git-upload-archive", exec, fd);

	packet_reader_init(&reader, fd[0], NULL, 0, PACKET_READ_CHOMP_NEWLINE);

	version = discover_version(&reader);

	if (version == protocol_v2 && server_supports_v2("archive", 0))
		do_v2_command_and_cap(fd[1]);

	/*
	 * Inject a fake --format field at the beginning of the
	 * arguments, with the format inferred from our output
	 * filename. This way explicit --format options can override
	 * it.
	 */
	if (name_hint) {
		const char *format = archive_format_from_filename(name_hint);
		if (format)
			packet_write_fmt(fd[1], "argument --format=%s\n", format);
	}
	for (i = 1; i < argc; i++)
		packet_write_fmt(fd[1], "argument %s\n", argv[i]);
	packet_flush(fd[1]);

	if (version == protocol_v0) {
		status = packet_reader_read(&reader);

		if (status != PACKET_READ_NORMAL || reader.pktlen <= 0)
			die(_("git archive: expected ACK/NAK, got a flush packet"));
		if (strcmp(reader.line, "ACK")) {
			if (starts_with(reader.line, "NACK "))
				die(_("git archive: NACK %s"), reader.line + 5);
			if (starts_with(reader.line, "ERR "))
				die(_("remote error: %s"), reader.line + 4);
			die(_("git archive: protocol error"));
		}

		status = packet_reader_read(&reader);
		if (status == PACKET_READ_NORMAL && reader.pktlen > 0)
			die(_("git archive: expected a flush"));
	} else if (version == protocol_v2 &&
		   (starts_with(transport->url, "http://") ||
		    starts_with(transport->url, "https://")))
		/*
		 * Commands over HTTP require two requests, so there's an
		 * additional server response to parse. We do only basic sanity
		 * checking here that the versions presented match across
		 * requests.
		 */
		if (version != discover_version(&reader))
			die(_("git archive: received different protocol versions in subsequent requests"));

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
		OPT_FILENAME('o', "output", &output,
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

	return write_archive(argc, argv, prefix, the_repository, output, 0);
}
