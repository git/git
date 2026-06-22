#include "git-compat-util.h"
#include "packfile.h"
#include "packfile-list.h"

void packfile_list_clear(struct packfile_list *list)
{
	struct packfile_list_entry *e, *next;

	for (e = list->head; e; e = next) {
		next = e->next;
		free(e);
	}

	list->head = list->tail = NULL;
}

static struct packfile_list_entry *packfile_list_remove_internal(struct packfile_list *list,
								 struct packed_git *pack)
{
	struct packfile_list_entry *e, *prev;

	for (e = list->head, prev = NULL; e; prev = e, e = e->next) {
		if (e->pack != pack)
			continue;

		if (prev)
			prev->next = e->next;
		if (list->head == e)
			list->head = e->next;
		if (list->tail == e)
			list->tail = prev;

		return e;
	}

	return NULL;
}

void packfile_list_remove(struct packfile_list *list, struct packed_git *pack)
{
	free(packfile_list_remove_internal(list, pack));
}

void packfile_list_prepend(struct packfile_list *list, struct packed_git *pack)
{
	struct packfile_list_entry *entry;

	entry = packfile_list_remove_internal(list, pack);
	if (!entry) {
		entry = xmalloc(sizeof(*entry));
		entry->pack = pack;
	}
	entry->next = list->head;

	list->head = entry;
	if (!list->tail)
		list->tail = entry;
}

void packfile_list_append(struct packfile_list *list, struct packed_git *pack)
{
	struct packfile_list_entry *entry;

	entry = packfile_list_remove_internal(list, pack);
	if (!entry) {
		entry = xmalloc(sizeof(*entry));
		entry->pack = pack;
	}
	entry->next = NULL;

	if (list->tail) {
		list->tail->next = entry;
		list->tail = entry;
	} else {
		list->head = list->tail = entry;
	}
}

struct packed_git *packfile_list_find_oid(struct packfile_list_entry *packs,
					  const struct object_id *oid)
{
	for (; packs; packs = packs->next)
		if (find_pack_entry_one(oid, packs->pack))
			return packs->pack;
	return NULL;
}
