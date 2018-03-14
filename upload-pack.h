#ifndef UPLOAD_PACK_H
#define UPLOAD_PACK_H

struct upload_pack_options {
	int stateless_rpc;
	int advertise_refs;
	unsigned int timeout;
	int daemon_mode;
};

void upload_pack(struct upload_pack_options *options);

#endif /* UPLOAD_PACK_H */
