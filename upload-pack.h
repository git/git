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
struct strvec;
struct packet_reader;
int upload_pack_v2(struct repository *r, struct strvec *keys,
		   struct packet_reader *request);

struct strbuf;
int upload_pack_advertise(struct repository *r,
			  struct strbuf *value);
int serve_upload_pack_startup_config(const char *var, const char *value,
				     void *data);

#endif /* UPLOAD_PACK_H */
