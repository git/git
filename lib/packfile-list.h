#ifndef PACKFILE_LIST_H
#define PACKFILE_LIST_H

struct object_id;

struct packfile_list {
	struct packfile_list_entry *head, *tail;
};

struct packfile_list_entry {
	struct packfile_list_entry *next;
	struct packed_git *pack;
};

void packfile_list_clear(struct packfile_list *list);
void packfile_list_remove(struct packfile_list *list, struct packed_git *pack);
void packfile_list_prepend(struct packfile_list *list, struct packed_git *pack);
void packfile_list_append(struct packfile_list *list, struct packed_git *pack);

/*
 * Find the pack within the "packs" list whose index contains the object
 * "oid". For general object lookups, you probably don't want this; use
 * find_pack_entry() instead.
 */
struct packed_git *packfile_list_find_oid(struct packfile_list_entry *packs,
					  const struct object_id *oid);

#endif
