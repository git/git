#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "dir.h"
#include "hash.h"
#include "read-cache.h"
#include "resolve-undo.h"
#include "sparse-index.h"
#include "string-list.h"

/* The only error case is to run out of memory in string-list */
void record_resolve_undo(struct index_state *istate, struct cache_entry *ce)
{
	struct string_list_item *lost;
	struct resolve_undo_info *ui;
	struct string_list *resolve_undo;
	int stage = ce_stage(ce);

	if (!stage)
		return;

	if (!istate->resolve_undo) {
		CALLOC_ARRAY(resolve_undo, 1);
		resolve_undo->strdup_strings = 1;
		istate->resolve_undo = resolve_undo;
	}
	resolve_undo = istate->resolve_undo;
	lost = string_list_insert(resolve_undo, ce->name);
	if (!lost->util)
		lost->util = xcalloc(1, sizeof(*ui));
	ui = lost->util;
	oidcpy(&ui->oid[stage - 1], &ce->oid);
	ui->mode[stage - 1] = ce->ce_mode;
}

void resolve_undo_write(struct strbuf *sb, struct string_list *resolve_undo)
{
	struct string_list_item *item;
	for_each_string_list_item(item, resolve_undo) {
		struct resolve_undo_info *ui = item->util;
		int i;

		if (!ui)
			continue;
		strbuf_addstr(sb, item->string);
		strbuf_addch(sb, 0);
		for (i = 0; i < 3; i++)
			strbuf_addf(sb, "%o%c", ui->mode[i], 0);
		for (i = 0; i < 3; i++) {
			if (!ui->mode[i])
				continue;
			strbuf_add(sb, ui->oid[i].hash, the_hash_algo->rawsz);
		}
	}
}

struct string_list *resolve_undo_read(const char *data, unsigned long size)
{
	struct string_list *resolve_undo;
	size_t len;
	char *endptr;
	int i;
	const unsigned rawsz = the_hash_algo->rawsz;

	CALLOC_ARRAY(resolve_undo, 1);
	resolve_undo->strdup_strings = 1;

	while (size) {
		struct string_list_item *lost;
		struct resolve_undo_info *ui;

		len = strlen(data) + 1;
		if (size <= len)
			goto error;
		lost = string_list_insert(resolve_undo, data);
		if (!lost->util)
			lost->util = xcalloc(1, sizeof(*ui));
		ui = lost->util;
		size -= len;
		data += len;

		for (i = 0; i < 3; i++) {
			ui->mode[i] = strtoul(data, &endptr, 8);
			if (!endptr || endptr == data || *endptr)
				goto error;
			len = (endptr + 1) - (char*)data;
			if (size <= len)
				goto error;
			size -= len;
			data += len;
		}

		for (i = 0; i < 3; i++) {
			if (!ui->mode[i])
				continue;
			if (size < rawsz)
				goto error;
			oidread(&ui->oid[i], (const unsigned char *)data,
				the_repository->hash_algo);
			size -= rawsz;
			data += rawsz;
		}
	}
	return resolve_undo;

error:
	string_list_clear(resolve_undo, 1);
	error("Index records invalid resolve-undo information");
	return NULL;
}

void resolve_undo_clear_index(struct index_state *istate)
{
	struct string_list *resolve_undo = istate->resolve_undo;
	if (!resolve_undo)
		return;
	string_list_clear(resolve_undo, 1);
	free(resolve_undo);
	istate->resolve_undo = NULL;
	istate->cache_changed |= RESOLVE_UNDO_CHANGED;
}

int unmerge_index_entry(struct index_state *istate, const char *path,
			struct resolve_undo_info *ru, unsigned ce_flags)
{
	int i = index_name_pos(istate, path, strlen(path));

	if (i < 0) {
		/* unmerged? */
		i = -i - 1;
		if (i < istate->cache_nr &&
		    !strcmp(istate->cache[i]->name, path))
			/* yes, it is already unmerged */
			return 0;
		/* fallthru: resolved to removal */
	} else {
		/* merged - remove it to replace it with unmerged entries */
		remove_index_entry_at(istate, i);
	}

	for (i = 0; i < 3; i++) {
		struct cache_entry *ce;
		if (!ru->mode[i])
			continue;
		ce = make_cache_entry(istate, ru->mode[i], &ru->oid[i],
				      path, i + 1, 0);
		ce->ce_flags |= ce_flags;
		if (add_index_entry(istate, ce, ADD_CACHE_OK_TO_ADD))
			return error("cannot unmerge '%s'", path);
	}
	return 0;
}

void unmerge_index(struct index_state *istate, const struct pathspec *pathspec,
		   unsigned ce_flags)
{
	struct string_list_item *item;

	if (!istate->resolve_undo)
		return;

	/* TODO: audit for interaction with sparse-index. */
	ensure_full_index(istate);

	for_each_string_list_item(item, istate->resolve_undo) {
		const char *path = item->string;
		struct resolve_undo_info *ru = item->util;
		if (!item->util)
			continue;
		if (!match_pathspec(istate, pathspec,
				    item->string, strlen(item->string),
				    0, NULL, 0))
			continue;
		unmerge_index_entry(istate, path, ru, ce_flags);
		free(ru);
		item->util = NULL;
	}
}
