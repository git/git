#ifndef SEND_PACK_H
#define SEND_PACK_H

struct send_pack_args {
	const char *receivepack;
	unsigned verbose:1,
		send_all:1,
		send_mirror:1,
		force_update:1,
		use_thin_pack:1,
		dry_run:1;
};

int send_pack(struct send_pack_args *args,
	      const char *dest, struct remote *remote,
	      int nr_heads, const char **heads);

#endif
