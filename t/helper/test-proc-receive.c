#include "cache.h"
#include "connect.h"
#include "parse-options.h"
#include "pkt-line.h"
#include "sigchain.h"
#include "test-tool.h"

static const char *proc_receive_usage[] = {
	"test-tool proc-receive [<options>...]",
	NULL
};

static int die_version = 0;
static int die_readline = 0;
static int no_push_options = 0;
static int use_atomic = 0;
static int use_push_options = 0;
static int verbose = 0;
static int version = 1;
static struct string_list returns = STRING_LIST_INIT_NODUP;

struct command {
	struct command *next;
	const char *error_string;
	unsigned int skip_update:1,
		     did_not_exist:1;
	int index;
	struct object_id old_oid;
	struct object_id new_oid;
	char ref_name[FLEX_ARRAY]; /* more */
};

static void proc_receive_verison(struct packet_reader *reader) {
	int server_version = 0;

	for (;;) {
		int linelen;

		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			break;

		if (reader->pktlen > 8 && starts_with(reader->line, "version=")) {
			server_version = atoi(reader->line+8);
			linelen = strlen(reader->line);
			if (linelen < reader->pktlen) {
				const char *feature_list = reader->line + linelen + 1;
				if (parse_feature_request(feature_list, "atomic"))
					use_atomic= 1;
				if (parse_feature_request(feature_list, "push-options"))
					use_push_options = 1;
			}
		}
	}

	if (server_version != 1 || die_version)
		die("bad protocol version: %d", server_version);

	packet_write_fmt(1, "version=%d%c%s\n",
			 version, '\0',
			 use_push_options && !no_push_options ? "push-options": "");
	packet_flush(1);
}

static void proc_receive_read_commands(struct packet_reader *reader,
				       struct command **commands)
{
	struct command **tail = commands;

	for (;;) {
		struct object_id old_oid, new_oid;
		struct command *cmd;
		const char *refname;
		const char *p;

		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			break;

		if (parse_oid_hex(reader->line, &old_oid, &p) ||
		    *p++ != ' ' ||
		    parse_oid_hex(p, &new_oid, &p) ||
		    *p++ != ' ' ||
		    die_readline)
			die("protocol error: expected 'old new ref', got '%s'",
			    reader->line);
		refname = p;
		FLEX_ALLOC_STR(cmd, ref_name, refname);
		oidcpy(&cmd->old_oid, &old_oid);
		oidcpy(&cmd->new_oid, &new_oid);

		*tail = cmd;
		tail = &cmd->next;
	}
}

static void proc_receive_read_push_options(struct packet_reader *reader,
					   struct string_list *options)
{

	if (no_push_options || !use_push_options)
	       return;

	while (1) {
		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			break;

		string_list_append(options, reader->line);
	}
}

int cmd__proc_receive(int argc, const char **argv)
{
	struct packet_reader reader;
	struct command *commands = NULL;
	struct string_list push_options = STRING_LIST_INIT_DUP;
	struct string_list_item *item;
	struct option options[] = {
		OPT_BOOL(0, "no-push-options", &no_push_options,
			 "disable push options"),
		OPT_BOOL(0, "die-version", &die_version,
			 "die during version negotiation"),
		OPT_BOOL(0, "die-readline", &die_readline,
			 "die when readline"),
		OPT_STRING_LIST('r', "return", &returns, "old/new/ref/status/msg",
				"return of results"),
		OPT__VERBOSE(&verbose, "be verbose"),
		OPT_INTEGER('V', "version", &version,
			    "use this protocol version number"),
		OPT_END()
	};

	argc = parse_options(argc, argv, "test-tools", options, proc_receive_usage, 0);
	if (argc > 0)
		usage_msg_opt("Too many arguments.", proc_receive_usage, options);

	packet_reader_init(&reader, 0, NULL, 0,
			   PACKET_READ_CHOMP_NEWLINE |
			   PACKET_READ_DIE_ON_ERR_PACKET);

	sigchain_push(SIGPIPE, SIG_IGN);
	proc_receive_verison(&reader);
	proc_receive_read_commands(&reader, &commands);
	proc_receive_read_push_options(&reader, &push_options);

	if (verbose) {
		struct command *cmd;

		if (use_push_options || use_atomic)
			fprintf(stderr, "proc-receive:%s%s\n",
				use_atomic? " atomic": "",
				use_push_options ? " push_options": "");

		for (cmd = commands; cmd; cmd = cmd->next)
			fprintf(stderr, "proc-receive< %s %s %s\n",
				oid_to_hex(&cmd->old_oid),
				oid_to_hex(&cmd->new_oid),
				cmd->ref_name);

		if (push_options.nr > 0)
			for_each_string_list_item(item, &push_options)
				fprintf(stderr, "proc-receive< %s\n", item->string);

		if (returns.nr)
			for_each_string_list_item(item, &returns)
				fprintf(stderr, "proc-receive> %s\n", item->string);
	}

	if (returns.nr)
		for_each_string_list_item(item, &returns)
			packet_write_fmt(1, "%s\n", item->string);
	packet_flush(1);
	sigchain_pop(SIGPIPE);

	return 0;
}
