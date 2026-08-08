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
static void send_object_info_request(const int fd_out,
				     const struct string_list *server_options,
				     const struct oid_array *oids,
				     unsigned ask_size)
{
	struct strbuf req_buf = STRBUF_INIT;

	write_command_and_capabilities(&req_buf, "object-info", server_options);

	if (ask_size)
		packet_buf_write(&req_buf, "size");

	if (oids)
		for (size_t i = 0; i < oids->nr; i++)
			packet_buf_write(&req_buf, "oid %s",
					 oid_to_hex(&oids->oid[i]));

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

void fetch_object_info(const enum protocol_version version,
		       const struct string_list *server_options,
		       const struct oid_array *oids,
		       struct packet_reader *reader,
		       struct fetch_object_info_results *results,
		       const int stateless_rpc,
		       const int fd_out)
{
	unsigned ask_size = 0;
	int size_index = -1;
	size_t wanted;

	results->nr = oids->nr;
	CALLOC_ARRAY(results->unrecognized, results->nr);

	switch (version) {
	case protocol_v2:
		if (!server_supports_v2("object-info"))
			die(_("object-info capability is not enabled on the server"));

		if (results->wants_size &&
		    server_supports_feature("object-info", "size", 0))
			ask_size = 1;

		/*
		 * Even if no options are left, we still send the oid so we get
		 * at least an existence check.
		 */
		send_object_info_request(fd_out, server_options, oids, ask_size);
		break;
	case protocol_v1:
	case protocol_v0:
		die(_("object-info requires protocol v2"));
	case protocol_unknown_version:
		BUG("unknown protocol version");
	}
	wanted = ask_size;

	for (size_t i = 0; i < wanted; i++) {
		if (packet_reader_read(reader) != PACKET_READ_NORMAL) {
			check_stateless_delimiter(stateless_rpc, reader,
						  "stateless delimiter expected");
			die(_("object-info: expected %" PRIuMAX " attributes, got %" PRIuMAX),
			    (uintmax_t)wanted, (uintmax_t)i);
		}

		if (!strcmp(reader->line, "size")) {
			if (!ask_size)
				die(_("object-info: unrequested 'size' attribute"));
			if (results->sizes)
				die(_("object-info: duplicate 'size' attribute"));
			size_index = (int)i;
			CALLOC_ARRAY(results->sizes, results->nr);
		} else {
			die(_("object-info: unknown attribute '%s'"),
			    reader->line);
		}
	}

	for (size_t i = 0; i < oids->nr; i++) {
		struct string_list object_info_values = STRING_LIST_INIT_DUP;

		if (packet_reader_read(reader) != PACKET_READ_NORMAL)
			die(_("object-info: expected %" PRIuMAX " objects, got %" PRIuMAX),
			    (uintmax_t)oids->nr, (uintmax_t)i);

		string_list_split(&object_info_values, reader->line, " ", -1);

		if (strcmp(object_info_values.items[0].string,
			   oid_to_hex(&oids->oid[i])))
			die(_("object-info: expected OID: %s, got %s"),
			    oid_to_hex(&oids->oid[i]),
			    object_info_values.items[0].string);

		/*
		 * If the response is two elements but the second one is an
		 * empty string, that means that the OID is unrecognized by the
		 * server.
		 */
		if (object_info_values.nr >= 2 &&
		    !strcmp(object_info_values.items[1].string, "")) {
			results->unrecognized[i] = 1;
			string_list_clear(&object_info_values, 0);
			continue;
		}

		/*
		 * Because we only ask for attributes the server said it
		 * supports, we expect the answer to have one value per
		 * requested attribute, plus the OID.
		 */
		if (wanted + 1 != object_info_values.nr)
			die("object-info: unexpected number of attributes: %s",
			    reader->line);

		if (results->sizes &&
		    parse_object_size(object_info_values.items[size_index + 1].string,
				      &results->sizes[i]))
			die("object-info: object %s has invalid size %s",
			    object_info_values.items[0].string,
			    object_info_values.items[size_index + 1].string);

		string_list_clear(&object_info_values, 0);
	}

	if (packet_reader_read(reader) != PACKET_READ_FLUSH)
		die(_("object-info: expected flush after %" PRIuMAX " objects"),
		    (uintmax_t)oids->nr);

	check_stateless_delimiter(stateless_rpc, reader, "stateless delimiter expected");
}

void free_fetch_object_info_results(struct fetch_object_info_results *results)
{
	free(results->sizes);
	free(results->unrecognized);
	memset(results, 0, sizeof(*results));
}
