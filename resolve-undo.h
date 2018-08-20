#ifndef RESOLVE_UNDO_H
#define RESOLVE_UNDO_H

#include "cache.h"

struct resolve_undo_info {
	unsigned int mode[3];
	struct object_id oid[3];
};

extern void record_resolve_undo(struct index_state *, struct cache_entry *);
extern void resolve_undo_write(struct strbuf *, struct string_list *);
extern struct string_list *resolve_undo_read(const char *, unsigned long);
extern void resolve_undo_clear_index(struct index_state *);
extern int unmerge_index_entry_at(struct index_state *, int);
extern void unmerge_index(struct index_state *, const struct pathspec *);
extern void unmerge_marked_index(struct index_state *);

#endif
