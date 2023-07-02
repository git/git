#include "git-compat-util.h"
#include "test-tool.h"
#include "pkt-line.h"
#include "write-or-die.h"

static void pack_line(const char *line)
{
	if (!strcmp(line, "0000") || !strcmp(line, "0000\n"))
		packet_flush(1);
	else if (!strcmp(line, "0001") || !strcmp(line, "0001\n"))
		packet_delim(1);
	else
		packet_write_fmt(1, "%s", line);
}

static void pack(int argc, const char **argv)
{
	if (argc) { /* read from argv */
		int i;
		for (i = 0; i < argc; i++)
			pack_line(argv[i]);
	} else { /* read from stdin */
		char line[LARGE_PACKET_MAX];
		while (fgets(line, sizeof(line), stdin)) {
			pack_line(line);
		}
	}
}

static void pack_raw_stdin(void)
{
	struct strbuf sb = STRBUF_INIT;

	if (strbuf_read(&sb, 0, 0) < 0)
		die_errno("failed to read from stdin");
	packet_write(1, sb.buf, sb.len);
	strbuf_release(&sb);
}

static void unpack(void)
{
	struct packet_reader reader;
	packet_reader_init(&reader, 0, NULL, 0,
			   PACKET_READ_GENTLE_ON_EOF |
			   PACKET_READ_CHOMP_NEWLINE);

	while (packet_reader_read(&reader) != PACKET_READ_EOF) {
		switch (reader.status) {
		case PACKET_READ_EOF:
			break;
		case PACKET_READ_NORMAL:
			printf("%s\n", reader.line);
			break;
		case PACKET_READ_FLUSH:
			printf("0000\n");
			break;
		case PACKET_READ_DELIM:
			printf("0001\n");
			break;
		case PACKET_READ_RESPONSE_END:
			printf("0002\n");
			break;
		}
	}
}

static void unpack_sideband(void)
{
	struct packet_reader reader;
	packet_reader_init(&reader, 0, NULL, 0,
			   PACKET_READ_GENTLE_ON_EOF |
			   PACKET_READ_CHOMP_NEWLINE);

	while (packet_reader_read(&reader) != PACKET_READ_EOF) {
		int band;
		int fd;

		switch (reader.status) {
		case PACKET_READ_EOF:
			break;
		case PACKET_READ_NORMAL:
			band = reader.line[0] & 0xff;
			if (band < 1 || band > 2)
				continue; /* skip non-sideband packets */
			fd = band;

			write_or_die(fd, reader.line + 1, reader.pktlen - 1);
			break;
		case PACKET_READ_FLUSH:
			return;
		case PACKET_READ_DELIM:
		case PACKET_READ_RESPONSE_END:
			break;
		}
	}
}

static int send_split_sideband(void)
{
	const char *part1 = "Hello,";
	const char *primary = "\001primary: regular output\n";
	const char *part2 = " world!\n";

	send_sideband(1, 2, part1, strlen(part1), LARGE_PACKET_MAX);
	packet_write(1, primary, strlen(primary));
	send_sideband(1, 2, part2, strlen(part2), LARGE_PACKET_MAX);
	packet_response_end(1);

	return 0;
}

static int receive_sideband(void)
{
	return recv_sideband("sideband", 0, 1);
}

int cmd__pkt_line(int argc, const char **argv)
{
	if (argc < 2)
		die("too few arguments");

	if (!strcmp(argv[1], "pack"))
		pack(argc - 2, argv + 2);
	else if (!strcmp(argv[1], "pack-raw-stdin"))
		pack_raw_stdin();
	else if (!strcmp(argv[1], "unpack"))
		unpack();
	else if (!strcmp(argv[1], "unpack-sideband"))
		unpack_sideband();
	else if (!strcmp(argv[1], "send-split-sideband"))
		send_split_sideband();
	else if (!strcmp(argv[1], "receive-sideband"))
		receive_sideband();
	else
		die("invalid argument '%s'", argv[1]);

	return 0;
}
