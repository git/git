#include "cache.h"
#include "hash.h"
#include "config.h"
#include "connect.h"
#include "parse-options.h"
#include "pkt-line.h"
#include "sigchain.h"
#include "test-tool.h"

static const char *hide_refs_usage[] = {
	"test-tool hide-refs [<options>...]",
	NULL
};

static int die_before_read_ref;
static int die_after_proc_ref;
static int version = 1;
static int hash_size = GIT_SHA1_HEXSZ;
static struct string_list hidelist = STRING_LIST_INIT_NODUP;

static void hide_refs_verison(struct packet_reader *reader) {
	int server_version = 0;

	for (;;) {
		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			break;

		if (reader->pktlen > 8 && starts_with(reader->line, "version=")) {
			server_version = atoi(reader->line+8);
			if (server_version != 1)
				die("bad protocol version: %d", server_version);
		}
	}

	packet_write_fmt(1, "version=%d\n", version);
	packet_flush(1);
}

static void hide_refs_proc(struct packet_reader *reader)
{
	const char *p;
	struct strbuf buf = STRBUF_INIT;
	enum packet_read_status status;

	if (die_before_read_ref)
		die("die with the --die-before-read-ref option");

	for (;;) {
		status = packet_reader_read(reader);
		if (status == PACKET_READ_EOF)
			exit(0);

		if (status != PACKET_READ_NORMAL)
			break;

		p = reader->line;
		strbuf_reset(&buf);
		strbuf_addstr(&buf, reader->line);
	}

	p = strchr(buf.buf, ':');
	if (unsorted_string_list_has_string(&hidelist, p + 1)) {
		packet_write_fmt(1, "hide");
	}

	if (die_after_proc_ref)
		die("die with the --die-after-proc-refs option");

	packet_flush(1);
}

int cmd__hide_refs(int argc, const char **argv) {
	int nongit_ok = 0;
	struct packet_reader reader;
	const char *value = NULL;
	struct option options[] = {
		OPT_BOOL(0, "die-before-read-ref", &die_before_read_ref,
			 "die when reading first reference"),
		OPT_BOOL(0, "die-after-proc-refs", &die_after_proc_ref,
			 "die after proc ref"),
		OPT_STRING_LIST('H', "hide", &hidelist, "refs-to-force-hidden",
				"refs that will be force hidden"),
		OPT_INTEGER('V', "version", &version,
			    "use this protocol version number"),
		OPT_END()
	};

	setup_git_directory_gently(&nongit_ok);

	argc = parse_options(argc, argv, "test-tools", options, hide_refs_usage, 0);
	if (argc > 0)
		usage_msg_opt("Too many arguments.", hide_refs_usage, options);

	packet_reader_init(&reader, 0, NULL, 0, PACKET_READ_CHOMP_NEWLINE | PACKET_READ_GENTLE_ON_EOF);

	if (!git_config_get_value("extensions.objectformat", &value)) {
		if (!strcmp(value, "sha256"))
			hash_size = GIT_SHA256_HEXSZ;
	}

	hide_refs_verison(&reader);
	for (;;) {
		hide_refs_proc(&reader);
	}

	return 0;
}
