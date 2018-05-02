#ifndef UPLOAD_PACK_H
#define UPLOAD_PACK_H

struct upload_pack_options {
	int stateless_rpc;
	int advertise_refs;
	unsigned int timeout;
	int daemon_mode;
};

void upload_pack(struct upload_pack_options *options);

struct repository;
struct argv_array;
struct packet_reader;
extern int upload_pack_v2(struct repository *r, struct argv_array *keys,
			  struct packet_reader *request);

struct strbuf;
extern int upload_pack_advertise(struct repository *r,
				 struct strbuf *value);

#endif /* UPLOAD_PACK_H */
