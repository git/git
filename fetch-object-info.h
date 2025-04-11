#ifndef FETCH_OBJECT_INFO_H
#define FETCH_OBJECT_INFO_H

#include "pkt-line.h"
#include "protocol.h"
#include "object-store.h"

struct object_info_args {
	struct string_list *object_info_options;
	const struct string_list *server_options;
	struct oid_array *oids;
};

/*
 * Sends git-cat-file object-info command into the request buf and read the
 * results from packets.
 */
int fetch_object_info(enum protocol_version version, struct object_info_args *args,
		      struct packet_reader *reader, struct object_info *object_info_data,
		      int stateless_rpc, int fd_out);

#endif /* FETCH_OBJECT_INFO_H */
