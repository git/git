#ifndef SEND_PACK_H
#define SEND_PACK_H

struct send_pack_args {
	unsigned verbose:1,
		send_mirror:1,
		force_update:1,
		use_thin_pack:1,
		dry_run:1;
};

int send_pack(struct send_pack_args *args,
	      int fd[], struct child_process *conn,
	      struct ref *remote_refs, struct extra_have_objects *extra_have);

#endif
