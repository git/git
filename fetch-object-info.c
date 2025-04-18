#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "pkt-line.h"
#include "connect.h"
#include "oid-array.h"
#include "object-store.h"
#include "fetch-object-info.h"
#include "string-list.h"

/* Sends git-cat-file object-info command and its arguments into the request buffer. */
static void send_object_info_request(const int fd_out, struct object_info_args *args)
{
	struct strbuf req_buf = STRBUF_INIT;

	write_command_and_capabilities(&req_buf, "object-info", args->server_options);

	if (unsorted_string_list_has_string(args->object_info_options, "size"))
		packet_buf_write(&req_buf, "size");

	if (args->oids)
		for (size_t i = 0; i < args->oids->nr; i++)
			packet_buf_write(&req_buf, "oid %s", oid_to_hex(&args->oids->oid[i]));

	packet_buf_flush(&req_buf);
	if (write_in_full(fd_out, req_buf.buf, req_buf.len) < 0)
		die_errno(_("unable to write request to remote"));

	strbuf_release(&req_buf);
}

int fetch_object_info(const enum protocol_version version, struct object_info_args *args,
		      struct packet_reader *reader, struct object_info *object_info_data,
		      const int stateless_rpc, const int fd_out)
{
	int size_index = -1;

	switch (version) {
	case protocol_v2:
		if (!server_supports_v2("object-info"))
			die(_("object-info capability is not enabled on the server"));
		send_object_info_request(fd_out, args);
		break;
	case protocol_v1:
	case protocol_v0:
		die(_("unsupported protocol version. expected v2"));
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	for (size_t i = 0; i < args->object_info_options->nr; i++) {
		if (packet_reader_read(reader) != PACKET_READ_NORMAL) {
			check_stateless_delimiter(stateless_rpc, reader, "stateless delimiter expected");
			return -1;
		}
		if (!string_list_has_string(args->object_info_options, reader->line))
			return -1;
		if (!strcmp(reader->line, "size")) {
			size_index = i;
			for (size_t j = 0; j < args->oids->nr; j++)
				object_info_data[j].sizep = xcalloc(1, sizeof(*object_info_data[j].sizep));
		}
	}

	for (size_t i = 0; packet_reader_read(reader) == PACKET_READ_NORMAL && i < args->oids->nr; i++){
		struct string_list object_info_values = STRING_LIST_INIT_DUP;

		string_list_split(&object_info_values, reader->line, ' ', -1);
		if (0 <= size_index) {
			if (!strcmp(object_info_values.items[1 + size_index].string, ""))
				die("object-info: not our ref %s",
					object_info_values.items[0].string);

			if (strtoul_ul(object_info_values.items[1 + size_index].string, 10, object_info_data[i].sizep))
				die("object-info: ref %s has invalid size %s",
					object_info_values.items[0].string,
					object_info_values.items[1 + size_index].string);
		}

		string_list_clear(&object_info_values, 0);
	}
	check_stateless_delimiter(stateless_rpc, reader, "stateless delimiter expected");

	return 0;
}
