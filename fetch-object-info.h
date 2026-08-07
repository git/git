#ifndef FETCH_OBJECT_INFO_H
#define FETCH_OBJECT_INFO_H

#include "pkt-line.h"
#include "protocol.h"

struct fetch_object_info_results {
	size_t *sizes;
	uint8_t *unrecognized;
	size_t nr;
	unsigned wants_size:1;
};

#define FETCH_OBJECT_INFO_RESULTS_INIT { 0 }

struct oid_array;
/*
 * Sends git-cat-file object-info command into the request buf and reads the
 * results from packets.
 *
 * The caller sets the wants_* flags in "results" to indicate which attributes
 * it is interested in. On return, "results" holds one array per attribute that
 * the server both advertised and answered with. An array left NULL means the
 * attribute is not available.
 * Release them with free_fetch_object_info_results().
 */
void fetch_object_info(enum protocol_version version,
		       const struct string_list *server_options,
		       const struct oid_array *oids,
		       struct packet_reader *reader,
		       struct fetch_object_info_results *results,
		       int stateless_rpc,
		       int fd_out);

void free_fetch_object_info_results(struct fetch_object_info_results *results);

#endif /* FETCH_OBJECT_INFO_H */
