#ifndef SEND_PACK_H
#define SEND_PACK_H

#include "string-list.h"

struct child_process;
struct oid_array;
struct ref;

/* Possible values for push_cert field in send_pack_args. */
#define SEND_PACK_PUSH_CERT_NEVER 0
#define SEND_PACK_PUSH_CERT_IF_ASKED 1
#define SEND_PACK_PUSH_CERT_ALWAYS 2

struct send_pack_args {
	const char *url;
	unsigned verbose:1,
		quiet:1,
		porcelain:1,
		progress:1,
		send_mirror:1,
		force_update:1,
		use_thin_pack:1,
		use_ofs_delta:1,
		dry_run:1,
		/* One of the SEND_PACK_PUSH_CERT_* constants. */
		push_cert:2,
		stateless_rpc:1,
		atomic:1,
		disable_bitmaps:1;
	const struct string_list *push_options;
};

struct option;
int option_parse_push_signed(const struct option *opt,
			     const char *arg, int unset);

int send_pack(struct send_pack_args *args,
	      int fd[], struct child_process *conn,
	      struct ref *remote_refs, struct oid_array *extra_have);

#endif
