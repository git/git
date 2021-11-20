#ifndef UPLOAD_PACK_H
#define UPLOAD_PACK_H

void upload_pack(const int advertise_refs, const int stateless_rpc,
		 const int timeout);

struct repository;
struct packet_reader;
int upload_pack_v2(struct repository *r, struct packet_reader *request);

struct strbuf;
int upload_pack_advertise(struct repository *r,
			  struct strbuf *value);

#endif /* UPLOAD_PACK_H */
