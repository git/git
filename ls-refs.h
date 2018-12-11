#ifndef LS_REFS_H
#define LS_REFS_H

struct repository;
struct argv_array;
struct packet_reader;
extern int ls_refs(struct repository *r, const char *config_context,
		   struct argv_array *keys,
		   struct packet_reader *request);

#endif /* LS_REFS_H */
