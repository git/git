#ifndef LS_REFS_H
#define LS_REFS_H

struct repository;
struct packet_reader;
int ls_refs(struct repository *r, struct packet_reader *request);
int ls_refs_advertise(struct repository *r, struct strbuf *value);

#endif /* LS_REFS_H */
