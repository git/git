#ifndef PROTOCOL_CAPS_H
#define PROTOCOL_CAPS_H

struct repository;
struct strvec;
struct packet_reader;
int cap_object_info(struct repository *r, struct strvec *keys,
		    struct packet_reader *request);

#endif /* PROTOCOL_CAPS_H */