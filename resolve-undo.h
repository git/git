#ifndef RESOLVE_UNDO_H
#define RESOLVE_UNDO_H

struct cache_entry;
struct index_state;
struct pathspec;
struct string_list;

#include "hash.h"

struct resolve_undo_info {
	unsigned int mode[3];
	struct object_id oid[3];
};

void record_resolve_undo(struct index_state *, struct cache_entry *);
void resolve_undo_write(struct strbuf *, struct string_list *);
struct string_list *resolve_undo_read(const char *, unsigned long);
void resolve_undo_clear_index(struct index_state *);
int unmerge_index_entry_at(struct index_state *, int);
void unmerge_index(struct index_state *, const struct pathspec *);
void unmerge_marked_index(struct index_state *);

#endif
