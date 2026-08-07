#include "git-compat-util.h"
#include "gettext.h"
#include "hex.h"
#include "pkt-line.h"
#include "connect.h"
#include "oid-array.h"
#include "odb.h"
#include "fetch-object-info.h"
#include "string-list.h"

/* Sends object-info command and its arguments into the request buffer. */
static void send_object_info_request(const int fd_out, struct object_info_args *args)
{
	struct strbuf req_buf = STRBUF_INIT;

	write_command_and_capabilities(&req_buf, "object-info", args->server_options);

	if (unsorted_string_list_has_string(args->object_info_options, "size"))
		packet_buf_write(&req_buf, "size");
	else if (args->object_info_options->nr)
		BUG("only size should be in object_info_options");

	if (args->oids)
		for (size_t i = 0; i < args->oids->nr; i++)
			packet_buf_write(&req_buf, "oid %s", oid_to_hex(&args->oids->oid[i]));

	packet_buf_flush(&req_buf);
	if (write_in_full(fd_out, req_buf.buf, req_buf.len) < 0)
		die_errno(_("unable to write request to remote"));

	strbuf_release(&req_buf);
}

static int parse_object_size(const char *s, size_t *res)
{
	uintmax_t uim;

	if (!s[0] || s[strspn(s, "0123456789")])
		return -1;
	errno = 0;
	uim = strtoumax(s, NULL, 10);
	if (errno || uim > SIZE_MAX)
		return -1;
	*res = uim;
	return 0;
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
		/*
		 * When removing an element from the list it gets swapped by the
		 * last element, iterate backwards to prevent elements skipping
		 * evaluation.
		 *
		 * object_info_options->nr can be safely casted without overflow
		 * because the number of options is a small known number (the
		 * supported placeholders which currently are size and type).
		 */
		for (int i = (int)args->object_info_options->nr - 1; i >= 0; i--)
			if (!server_supports_feature("object-info",
						     args->object_info_options->items[i].string, 0))
				unsorted_string_list_delete_item(args->object_info_options, i, 0);

		/*
		 * Even if no options are left, we still send the oid so we get
		 * at least an existence check.
		 */
		send_object_info_request(fd_out, args);
		break;
	case protocol_v1:
	case protocol_v0:
		die(_("object-info requires protocol v2"));
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}

	for (size_t i = 0; i < args->object_info_options->nr; i++) {
		if (packet_reader_read(reader) != PACKET_READ_NORMAL) {
			check_stateless_delimiter(stateless_rpc, reader,
						  "stateless delimiter expected");
			return -1;
		}

		if (!unsorted_string_list_has_string(args->object_info_options, reader->line))
			return -1;

		if (!strcmp(reader->line, "size")) {
			/*
			 * i is the number of supported options which currently
			 * is only size. No risk of overflow.
			 */
			size_index = (int)i;
			for (size_t j = 0; j < args->oids->nr; j++)
				object_info_data[j].sizep =
					xcalloc(1, sizeof(*object_info_data[j].sizep));
		} else {
			BUG("only size is supported");
		}
	}

	for (size_t i = 0;
	     packet_reader_read(reader) == PACKET_READ_NORMAL &&
	     i < args->oids->nr;
	     i++) {
		struct string_list object_info_values = STRING_LIST_INIT_DUP;

		string_list_split(&object_info_values, reader->line, " ", -1);

		if (strcmp(object_info_values.items[0].string,
			   oid_to_hex(&args->oids->oid[i])))
			die(_("object-info: expected OID: %s, got %s"),
			    oid_to_hex(&args->oids->oid[i]),
			    object_info_values.items[0].string);

		/*
		 * If the response is two elements but the second one is an
		 * empty string, that means that the OID is unrecognized by the
		 * server.
		 */
		if (object_info_values.nr >= 2 &&
		    !strcmp(object_info_values.items[1].string, "")) {
			object_info_data[i].unrecognized = 1;
			string_list_clear(&object_info_values, 0);
			continue;
		}

		/*
		 * Because we filter the options to be only the supported by
		 * the server we expect the server to answer with the same
		 * number of attributes requested.
		 */
		if (args->object_info_options->nr + 1 != object_info_values.nr)
			die("object-info: unexpected number of attributes: %s",
			    reader->line);

		if (size_index >= 0 &&
		    parse_object_size(object_info_values.items[size_index + 1].string,
				      object_info_data[i].sizep))
			die("object-info: ref %s has invalid size %s",
			    object_info_values.items[0].string,
			    object_info_values.items[size_index + 1].string);

		string_list_clear(&object_info_values, 0);
	}
	check_stateless_delimiter(stateless_rpc, reader, "stateless delimiter expected");

	return 0;
}
