#ifndef LS_REFS_H
#define LS_REFS_H

struct repository;
struct argv_array;
struct packet_reader;
int ls_refs(struct repository *r, struct argv_array *keys,
	    struct packet_reader *request);

#endif /* LS_REFS_H */
